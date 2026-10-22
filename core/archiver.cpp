// SPDX-License-Identifier: GPL-3.0-or-later
#include "archiver.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSaveFile>
#include <QTimer>

#include <algorithm>

#include "database.h"
#include "downloadmanager.h"
#include "feedupdater.h"
#include "filenames.h"
#include "netutil.h"
#include "syncengine.h"

namespace {

const char MetadataFile[] = "episodes.json";
const char FeedFile[] = "feed.xml";
const char FormatName[] = "sailcast-archive";
const int FormatVersion = 1;

QString isoTime(qint64 seconds)
{
    return seconds > 0 ? SyncEngine::formatTimestamp(seconds) : QString();
}

// "cover.jpg", "cover.png", ... if present in the folder
QString findCover(const QString &folder)
{
    const QStringList covers = QDir(folder).entryList(QStringList() << QStringLiteral("cover.*"), QDir::Files);
    return covers.isEmpty() ? QString() : covers.first();
}

// "name.mp3" -> "name (2).mp3"
QString numbered(const QString &fileName, int number)
{
    const int dot = fileName.lastIndexOf(QLatin1Char('.'));
    if (dot <= 0) {
        return QStringLiteral("%1 (%2)").arg(fileName).arg(number);
    }
    return QStringLiteral("%1 (%2)%3").arg(fileName.left(dot)).arg(number).arg(fileName.mid(dot));
}

bool moveFile(const QString &from, const QString &to)
{
    if (QFile::rename(from, to)) {
        return true;
    }
    // Different file systems (e.g. internal storage -> SD card)
    if (QFile::copy(from, to)) {
        QFile::remove(from);
        return true;
    }
    return false;
}

} // namespace

Archiver::Archiver(Database *database, FeedUpdater *updater, DownloadManager *downloads, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_updater(updater)
    , m_downloads(downloads)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

QString Archiver::folderFor(qint64 podcastId) const
{
    const PodcastRow podcast = m_database->podcast(podcastId);
    if (!podcast.id || podcast.archiveFolder.isEmpty() || m_root.isEmpty()) {
        return QString();
    }
    return QDir(m_root).absoluteFilePath(podcast.archiveFolder);
}

// --- Archive ----------------------------------------------------------------

void Archiver::archive(qint64 podcastId)
{
    if (isRunning()) {
        emit failed(QStringLiteral("An archive is already running"));
        return;
    }
    if (m_root.isEmpty()) {
        emit failed(QStringLiteral("No archive folder set"));
        return;
    }
    const PodcastRow podcast = m_database->podcast(podcastId);
    if (!podcast.id) {
        emit failed(QStringLiteral("No podcast with id %1").arg(podcastId));
        return;
    }

    // The folder name is chosen once and kept, even if the title changes
    QString folderName = podcast.archiveFolder;
    if (folderName.isEmpty()) {
        folderName = FileNames::safe(podcast.title, 80);
        if (m_database->isArchiveFolderUsed(folderName, podcastId)) {
            folderName += QStringLiteral(" (%1)").arg(podcastId);
        }
        m_database->setArchiveFolder(podcastId, folderName);
    }

    m_podcastId = podcastId;
    m_result = ArchiveResult();
    m_pending.clear();
    m_result.folder = QDir(m_root).absoluteFilePath(folderName);
    if (!QDir().mkpath(m_result.folder)) {
        const QString error = QStringLiteral("Cannot create the folder %1").arg(m_result.folder);
        m_podcastId = 0;
        emit failed(error);
        return;
    }

    if (!m_updater) {
        saveCover();
        return;
    }
    emit progress(QStringLiteral("Updating the feed"));
    m_connections << connect(m_updater, &FeedUpdater::feedData, this,
                             [this](qint64 id, const QByteArray &xml) {
        if (id != m_podcastId) {
            return;
        }
        QSaveFile file(m_result.folder + QLatin1Char('/') + QLatin1String(FeedFile));
        if (file.open(QIODevice::WriteOnly)) {
            file.write(xml);
            m_result.feedSaved = file.commit();
        }
    });
    const auto feedDone = [this](qint64 id) {
        if (id != m_podcastId) {
            return;
        }
        for (int i = 0; i < m_connections.size(); ++i) {
            disconnect(m_connections.at(i));
        }
        m_connections.clear();
        saveCover();
    };
    m_connections << connect(m_updater, &FeedUpdater::podcastUpdated, this,
                             [feedDone](qint64 id, int) { feedDone(id); });
    m_connections << connect(m_updater, &FeedUpdater::podcastFailed, this,
                             [feedDone](qint64 id, const QString &) { feedDone(id); });
    m_updater->updatePodcast(podcastId, true);
}

void Archiver::saveCover()
{
    const PodcastRow podcast = m_database->podcast(m_podcastId);
    if (podcast.coverUrl.isEmpty()) {
        archiveEpisodes();
        return;
    }
    emit progress(QStringLiteral("Saving the cover"));
    QNetworkRequest request{QUrl(podcast.coverUrl)};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_network->get(request);
    QTimer::singleShot(60000, reply, SLOT(abort()));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        if (reply->error() == QNetworkReply::NoError && !data.isEmpty()) {
            const QString type = reply->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
            QString suffix = QStringLiteral("jpg");
            if (type.contains(QLatin1String("png"))) {
                suffix = QStringLiteral("png");
            } else if (type.contains(QLatin1String("webp"))) {
                suffix = QStringLiteral("webp");
            }
            const QString old = findCover(m_result.folder);
            if (!old.isEmpty()) {
                QFile::remove(m_result.folder + QLatin1Char('/') + old);
            }
            QSaveFile file(m_result.folder + QStringLiteral("/cover.") + suffix);
            if (file.open(QIODevice::WriteOnly)) {
                file.write(data);
                m_result.coverSaved = file.commit();
            }
        }
        archiveEpisodes();
    });
}

void Archiver::archiveEpisodes()
{
    QList<EpisodeRow> episodes = m_database->episodes(m_podcastId);
    std::reverse(episodes.begin(), episodes.end());     // oldest first
    m_result.episodes = episodes.size();

    // File names already taken in this folder (lower case: case-insensitive
    // file systems)
    QSet<QString> used;
    for (int i = 0; i < episodes.size(); ++i) {
        if (!episodes.at(i).archiveFile.isEmpty()) {
            used.insert(episodes.at(i).archiveFile.toLower());
        }
    }

    QList<qint64> toDownload;
    QList<QString> targets;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (e.url.isEmpty()) {
            continue;
        }
        QString name = e.archiveFile;
        if (name.isEmpty()) {
            const QString base = FileNames::forEpisode(e);
            name = base;
            for (int n = 2; used.contains(name.toLower()); ++n) {
                name = numbered(base, n);
            }
            used.insert(name.toLower());
            m_database->setArchiveFile(e.id, name);
        }
        const QString target = m_result.folder + QLatin1Char('/') + name;
        const QFileInfo info(target);

        if (info.exists() && info.size() > 0 && !QFile::exists(target + QStringLiteral(".part"))) {
            if (!e.downloaded || e.downloadPath != target) {
                m_database->setDownloaded(e.id, target);
            }
            ++m_result.present;
        } else if (e.downloaded && e.downloadPath != target && QFile::exists(e.downloadPath)
                   && moveFile(e.downloadPath, target)) {
            m_database->setDownloaded(e.id, target);
            ++m_result.moved;
        } else {
            toDownload.append(e.id);
            targets.append(target);
        }
    }

    if (toDownload.isEmpty() || !m_downloads) {
        finish();
        return;
    }

    emit progress(QStringLiteral("Downloading %1 episodes").arg(toDownload.size()));
    const auto checkDone = [this]() {
        if (m_pending.isEmpty()) {
            for (int i = 0; i < m_connections.size(); ++i) {
                disconnect(m_connections.at(i));
            }
            m_connections.clear();
            finish();
        }
    };
    m_connections << connect(m_downloads, &DownloadManager::downloaded, this,
                             [this, checkDone](qint64 id, const QString &) {
        if (m_pending.remove(id)) {
            ++m_result.downloaded;
            checkDone();
        }
    });
    m_connections << connect(m_downloads, &DownloadManager::failed, this,
                             [this, checkDone](qint64 id, const QString &error) {
        if (m_pending.remove(id)) {
            const EpisodeRow e = m_database->episode(id);
            m_result.failures.append(QStringLiteral("%1: %2").arg(e.title, error));
            checkDone();
        }
    });
    for (int i = 0; i < toDownload.size(); ++i) {
        m_pending.insert(toDownload.at(i));
        m_downloads->download(toDownload.at(i), targets.at(i));
    }
}

void Archiver::finish()
{
    emit progress(QStringLiteral("Writing %1").arg(QLatin1String(MetadataFile)));
    QString error;
    if (!writeMetadata(m_podcastId, &error)) {
        m_result.failures.append(error);
    }
    m_podcastId = 0;
    emit finished(m_result);
}

// --- Metadata ---------------------------------------------------------------

bool Archiver::writeMetadata(qint64 podcastId, QString *error)
{
    const PodcastRow podcast = m_database->podcast(podcastId);
    const QString folder = folderFor(podcastId);
    if (folder.isEmpty()) {
        if (error) {
            *error = QStringLiteral("The podcast has no archive folder");
        }
        return false;
    }

    QJsonObject p;
    p.insert(QStringLiteral("title"), podcast.title);
    p.insert(QStringLiteral("url"), podcast.url);
    p.insert(QStringLiteral("link"), podcast.link);
    p.insert(QStringLiteral("author"), podcast.author);
    p.insert(QStringLiteral("description"), podcast.description);
    p.insert(QStringLiteral("cover_url"), podcast.coverUrl);
    p.insert(QStringLiteral("cover"), findCover(folder));
    p.insert(QStringLiteral("archived"), podcast.archived);

    QJsonArray list;
    QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    std::reverse(episodes.begin(), episodes.end());
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        QJsonObject o;
        o.insert(QStringLiteral("guid"), e.guid);
        o.insert(QStringLiteral("title"), e.title);
        o.insert(QStringLiteral("published"), isoTime(e.published));
        o.insert(QStringLiteral("duration"), e.duration);
        o.insert(QStringLiteral("url"), e.url);
        o.insert(QStringLiteral("mime_type"), e.mimeType);
        o.insert(QStringLiteral("file_size"), static_cast<double>(e.fileSize));
        const bool present = !e.archiveFile.isEmpty()
                && QFile::exists(folder + QLatin1Char('/') + e.archiveFile);
        o.insert(QStringLiteral("file"), present ? e.archiveFile : QString());
        o.insert(QStringLiteral("link"), e.link);
        o.insert(QStringLiteral("image_url"), e.imageUrl);
        o.insert(QStringLiteral("chapters_url"), e.chaptersUrl);
        o.insert(QStringLiteral("description"), m_database->episodeDescription(e.id, false));
        o.insert(QStringLiteral("description_html"), m_database->episodeDescription(e.id, true));
        if (e.chapterCount > 0) {
            QJsonArray chapters;
            const QList<ParsedChapter> list = m_database->chapters(e.id);
            for (int c = 0; c < list.size(); ++c) {
                QJsonObject chapter;
                chapter.insert(QStringLiteral("start"), list.at(c).startMs / 1000.0);
                chapter.insert(QStringLiteral("title"), list.at(c).title);
                if (!list.at(c).href.isEmpty()) {
                    chapter.insert(QStringLiteral("href"), list.at(c).href);
                }
                if (!list.at(c).image.isEmpty()) {
                    chapter.insert(QStringLiteral("image"), list.at(c).image);
                }
                chapters.append(chapter);
            }
            o.insert(QStringLiteral("chapters"), chapters);
        }
        o.insert(QStringLiteral("played"), e.played);
        o.insert(QStringLiteral("position"), e.position);
        o.insert(QStringLiteral("position_updated"), static_cast<double>(e.positionUpdated));
        o.insert(QStringLiteral("last_played"), static_cast<double>(e.lastPlayed));
        // Bookmarks (49): position, note, created - only if there are any
        const QList<BookmarkRow> bookmarks = m_database->bookmarks(e.id);
        if (!bookmarks.isEmpty()) {
            QJsonArray marks;
            for (int b = 0; b < bookmarks.size(); ++b) {
                QJsonObject mark;
                mark.insert(QStringLiteral("position"), bookmarks.at(b).position);
                mark.insert(QStringLiteral("note"), bookmarks.at(b).note);
                mark.insert(QStringLiteral("title"), bookmarks.at(b).title);
                mark.insert(QStringLiteral("created"), static_cast<double>(bookmarks.at(b).created));
                marks.append(mark);
            }
            o.insert(QStringLiteral("bookmarks"), marks);
        }
        list.append(o);
    }

    QJsonObject root;
    root.insert(QStringLiteral("format"), QLatin1String(FormatName));
    root.insert(QStringLiteral("version"), FormatVersion);
    root.insert(QStringLiteral("written"), isoTime(Database::now()));
    root.insert(QStringLiteral("podcast"), p);
    root.insert(QStringLiteral("episodes"), list);

    QSaveFile file(folder + QLatin1Char('/') + QLatin1String(MetadataFile));
    if (!file.open(QIODevice::WriteOnly)) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    file.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    if (!file.commit()) {
        if (error) {
            *error = file.errorString();
        }
        return false;
    }
    return true;
}

ArchiveImportResult Archiver::importFolder(const QString &folder)
{
    ArchiveImportResult result;
    const QString dir = QDir(folder).absolutePath();
    QFile file(dir + QLatin1Char('/') + QLatin1String(MetadataFile));
    if (!file.open(QIODevice::ReadOnly)) {
        result.error = QStringLiteral("No %1 in %2").arg(QLatin1String(MetadataFile), dir);
        return result;
    }
    const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String(FormatName)) {
        result.error = QStringLiteral("%1 is not a Sailcast archive").arg(file.fileName());
        return result;
    }
    if (root.value(QStringLiteral("version")).toInt() > FormatVersion) {
        result.error = QStringLiteral("The archive is from a newer Sailcast (format %1)")
                .arg(root.value(QStringLiteral("version")).toInt());
        return result;
    }
    const QJsonObject p = root.value(QStringLiteral("podcast")).toObject();
    const QString url = p.value(QStringLiteral("url")).toString();
    if (url.isEmpty()) {
        result.error = QStringLiteral("The archive has no feed URL");
        return result;
    }

    qint64 podcastId = m_database->findPodcast(url);
    if (podcastId < 0) {
        podcastId = m_database->addPodcast(url, p.value(QStringLiteral("title")).toString());
        if (podcastId < 0) {
            result.error = m_database->lastError();
            return result;
        }
        result.podcastAdded = true;
    }
    result.podcastId = podcastId;
    m_database->setPodcastMetadata(podcastId, p.value(QStringLiteral("title")).toString(),
                                   p.value(QStringLiteral("link")).toString(),
                                   p.value(QStringLiteral("description")).toString(),
                                   p.value(QStringLiteral("author")).toString(),
                                   p.value(QStringLiteral("cover_url")).toString());
    m_database->setArchiveFolder(podcastId, QDir(dir).dirName());
    if (p.value(QStringLiteral("archived")).toBool()) {
        m_database->setArchived(podcastId, true);
    }

    // The episodes go through the same code as a feed update
    ParsedFeed feed;
    feed.title = p.value(QStringLiteral("title")).toString();
    feed.link = p.value(QStringLiteral("link")).toString();
    feed.description = p.value(QStringLiteral("description")).toString();
    feed.author = p.value(QStringLiteral("author")).toString();
    feed.coverUrl = p.value(QStringLiteral("cover_url")).toString();
    const QJsonArray list = root.value(QStringLiteral("episodes")).toArray();
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject o = list.at(i).toObject();
        ParsedEpisode e;
        e.guid = o.value(QStringLiteral("guid")).toString();
        e.url = o.value(QStringLiteral("url")).toString();
        if (e.guid.isEmpty()) {
            e.guid = e.url;
        }
        if (e.guid.isEmpty()) {
            continue;
        }
        e.title = o.value(QStringLiteral("title")).toString();
        e.description = o.value(QStringLiteral("description")).toString();
        e.descriptionHtml = o.value(QStringLiteral("description_html")).toString();
        e.link = o.value(QStringLiteral("link")).toString();
        e.mimeType = o.value(QStringLiteral("mime_type")).toString();
        e.fileSize = static_cast<qint64>(o.value(QStringLiteral("file_size")).toDouble());
        e.published = SyncEngine::parseTimestamp(o.value(QStringLiteral("published")).toString());
        e.duration = o.value(QStringLiteral("duration")).toInt();
        e.imageUrl = o.value(QStringLiteral("image_url")).toString();
        e.chaptersUrl = o.value(QStringLiteral("chapters_url")).toString();
        const QJsonArray chapters = o.value(QStringLiteral("chapters")).toArray();
        for (int c = 0; c < chapters.size(); ++c) {
            const QJsonObject chapter = chapters.at(c).toObject();
            ParsedChapter pc;
            pc.startMs = static_cast<qint64>(chapter.value(QStringLiteral("start")).toDouble() * 1000.0 + 0.5);
            pc.title = chapter.value(QStringLiteral("title")).toString();
            pc.href = chapter.value(QStringLiteral("href")).toString();
            pc.image = chapter.value(QStringLiteral("image")).toString();
            e.chapters.append(pc);
        }
        feed.episodes.append(e);
    }
    const int added = m_database->applyFeed(podcastId, feed, QString(), QString());
    if (added < 0) {
        result.error = m_database->lastError();
        return result;
    }
    result.episodesAdded = added;

    // Files and listening states
    for (int i = 0; i < list.size(); ++i) {
        const QJsonObject o = list.at(i).toObject();
        QString guid = o.value(QStringLiteral("guid")).toString();
        if (guid.isEmpty()) {
            guid = o.value(QStringLiteral("url")).toString();
        }
        const qint64 episodeId = m_database->findEpisodeByGuid(podcastId, guid);
        if (episodeId < 0) {
            continue;
        }
        const QString fileName = o.value(QStringLiteral("file")).toString();
        if (!fileName.isEmpty() && QFile::exists(dir + QLatin1Char('/') + fileName)) {
            m_database->setArchiveFile(episodeId, fileName);
            m_database->setDownloaded(episodeId, dir + QLatin1Char('/') + fileName);
            ++result.filesFound;
        }
        const qint64 updated = static_cast<qint64>(o.value(QStringLiteral("position_updated")).toDouble());
        if (updated > 0 && m_database->mergeListeningState(
                    episodeId, o.value(QStringLiteral("played")).toBool(),
                    o.value(QStringLiteral("position")).toInt(), updated,
                    static_cast<qint64>(o.value(QStringLiteral("last_played")).toDouble()))) {
            ++result.statesTaken;
        }
        // Bookmarks (49): added unless there is one at the same position
        const QJsonArray marks = o.value(QStringLiteral("bookmarks")).toArray();
        if (!marks.isEmpty()) {
            const QList<BookmarkRow> existing = m_database->bookmarks(episodeId);
            for (int b = 0; b < marks.size(); ++b) {
                const QJsonObject mark = marks.at(b).toObject();
                const int position = mark.value(QStringLiteral("position")).toInt();
                bool known = false;
                for (int k = 0; k < existing.size(); ++k) {
                    if (existing.at(k).position == position) {
                        known = true;
                    }
                }
                if (!known) {
                    const qint64 id = m_database->addBookmark(episodeId, position,
                            static_cast<qint64>(mark.value(QStringLiteral("created")).toDouble()));
                    const QString note = mark.value(QStringLiteral("note")).toString();
                    if (id > 0 && !note.isEmpty()) {
                        m_database->setBookmarkNote(id, note);
                    }
                    const QString title = mark.value(QStringLiteral("title")).toString();
                    if (id > 0 && !title.isEmpty()) {
                        m_database->setBookmarkTitle(id, title);
                    }
                }
            }
        }
    }
    result.ok = true;
    return result;
}

// --- Playback sources -------------------------------------------------------

QList<PlaybackSource> Archiver::playbackSources(Database *database, qint64 episodeId)
{
    QList<PlaybackSource> sources;
    const EpisodeRow e = database->episode(episodeId);
    if (!e.id) {
        return sources;
    }
    if (e.downloaded && QFile::exists(e.downloadPath)) {
        PlaybackSource local;
        local.kind = PlaybackSource::LocalFile;
        local.url = QUrl::fromLocalFile(e.downloadPath);
        sources.append(local);
    }
    if (!e.url.isEmpty()) {
        PlaybackSource original;
        original.kind = PlaybackSource::Original;
        original.url = QUrl(e.url);
        sources.append(original);
    }
    QString base = database->setting(QStringLiteral("archive.url")).trimmed();
    const PodcastRow podcast = database->podcast(e.podcastId);
    if (!base.isEmpty() && !podcast.archiveFolder.isEmpty() && !e.archiveFile.isEmpty()) {
        if (!base.endsWith(QLatin1Char('/'))) {
            base += QLatin1Char('/');
        }
        PlaybackSource archive;
        archive.kind = PlaybackSource::CloudArchive;
        archive.url = QUrl::fromEncoded(base.toUtf8()
                                        + QUrl::toPercentEncoding(podcast.archiveFolder) + '/'
                                        + QUrl::toPercentEncoding(e.archiveFile));
        sources.append(archive);
    }
    return sources;
}

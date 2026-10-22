// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcore.h"

#include <QHash>
#include <QSet>
#include "urlmask.h"
#include "nextcloudlogin.h"
#include "gpodderdevices.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
#include <QNetworkConfiguration>
#include <QNetworkConfigurationManager>
#endif

#include "archiver.h"
#include "chaptersloader.h"
#include "database.h"
#include "downloadmanager.h"
#include "feedprobe.h"
#include "feedupdater.h"
#include "gpodderimport.h"
#include "opml.h"
#include "podcastindex.h"
#include "syncengine.h"

namespace {

AppCore *s_instance = nullptr;

// Automatic downloads: at most this many new episodes per podcast and update
const int AutoDownloadLimit = 3;
// episodes.json of an archived podcast is written this long after the last
// change of its listening state
const int MetadataDelayMs = 60000;

bool isInside(const QString &path, const QString &folder)
{
    if (folder.isEmpty()) {
        return false;
    }
    return QFileInfo(path).absoluteFilePath().startsWith(QDir(folder).absolutePath() + QLatin1Char('/'));
}

QString today()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

// Show notes may contain images and scripts - QML's StyledText would load
// remote images while scrolling. Keep text and links only.
QString cleanDescription(const QString &html)
{
    QString text = html;
    text.remove(QRegularExpression(QStringLiteral("<(script|style)[^>]*>.*?</\\1>"),
                                   QRegularExpression::CaseInsensitiveOption
                                   | QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression(QStringLiteral("<img[^>]*>"), QRegularExpression::CaseInsensitiveOption));
    return text.trimmed();
}

} // namespace

AppCore::AppCore(QObject *parent)
    : QObject(parent)
    , m_database(new Database)
{
    s_instance = this;
    openDatabase();
    // Left over from builds before 16 (see FeedUpdater)
    m_database->clearConnectionErrors();

    m_updater = new FeedUpdater(m_database, this);
    m_downloads = new DownloadManager(m_database, m_updater, this);
    m_downloads->setDownloadDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                      + QStringLiteral("/downloads"));
    m_sync = new SyncEngine(m_database, m_updater, this);
    m_archiver = new Archiver(m_database, m_updater, m_downloads, this);
    m_archiver->setArchiveRoot(archiveRoot());
    m_chapters = new ChaptersLoader(m_database, this);
    m_probe = new FeedProbe(this);
    connect(m_probe, &FeedProbe::finished, this, &AppCore::onFeedChecked);
    m_index = new PodcastIndex(this);
    connect(m_index, &PodcastIndex::searchFinished, this,
            [this](const QString &term, const QList<PodcastSearchResult> &results) {
        QVariantList list;
        for (int i = 0; i < results.size(); ++i) {
            const PodcastSearchResult &r = results.at(i);
            QVariantMap map;
            map.insert(QStringLiteral("title"), r.title);
            map.insert(QStringLiteral("author"), r.author);
            map.insert(QStringLiteral("url"), r.url);
            map.insert(QStringLiteral("imageUrl"), r.imageUrl);
            map.insert(QStringLiteral("description"), r.description);
            map.insert(QStringLiteral("language"), r.language);
            map.insert(QStringLiteral("episodes"), r.episodeCount);
            map.insert(QStringLiteral("link"), r.link);
            map.insert(QStringLiteral("ownerName"), r.ownerName);
            map.insert(QStringLiteral("categories"), r.categories);
            map.insert(QStringLiteral("lastUpdate"), double(r.lastUpdate));
            map.insert(QStringLiteral("explicit"), r.explicitContent);
            map.insert(QStringLiteral("fundingUrl"), r.fundingUrl);
            map.insert(QStringLiteral("fundingMessage"), r.fundingMessage);
            map.insert(QStringLiteral("subscribed"), m_database->findPodcast(r.url) >= 0);
            list.append(map);
        }
        emit searchFinished(term, list);
    });
    // Nextcloud login in the browser (44)
    m_browserLogin = new NextcloudLogin(this);
    connect(m_browserLogin, &NextcloudLogin::activeChanged, this, &AppCore::browserLoginActiveChanged);
    connect(m_browserLogin, &NextcloudLogin::loginUrl, this, &AppCore::browserLoginUrl);
    connect(m_browserLogin, &NextcloudLogin::finished, this, &AppCore::browserLoginFinished);
    connect(m_browserLogin, &NextcloudLogin::failed, this, [this](int error, const QString &detail) {
        QString text;
        switch (error) {
        case NextcloudLogin::InvalidAddress: text = tr("Invalid server address"); break;
        case NextcloudLogin::NoNextcloud: text = tr("No Nextcloud found at this address"); break;
        case NextcloudLogin::NotStarted: text = tr("The server did not start the login - is it a Nextcloud?"); break;
        case NextcloudLogin::TimedOut: text = tr("The login in the browser was not completed in time"); break;
        case NextcloudLogin::Incomplete: text = tr("The server sent an incomplete answer"); break;
        default: text = tr("Login failed: %1").arg(detail); break;
        }
        emit browserLoginFailed(text);
    });
    // gpodder.net devices (51)
    m_devices = new GpodderDevices(this);
    connect(m_devices, &GpodderDevices::listed, this, [this](const QList<GpodderDevice> &devices) {
        QVariantList list;
        for (int i = 0; i < devices.size(); ++i) {
            QVariantMap map;
            map.insert(QStringLiteral("id"), devices.at(i).id);
            map.insert(QStringLiteral("caption"), devices.at(i).caption);
            map.insert(QStringLiteral("type"), devices.at(i).type);
            map.insert(QStringLiteral("subscriptions"), devices.at(i).subscriptions);
            list.append(map);
        }
        emit devicesListed(list);
    });
    connect(m_devices, &GpodderDevices::failed, this, [this](const QString &error) {
        emit devicesFailed(tr("Devices could not be loaded: %1").arg(error));
    });
    connect(m_devices, &GpodderDevices::synchronized, this, [this]() {
        // The Sailcast device exists on the server now
        m_database->setSetting(QStringLiteral("sync.deviceExisting"), QStringLiteral("1"));
        emit settingsChanged();
        emitMessage(tr("The devices now share their subscriptions - synchronize to get them"));
        emit devicesSynchronized();
    });
    connect(m_devices, &GpodderDevices::synchronizeFailed, this, [this](const QString &error) {
        emitMessage(tr("The devices could not be grouped: %1").arg(error));
    });
    connect(m_index, &PodcastIndex::searchFailed, this, [this](const QString &term, const QString &error) {
        // Only the reason: the search page shows "Search failed" as the title
        // above it (59: the text said it twice)
        emit searchFailed(term, UrlMask::maskText(error));
    });
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    m_networkConfig = new QNetworkConfigurationManager(this);
#endif
    m_metadataTimer = new QTimer(this);
    m_metadataTimer->setSingleShot(true);
    m_metadataTimer->setInterval(MetadataDelayMs);
    connect(m_metadataTimer, &QTimer::timeout, this, &AppCore::writeArchiveMetadata);

    // --- Feed updates ---
    connect(m_updater, &FeedUpdater::progress, this, [this](int done, int total) {
        m_updateDone = done;
        m_updateTotal = total;
        emit updatingChanged();
    });
    connect(m_updater, &FeedUpdater::podcastUpdated, this, [this](qint64 id, int newEpisodes) {
        m_updateNew += newEpisodes;
        emit podcastRefreshed(int(id), true);
        if (newEpisodes > 0) {
            autoDownload(id, newEpisodes);
            // "New since you last looked" marker on the podcast (50)
            m_database->setSetting(QStringLiteral("podcast.%1.newAt").arg(id),
                                   QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
        }
    });
    connect(m_updater, &FeedUpdater::podcastFailed, this, [this](qint64 id, const QString &) {
        ++m_updateFailed;
        emit podcastRefreshed(int(id), false);
    });
    connect(m_updater, &FeedUpdater::finished, this, [this]() {
        m_database->applyPendingActions();
        afterPlayed();
        m_updateDone = 0;
        m_updateTotal = 0;
        emit updatingChanged();
        emit dataChanged();
        if (!m_sync->isRunning()) {
            emitMessage(m_updateFailed > 0
                         ? tr("%n new episodes, %1 feeds failed", "", m_updateNew).arg(m_updateFailed)
                         : tr("%n new episodes", "", m_updateNew));
        }
        m_updateNew = 0;
        m_updateFailed = 0;
    });

    // --- Sync ---
    connect(m_sync, &SyncEngine::finished, this, [this](const SyncResult &r) {
        // Episodes played on another device leave the playlist here too
        afterPlayed();
        emit syncingChanged();
        emit dataChanged();
        emit queueChanged();
        emitMessage(tr("Synchronized: %1 listening states, %2 subscription changes")
                     .arg(r.applied.applied + r.actionsUploaded)
                     .arg(r.podcastsAdded + r.podcastsRemoved + r.subscriptionsUploaded));
    });
    connect(m_sync, &SyncEngine::failed, this, [this](const QString &error) {
        emit syncingChanged();
        emitMessage(tr("Sync failed: %1").arg(error));
    });

    // --- Downloads ---
    connect(m_downloads, &DownloadManager::progress, this, [this](qint64 id, qint64 received, qint64 total) {
        const qreal progress = total > 0 ? qreal(received) / qreal(total) : 0.0;
        m_downloadProgress.insert(int(id), progress);
        emit downloadProgressChanged(int(id), progress);
    });
    connect(m_downloads, &DownloadManager::downloaded, this, [this](qint64 id, const QString &) {
        m_downloadProgress.remove(int(id));
        emit downloadFinished(int(id), true);
        emit episodeChanged(int(id));
        emit downloadsChanged();
        // Download counts and the downloads list
        emit dataChanged();
    });
    connect(m_downloads, &DownloadManager::failed, this, [this](qint64 id, const QString &error) {
        m_downloadProgress.remove(int(id));
        emit downloadFinished(int(id), false);
        emit episodeChanged(int(id));
        emit downloadsChanged();
        const bool cancelledHere = m_cancelled.remove(int(id));
        if (!cancelledHere && error != DownloadManager::cancelledError()) {
            emitMessage(tr("Download failed: %1").arg(error));
        }
    });

    // --- Archive ---
    connect(m_archiver, &Archiver::progress, this, [this](const QString &) {
        // The archive queues its downloads right after this signal
        QTimer::singleShot(0, this, [this]() {
            if (m_archiveCancelled) {
                cancelArchive();
            }
            emit downloadsChanged();
        });
    });
    connect(m_archiver, &Archiver::finished, this, &AppCore::onArchiveFinished);
    connect(m_archiver, &Archiver::failed, this, [this](const QString &error) {
        // Also emitted when an archive could not start (no podcast id set)
        if (!m_archiver->isRunning()) {
            m_archivingId = 0;
            m_archiveCancelled = false;
            emit archivingChanged();
        }
        emitMessage(tr("Archiving failed: %1").arg(error));
    });

    // --- Chapters ---
    connect(m_chapters, &ChaptersLoader::loaded, this, [this](qint64 id, int) {
        emit episodeChanged(int(id));
    });

    reloadQueue();
    cleanupOnStart();
}

AppCore::~AppCore()
{
    // Pending episodes.json updates of archived podcasts
    writeArchiveMetadata();
    // The workers use the database - delete them first
    delete m_chapters;
    delete m_archiver;
    delete m_sync;
    delete m_downloads;
    delete m_updater;
    delete m_database;
    s_instance = nullptr;
}

AppCore *AppCore::instance()
{
    return s_instance;
}

QString AppCore::databasePath() const
{
    // Sailjail: ~/.local/share/harbour-sailcast/harbour-sailcast
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/sailcast.db");
}

void AppCore::openDatabase()
{
    m_databaseError.clear();
    if (!m_database->open(databasePath())) {
        m_databaseError = m_database->lastError();
    }
}

QString AppCore::archiveRoot() const
{
    const QString dir = m_database->setting(QStringLiteral("archive.dir"));
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                           + QStringLiteral("/Podcasts")
                         : dir;
}

QString AppCore::version() const
{
    return QStringLiteral("1.0 (build 80)");
}

void AppCore::reloadQueue()
{
    m_queue.clear();
    const QList<qint64> queue = m_database->queueIds();
    for (int i = 0; i < queue.size(); ++i) {
        m_queue.insert(int(queue.at(i)));
    }
}

bool AppCore::updating() const
{
    return m_updater->isBusy();
}

qreal AppCore::updateProgress() const
{
    return m_updateTotal > 0 ? qreal(m_updateDone) / qreal(m_updateTotal) : 0.0;
}

bool AppCore::syncing() const
{
    return m_sync->isRunning();
}

bool AppCore::syncConfigured() const
{
    return m_database->isSyncConfigured();
}

QString AppCore::syncAccount() const
{
    if (!m_database->isSyncConfigured()) {
        return QString();
    }
    return m_database->setting(QStringLiteral("sync.user")) + QStringLiteral(" @ ")
            + QUrl(m_database->setting(QStringLiteral("sync.server"))).host();
}

int AppCore::podcastCount() const
{
    return m_database->stats().podcasts;
}

int AppCore::queueCount() const
{
    return m_queue.size();
}

int AppCore::downloadCount() const
{
    return m_downloads->pendingIds().size();
}

// --- Feeds -------------------------------------------------------------------

void AppCore::updateAll()
{
    m_updater->updateAll();
    emit updatingChanged();
}

void AppCore::updatePodcast(int podcastId)
{
    m_updater->updatePodcast(podcastId);
    emit updatingChanged();
}

void AppCore::subscribe(const QString &url, int folderId)
{
    const qint64 id = m_updater->subscribe(url.trimmed());
    if (id < 0) {
        emitMessage(tr("Cannot subscribe: %1").arg(m_database->lastError()));
        return;
    }
    if (folderId > 0) {
        m_database->setPodcastFolder(id, folderId);
    }
    emit dataChanged();
    emit updatingChanged();
}

void AppCore::unsubscribe(int podcastId)
{
    // Running downloads of the podcast end first
    const QList<qint64> pending = m_downloads->pendingIds();
    for (int i = 0; i < pending.size(); ++i) {
        if (m_database->episode(pending.at(i)).podcastId == podcastId) {
            cancelDownload(int(pending.at(i)));
        }
    }
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        // Files in the archive stay, normal downloads are deleted
        if (!isInside(downloads.at(i).downloadPath, archiveRoot())) {
            QFile::remove(downloads.at(i).downloadPath);
        }
    }
    const QString title = m_database->podcast(podcastId).title;
    if (!m_database->removePodcast(podcastId)) {
        qWarning("[AppCore] Unsubscribe %d failed: %s", podcastId, qPrintable(m_database->lastError()));
        emitMessage(tr("Cannot unsubscribe: %1").arg(m_database->lastError()));
        return;
    }
    m_updater->setPassword(podcastId, QString());
    m_downloads->setPassword(podcastId, QString());
    emit bookmarksChanged(0);   // its bookmarks went with its episodes
    m_database->removeSettings(QStringLiteral("podcast.%1.").arg(podcastId));
    m_metadataDirty.remove(podcastId);
    reloadQueue();
    emit dataChanged();
    emit queueChanged();
    emitMessage(tr("Unsubscribed: %1").arg(title));
}

void AppCore::checkFeed(const QString &url)
{
    m_probe->check(url);
}

bool AppCore::searchAvailable() const
{
    return m_index->isAvailable();
}

void AppCore::searchPodcasts(const QString &term)
{
    m_index->search(term);
}

bool AppCore::isSubscribed(const QString &url) const
{
    const QString normalized = FeedProbe::normalize(url);
    return !normalized.isEmpty() && m_database->findPodcast(normalized) >= 0;
}

void AppCore::onFeedChecked(const FeedProbeResult &r)
{
    QVariantMap map;
    map.insert(QStringLiteral("requested"), r.requestedUrl);
    map.insert(QStringLiteral("ok"), r.ok());
    map.insert(QStringLiteral("needsLogin"), r.status == FeedProbeResult::LoginRequired);
    map.insert(QStringLiteral("url"), r.url);
    map.insert(QStringLiteral("title"), r.title);
    map.insert(QStringLiteral("author"), r.author);
    map.insert(QStringLiteral("coverUrl"), r.coverUrl);
    map.insert(QStringLiteral("episodes"), r.episodes);
    map.insert(QStringLiteral("description"), r.description);
    QVariantList latest;
    for (int i = 0; i < r.latest.size(); ++i) {
        QVariantMap e;
        e.insert(QStringLiteral("title"), r.latest.at(i).title);
        e.insert(QStringLiteral("published"), double(r.latest.at(i).published));
        e.insert(QStringLiteral("duration"), r.latest.at(i).duration);
        latest.append(e);
    }
    map.insert(QStringLiteral("latest"), latest);
    map.insert(QStringLiteral("discovered"), r.discovered);
    map.insert(QStringLiteral("subscribed"), !r.url.isEmpty() && m_database->findPodcast(r.url) >= 0);
    QString error;
    switch (r.status) {
    case FeedProbeResult::Ok:
        break;
    case FeedProbeResult::LoginRequired:
        error = tr("The feed needs a login (HTTP %1) - after subscribing, set it on the podcast page").arg(r.httpStatus);
        break;
    case FeedProbeResult::NotAFeed:
        error = tr("This address is not a podcast feed");
        break;
    case FeedProbeResult::HttpError:
        error = r.httpStatus == 404 || r.httpStatus == 410 ? tr("Nothing found at this address (HTTP %1)").arg(r.httpStatus)
                                                           : tr("The server answered with an error (HTTP %1)").arg(r.httpStatus);
        break;
    case FeedProbeResult::NetworkError:
        error = tr("Not reachable: %1").arg(UrlMask::maskText(r.error));
        break;
    case FeedProbeResult::Timeout:
        error = tr("No answer from the server");
        break;
    case FeedProbeResult::InvalidUrl:
        error = tr("Not a web address");
        break;
    }
    map.insert(QStringLiteral("error"), error);
    emit feedChecked(map);
}

void AppCore::setFeedLogin(int podcastId, const QString &user, const QString &password)
{
    const QString name = user.trimmed();
    m_database->setAuthUser(podcastId, name);
    setFeedPassword(podcastId, name.isEmpty() ? QString() : password);
    emit dataChanged();
}

void AppCore::setFeedPassword(int podcastId, const QString &password)
{
    m_updater->setPassword(podcastId, password);
    m_downloads->setPassword(podcastId, password);
}

QVariantList AppCore::feedsWithLogin() const
{
    QVariantList ids;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        if (!podcasts.at(i).authUser.isEmpty()) {
            ids.append(int(podcasts.at(i).id));
        }
    }
    return ids;
}

// --- Sync --------------------------------------------------------------------

void AppCore::sync(const QString &password)
{
    if (m_sync->isRunning()) {
        return;
    }
    m_sync->setPassword(password);
    m_sync->start();
    emit syncingChanged();
}

bool AppCore::setSyncAccount(const QString &backend, const QString &server, const QString &user)
{
    SyncEngine::Backend type;
    QString address = server.trimmed();
    const QString name = user.trimmed();
    if (!SyncEngine::backendFromName(backend, &type) || address.isEmpty() || name.isEmpty()) {
        return false;
    }
    while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
    }
    if (!address.startsWith(QLatin1String("https://")) && !address.startsWith(QLatin1String("http://"))) {
        address.prepend(QStringLiteral("https://"));
    }
    if (!QUrl(address).isValid() || QUrl(address).host().isEmpty()) {
        return false;
    }
    // Another account or server: start over (the "since" timestamps and the
    // outbox belong to the old one). The device name only matters for the
    // gpodder.net API; an imported database keeps its own.
    const bool sameAccount = m_database->setting(QStringLiteral("sync.backend")) == SyncEngine::backendName(type)
            && m_database->setting(QStringLiteral("sync.server")) == address
            && m_database->setting(QStringLiteral("sync.user")) == name;
    if (!sameAccount) {
        m_database->resetSyncState();
    }
    m_database->setSetting(QStringLiteral("sync.backend"), SyncEngine::backendName(type));
    m_database->setSetting(QStringLiteral("sync.server"), address);
    m_database->setSetting(QStringLiteral("sync.user"), name);
    if (m_database->setting(QStringLiteral("sync.device")).isEmpty()) {
        m_database->setSetting(QStringLiteral("sync.device"), QStringLiteral("sailcast"));
    }
    emit settingsChanged();
    return true;
}

void AppCore::removeSyncAccount()
{
    if (m_sync->isRunning()) {
        emitMessage(tr("Please wait until the sync is finished"));
        return;
    }
    m_database->resetSyncState();
    m_database->setSetting(QStringLiteral("sync.backend"), QString());
    m_database->setSetting(QStringLiteral("sync.server"), QString());
    m_database->setSetting(QStringLiteral("sync.user"), QString());
    m_database->setSetting(QStringLiteral("sync.device"), QString());
    m_database->setSetting(QStringLiteral("sync.deviceExisting"), QString());
    emit settingsChanged();
}

// --- Episodes ----------------------------------------------------------------

QVariantMap AppCore::episode(int episodeId) const
{
    QVariantMap map;
    const EpisodeRow e = m_database->episode(episodeId);
    if (!e.id) {
        return map;
    }
    map.insert(QStringLiteral("episodeId"), int(e.id));
    map.insert(QStringLiteral("podcastId"), int(e.podcastId));
    map.insert(QStringLiteral("title"), e.title);
    map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
    map.insert(QStringLiteral("imageUrl"), e.displayImageUrl);
    map.insert(QStringLiteral("episodeImageUrl"), e.imageUrl);
    map.insert(QStringLiteral("coverUrl"), m_database->podcastCoverUrl(e.podcastId));
    map.insert(QStringLiteral("published"), double(e.published) * 1000.0);
    map.insert(QStringLiteral("duration"), e.duration);
    map.insert(QStringLiteral("position"), e.position);
    map.insert(QStringLiteral("remaining"), e.remaining());
    map.insert(QStringLiteral("played"), e.played);
    map.insert(QStringLiteral("isNew"), e.isNew);
    map.insert(QStringLiteral("downloaded"), e.downloaded);
    map.insert(QStringLiteral("link"), e.link);
    map.insert(QStringLiteral("chapterCount"), e.chapterCount);
    map.insert(QStringLiteral("hasChaptersUrl"), !e.chaptersUrl.isEmpty());
    QString description = m_database->episodeDescription(e.id, true);
    if (description.isEmpty()) {
        description = m_database->episodeDescription(e.id, false);
    }
    map.insert(QStringLiteral("description"), cleanDescription(description));

    QStringList sources;
    const QList<PlaybackSource> list = Archiver::playbackSources(m_database, e.id);
    for (int i = 0; i < list.size(); ++i) {
        sources.append(list.at(i).url.toString(QUrl::FullyEncoded));
    }
    map.insert(QStringLiteral("sources"), sources);

    QVariantList chapters;
    const QList<ParsedChapter> parsed = m_database->chapters(e.id);
    for (int i = 0; i < parsed.size(); ++i) {
        QVariantMap chapter;
        chapter.insert(QStringLiteral("start"), double(parsed.at(i).startMs));
        chapter.insert(QStringLiteral("title"), parsed.at(i).title);
        chapters.append(chapter);
    }
    map.insert(QStringLiteral("chapters"), chapters);
    return map;
}

QVariantMap AppCore::podcast(int podcastId) const
{
    QVariantMap map;
    const PodcastRow p = m_database->podcast(podcastId);
    if (!p.id) {
        return map;
    }
    map.insert(QStringLiteral("podcastId"), int(p.id));
    map.insert(QStringLiteral("title"), p.title);
    map.insert(QStringLiteral("author"), p.author);
    map.insert(QStringLiteral("description"), cleanDescription(p.description));
    map.insert(QStringLiteral("coverUrl"), p.coverUrl);
    map.insert(QStringLiteral("link"), p.link);
    map.insert(QStringLiteral("url"), p.url);
    map.insert(QStringLiteral("episodeCount"), p.episodeCount);
    map.insert(QStringLiteral("unplayed"), p.unplayedCount);
    map.insert(QStringLiteral("archived"), p.archived);
    map.insert(QStringLiteral("manualUpdate"), p.manualUpdate);
    map.insert(QStringLiteral("folderId"), int(p.folderId));
    map.insert(QStringLiteral("archiveFolder"), p.archiveFolder.isEmpty()
               ? QString() : QDir(archiveRoot()).absoluteFilePath(p.archiveFolder));
    map.insert(QStringLiteral("lastError"), p.lastError);
    map.insert(QStringLiteral("authUser"), p.authUser);
    map.insert(QStringLiteral("downloadedCount"), p.downloadedCount);
    // For the archive dialog: size as declared in the feed (often missing)
    qint64 bytes = 0;
    int withMedia = 0;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    for (int i = 0; i < episodes.size(); ++i) {
        if (!episodes.at(i).url.isEmpty()) {
            ++withMedia;
            bytes += qMax<qint64>(0, episodes.at(i).fileSize);
        }
    }
    map.insert(QStringLiteral("mediaCount"), withMedia);
    map.insert(QStringLiteral("declaredBytes"), double(bytes));
    return map;
}

void AppCore::markPlayed(int episodeId)
{
    m_database->markPlayed(episodeId);
    archiveStateChanged(episodeId);
    afterPlayed();
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::markUnplayed(int episodeId)
{
    m_database->markUnplayed(episodeId);
    archiveStateChanged(episodeId);
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::markAllPlayed(int podcastId)
{
    const int count = m_database->markAllPlayed(podcastId);
    afterPlayed();
    if (!m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

void AppCore::recordPlayback(int episodeId, int started, int position, int total)
{
    const bool wasPlayed = m_database->episode(episodeId).played;
    m_database->recordPlayback(episodeId, started, position, total);
    archiveStateChanged(episodeId);
    emit episodeChanged(episodeId);
    // Played state changed: counts in the lists change too
    const bool played = m_database->episode(episodeId).played;
    if (played && !wasPlayed) {
        afterPlayed();
    }
    if (played != wasPlayed) {
        emit dataChanged();
    }
}

void AppCore::refreshPodcast(int podcastId)
{
    // Full update: the feed may answer 304 although the links in it changed
    m_updater->updatePodcast(podcastId, true);
    emit updatingChanged();
}

void AppCore::loadChapters(int episodeId)
{
    m_chapters->load(QList<qint64>() << episodeId);
}

// --- Downloads ---------------------------------------------------------------

void AppCore::download(int episodeId)
{
    if (m_downloadProgress.contains(episodeId)) {
        return;
    }
    m_cancelled.remove(episodeId);
    m_downloadProgress.insert(episodeId, 0.0);
    emit downloadProgressChanged(episodeId, 0.0);
    m_downloads->download(episodeId);
    emit downloadsChanged();
}

void AppCore::cancelDownload(int episodeId)
{
    m_cancelled.insert(episodeId);
    if (!m_downloads->cancel(episodeId)) {
        m_cancelled.remove(episodeId);
        m_downloadProgress.remove(episodeId);
        emit downloadFinished(episodeId, false);
        emit downloadsChanged();
    }
}

void AppCore::cancelAllDownloads()
{
    const QList<qint64> ids = m_downloads->pendingIds();
    for (int i = ids.size() - 1; i >= 0; --i) {
        cancelDownload(int(ids.at(i)));
    }
}

void AppCore::deleteDownload(int episodeId)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (e.downloaded) {
        if (isInside(e.downloadPath, archiveRoot())) {
            emitMessage(tr("The file is part of the archive and stays"));
            return;
        }
        QFile::remove(e.downloadPath);
        m_database->clearDownload(episodeId);
    }
    emit episodeChanged(episodeId);
    emit dataChanged();
}

bool AppCore::isDownloading(int episodeId) const
{
    return m_downloadProgress.contains(episodeId) || m_downloads->pendingIds().contains(episodeId);
}

qreal AppCore::downloadProgress(int episodeId) const
{
    return m_downloadProgress.value(episodeId, 0.0);
}

QVariantList AppCore::activeDownloads() const
{
    QVariantList list;
    const QList<qint64> ids = m_downloads->pendingIds();
    for (int i = 0; i < ids.size(); ++i) {
        const EpisodeRow e = m_database->episode(ids.at(i));
        if (!e.id) {
            continue;
        }
        QVariantMap map;
        map.insert(QStringLiteral("episodeId"), int(e.id));
        map.insert(QStringLiteral("title"), e.title);
        map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
        map.insert(QStringLiteral("imageUrl"), e.displayImageUrl);
        map.insert(QStringLiteral("progress"), m_downloadProgress.value(int(e.id), 0.0));
        map.insert(QStringLiteral("running"), m_downloads->isActive(e.id));
        list.append(map);
    }
    return list;
}

QVariantMap AppCore::downloadStats() const
{
    int count = 0;
    qint64 bytes = 0;
    const QList<EpisodeRow> downloads = m_database->episodes(-1, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        const QFileInfo info(downloads.at(i).downloadPath);
        if (info.exists()) {
            ++count;
            bytes += info.size();
        }
    }
    QVariantMap map;
    map.insert(QStringLiteral("count"), count);
    map.insert(QStringLiteral("bytes"), double(bytes));
    return map;
}

int AppCore::cleanupDownloads(int days)
{
    if (days < 0) {
        bool ok = false;
        days = m_database->setting(QStringLiteral("cleanup.days"), QStringLiteral("7")).toInt(&ok);
        if (!ok || days < 0) {
            return 0;
        }
    }
    const CleanupResult r = m_downloads->cleanup(days, archiveRoot());
    emit dataChanged();
    emitMessage(tr("%n downloads deleted", "", r.files));
    return r.files;
}

void AppCore::afterPlayed()
{
    // Played episodes leave the playlist
    const QList<qint64> queue = m_database->queueIds();
    bool queueChangedHere = false;
    for (int i = 0; i < queue.size(); ++i) {
        if (m_database->episode(queue.at(i)).played) {
            m_database->queueRemove(queue.at(i));
            queueChangedHere = true;
        }
    }
    if (queueChangedHere) {
        reloadQueue();
        emit queueChanged();
    }
    // "Right after listening": delete their downloads now, not only at the
    // next start - never the file in the player
    if (m_database->setting(QStringLiteral("cleanup.days"), QStringLiteral("7")) == QLatin1String("0")) {
        const CleanupResult r = m_downloads->cleanup(0, archiveRoot(), m_currentEpisode);
        if (r.files > 0) {
            emit dataChanged();
        }
    }
}

void AppCore::cleanupOnStart()
{
    // "cleanup.days": -1 = never, 0 = right after listening, N = days
    bool ok = false;
    const int days = m_database->setting(QStringLiteral("cleanup.days"), QStringLiteral("7")).toInt(&ok);
    if (ok && days >= 0) {
        m_downloads->cleanup(days, archiveRoot());
    }
}

bool AppCore::onWifi() const
{
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    // Sailfish OS (Qt 5.6): the active connections of the bearer plugin
    const QList<QNetworkConfiguration> active
            = m_networkConfig->allConfigurations(QNetworkConfiguration::Active);
    for (int i = 0; i < active.size(); ++i) {
        const QNetworkConfiguration::BearerType type = active.at(i).bearerType();
        if (type == QNetworkConfiguration::BearerWLAN || type == QNetworkConfiguration::BearerEthernet) {
            return true;
        }
    }
    return false;
#else
    // Bearer management is gone from Qt 5.15 on (desktop only)
    return true;
#endif
}

bool AppCore::downloadAllowed() const
{
    return m_database->setting(QStringLiteral("downloads.wifiOnly"), QStringLiteral("0")) != QLatin1String("1")
            || onWifi();
}

void AppCore::autoDownload(qint64 podcastId, int newEpisodes)
{
    if (m_database->setting(QStringLiteral("downloads.auto"), QStringLiteral("0")) != QLatin1String("1")
            || !downloadAllowed()) {
        return;
    }
    const PodcastRow podcast = m_database->podcast(podcastId);
    // The first update of a subscription brings the whole back catalog -
    // nothing is loaded automatically then
    // Being archived: the archive loads the new episodes into its folder
    if (!podcast.id || podcast.archived || newEpisodes >= podcast.episodeCount
            || podcast.id == m_archivingId) {
        return;
    }
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, qMin(newEpisodes, AutoDownloadLimit),
                                                            Database::NewEpisodes);
    for (int i = 0; i < episodes.size(); ++i) {
        if (!episodes.at(i).downloaded && !episodes.at(i).url.isEmpty()) {
            download(int(episodes.at(i).id));
        }
    }
}

// --- Playlist ----------------------------------------------------------------

void AppCore::queueAdd(int episodeId, bool next)
{
    m_database->queueAdd(episodeId, next);
    m_queue.insert(episodeId);
    emit queueChanged();
    emit episodeChanged(episodeId);
}

void AppCore::queueRemove(int episodeId)
{
    m_database->queueRemove(episodeId);
    m_queue.remove(episodeId);
    emit queueChanged();
    emit episodeChanged(episodeId);
}

void AppCore::queueMove(int episodeId, int newIndex)
{
    m_database->queueMove(episodeId, newIndex);
    emit queueChanged();
}

void AppCore::queueSetOrder(const QVariantList &episodeIds)
{
    QList<qint64> ids;
    // Queued episodes that are not in the list (the current one) first
    const QList<qint64> current = m_database->queueIds();
    QSet<qint64> shown;
    for (int i = 0; i < episodeIds.size(); ++i) {
        shown.insert(episodeIds.at(i).toLongLong());
    }
    for (int i = 0; i < current.size(); ++i) {
        if (!shown.contains(current.at(i))) {
            ids.append(current.at(i));
        }
    }
    for (int i = 0; i < episodeIds.size(); ++i) {
        ids.append(episodeIds.at(i).toLongLong());
    }
    m_database->queueSetOrder(ids);
    emit queueChanged();
}

void AppCore::queueClear()
{
    m_database->queueClear();
    reloadQueue();
    emit queueChanged();
    emit dataChanged();
}

bool AppCore::isQueued(int episodeId) const
{
    return m_queue.contains(episodeId);
}

int AppCore::queueNext(int finishedEpisodeId)
{
    if (m_queue.contains(finishedEpisodeId)) {
        queueRemove(finishedEpisodeId);
    }
    const QList<qint64> ids = m_database->queueIds();
    return ids.isEmpty() ? -1 : int(ids.first());
}

int AppCore::nextEpisode(int currentEpisodeId) const
{
    const QList<qint64> queue = m_database->queueIds();
    for (int i = 0; i < queue.size(); ++i) {
        if (queue.at(i) != currentEpisodeId) {
            return int(queue.at(i));
        }
    }
    const QList<EpisodeRow> started = m_database->episodes(-1, 2, Database::InProgress);
    for (int i = 0; i < started.size(); ++i) {
        if (started.at(i).id != currentEpisodeId) {
            return int(started.at(i).id);
        }
    }
    return -1;
}

int AppCore::inProgressCount(int excludeEpisodeId) const
{
    int count = m_database->stats().inProgress;
    if (excludeEpisodeId > 0 && m_database->episode(excludeEpisodeId).inProgress()) {
        --count;
    }
    return qMax(0, count);
}

// --- Archive -----------------------------------------------------------------

void AppCore::archivePodcast(int podcastId)
{
    if (m_archiver->isRunning()) {
        emitMessage(tr("An archive is already running"));
        return;
    }
    if (!QDir().mkpath(archiveRoot())) {
        emitMessage(tr("Cannot create the archive folder %1").arg(archiveRoot()));
        return;
    }
    m_archiver->setArchiveRoot(archiveRoot());
    m_archivingId = podcastId;
    m_archiveCancelled = false;
    emit archivingChanged();
    m_archiver->archive(podcastId);
}

void AppCore::cancelArchive()
{
    if (!m_archivingId) {
        return;
    }
    m_archiveCancelled = true;
    const QList<qint64> pending = m_downloads->pendingIds();
    for (int i = 0; i < pending.size(); ++i) {
        if (m_database->episode(pending.at(i)).podcastId == m_archivingId) {
            cancelDownload(int(pending.at(i)));
        }
    }
}

void AppCore::onArchiveFinished(const ArchiveResult &result)
{
    const bool cancelled = m_archiveCancelled;
    m_archivingId = 0;
    m_archiveCancelled = false;
    emit archivingChanged();
    emit dataChanged();
    emit downloadsChanged();
    const int inArchive = result.present + result.moved + result.downloaded;
    if (cancelled) {
        emitMessage(tr("Archiving cancelled: %n episodes in the archive", "", inArchive));
    } else if (!result.failures.isEmpty()) {
        emitMessage(tr("Archived: %n episodes, %1 failed", "", inArchive).arg(result.failures.size()));
    } else {
        emitMessage(tr("Archived: %n episodes", "", inArchive));
    }
}

void AppCore::setManualUpdate(int podcastId, bool manual)
{
    m_database->setManualUpdate(podcastId, manual);
    emit dataChanged();
}

void AppCore::setArchived(int podcastId, bool archived)
{
    m_database->setArchived(podcastId, archived);
    if (!m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
}

QString AppCore::archiveDirectory() const
{
    return archiveRoot();
}

QString AppCore::defaultArchiveDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Podcasts");
}

bool AppCore::setArchiveDirectory(const QString &path)
{
    QString folder = path.trimmed();
    if (folder.startsWith(QLatin1String("~/"))) {
        folder = QDir::homePath() + folder.mid(1);
    }
    while (folder.length() > 1 && folder.endsWith(QLatin1Char('/'))) {
        folder.chop(1);
    }
    if (folder == defaultArchiveDirectory()) {
        folder.clear();
    }
    if (!folder.isEmpty()) {
        const QFileInfo info(folder);
        if (!info.isAbsolute() || !QDir().mkpath(folder) || !QFileInfo(folder).isWritable()) {
            emitMessage(tr("Cannot write to %1").arg(folder));
            return false;
        }
    }
    m_database->setSetting(QStringLiteral("archive.dir"), folder);
    m_archiver->setArchiveRoot(archiveRoot());
    emit settingsChanged();
    return true;
}

QVariantList AppCore::archiveFolders() const
{
    QVariantList list;
    const QFileInfoList dirs = QDir(archiveRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                 QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < dirs.size(); ++i) {
        QFile file(dirs.at(i).absoluteFilePath() + QStringLiteral("/episodes.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject podcast = root.value(QStringLiteral("podcast")).toObject();
        const QString url = podcast.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) {
            continue;
        }
        QVariantMap map;
        map.insert(QStringLiteral("path"), dirs.at(i).absoluteFilePath());
        map.insert(QStringLiteral("title"), podcast.value(QStringLiteral("title")).toString());
        map.insert(QStringLiteral("episodes"), root.value(QStringLiteral("episodes")).toArray().size());
        map.insert(QStringLiteral("subscribed"), m_database->findPodcast(url) >= 0);
        list.append(map);
    }
    return list;
}

void AppCore::importArchiveFolder(const QString &path)
{
    const ArchiveImportResult r = m_archiver->importFolder(path);
    if (!r.ok) {
        emitMessage(tr("Import failed: %1").arg(r.error));
        return;
    }
    emit dataChanged();
    emitMessage(tr("Archive imported: %n episodes, %1 files", "", r.episodesAdded).arg(r.filesFound));
}

void AppCore::archiveStateChanged(int episodeId)
{
    const qint64 podcastId = m_database->episode(episodeId).podcastId;
    if (podcastId && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
}

void AppCore::writeArchiveMetadata()
{
    m_metadataTimer->stop();
    const QList<qint64> ids = m_metadataDirty.values();
    m_metadataDirty.clear();
    for (int i = 0; i < ids.size(); ++i) {
        // Not while the archive itself writes it
        if (m_archiver->isRunning() && ids.at(i) == m_archivingId) {
            continue;
        }
        QString error;
        if (!m_archiver->writeMetadata(ids.at(i), &error)) {
            qWarning("[AppCore] episodes.json: %s", qPrintable(error));
        }
    }
}

// --- Settings ----------------------------------------------------------------

QString AppCore::setting(const QString &key, const QString &defaultValue) const
{
    return m_database->setting(key, defaultValue);
}

void AppCore::setSetting(const QString &key, const QString &value)
{
    m_database->setSetting(key, value);
    emit settingsChanged();
}

// --- Import and export -------------------------------------------------------

void AppCore::importOpml(const QString &path, int folderId)
{
    QString error;
    const QList<OpmlEntry> entries = Opml::read(path, &error);
    if (entries.isEmpty()) {
        emitMessage(error.isEmpty() ? tr("No feeds in %1").arg(QFileInfo(path).fileName())
                                     : tr("Import failed: %1").arg(error));
        return;
    }
    int added = 0;
    for (int i = 0; i < entries.size(); ++i) {
        qint64 id = m_database->findPodcast(entries.at(i).url);
        const bool existed = id >= 0;
        if (!existed) {
            id = m_database->addPodcast(entries.at(i).url, entries.at(i).title);
            if (id >= 0) {
                ++added;
            }
        }
        // Folders from the file (45): new podcasts, and existing ones that
        // are in no folder yet
        if (id >= 0 && !entries.at(i).folder.isEmpty()
                && (!existed || m_database->podcast(id).folderId == 0)) {
            m_database->setPodcastFolder(id, m_database->createFolder(entries.at(i).folder));
        } else if (id >= 0 && !existed && folderId > 0) {
            // No folder in the file: the folder chosen on the import page
            m_database->setPodcastFolder(id, folderId);
        }
    }
    emit dataChanged();
    emitMessage(added > 0 ? tr("%n podcasts added - loading the episodes", "", added)
                           : tr("All podcasts of the file are already subscribed"));
    if (added > 0) {
        updateAll();
    }
}

void AppCore::importGpodder(const QString &path)
{
    if (m_updater->isBusy() || m_sync->isRunning()) {
        emitMessage(tr("Please wait until updates and the sync are finished"));
        return;
    }
    const GpodderImportResult r = GpodderImport::run(m_database, path);
    if (!r.ok) {
        emitMessage(tr("Import failed: %1").arg(r.error));
        return;
    }
    reloadQueue();
    emit dataChanged();
    emitMessage(tr("gPodder: %n podcasts added, %1 episodes, %2 played, %3 in progress", "", r.podcastsAdded)
                 .arg(r.episodesAdded + r.episodesMerged).arg(r.played).arg(r.inProgress));
    updateAll();
}

void AppCore::importDatabase(const QString &path)
{
    if (m_updater->isBusy() || m_sync->isRunning() || m_downloads->isBusy() || m_archiver->isRunning()) {
        emitMessage(tr("Please wait until updates and downloads are finished"));
        return;
    }
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(databasePath()).absoluteFilePath()) {
        return;
    }
    writeArchiveMetadata();
    const QString target = databasePath();
    m_database->close();
    QFile::remove(target);
    QFile::remove(target + QStringLiteral("-wal"));
    QFile::remove(target + QStringLiteral("-shm"));
    const bool copied = QFile::copy(path, target);
    if (copied) {
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    }
    openDatabase();
    // Folders of the other device (e.g. /tmp on the desktop) do not exist
    // here: back to the defaults
    m_database->setSetting(QStringLiteral("archive.dir"), QString());
    m_database->setSetting(QStringLiteral("downloads.dir"), QString());
    m_archiver->setArchiveRoot(archiveRoot());
    reloadQueue();
    emit settingsChanged();
    emit dataChanged();
    emit queueChanged();
    emitMessage(copied && m_databaseError.isEmpty()
                 ? tr("Database taken over: %n podcasts", "", m_database->stats().podcasts)
                 : tr("Import failed: %1").arg(m_databaseError.isEmpty() ? path : m_databaseError));
}

QString AppCore::exportOpml(const QVariantList &folderIds)
{
    QList<OpmlEntry> entries;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    // Folders (45): written as enclosing outlines, in the folder order
    QSet<int> wanted;
    for (int i = 0; i < folderIds.size(); ++i) {
        wanted.insert(folderIds.at(i).toInt());
    }
    const QList<FolderRow> folders = m_database->folders();
    QHash<qint64, QString> folderNames;
    for (int f = 0; f < folders.size(); ++f) {
        folderNames.insert(folders.at(f).id, folders.at(f).name);
    }
    for (int f = -1; f < folders.size(); ++f) {
        const qint64 folderId = f < 0 ? 0 : folders.at(f).id;
        for (int i = 0; i < podcasts.size(); ++i) {
            if (podcasts.at(i).folderId != folderId
                    && !(f < 0 && !folderNames.contains(podcasts.at(i).folderId))) {
                continue;
            }
            OpmlEntry entry;
            entry.title = podcasts.at(i).title;
            entry.url = podcasts.at(i).url;
            entry.htmlUrl = podcasts.at(i).link;
            entry.folder = folderNames.value(podcasts.at(i).folderId);
            // Only the chosen folders (45); a podcast in an unknown folder
            // counts as "Other podcasts" (0)
            const int key = folderNames.contains(podcasts.at(i).folderId) ? int(podcasts.at(i).folderId) : 0;
            if (!wanted.isEmpty() && !wanted.contains(key)) {
                continue;
            }
            entries.append(entry);
        }
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + QStringLiteral("/sailcast-subscriptions-%1.opml").arg(today());
    QString error;
    if (!Opml::write(path, entries, &error)) {
        emitMessage(tr("Export failed: %1").arg(error));
        return QString();
    }
    return path;
}

QString AppCore::exportDatabase()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + QStringLiteral("/sailcast-%1.db").arg(today());
    // Everything from the write-ahead log into the database file first
    QSqlQuery q(m_database->handle());
    q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    QFile::remove(path);
    if (!QFile::copy(databasePath(), path)) {
        emitMessage(tr("Export failed: %1").arg(path));
        return QString();
    }
    return path;
}

// All messages to the user without tokens from feed/media addresses (40)
void AppCore::emitMessage(const QString &text)
{
    emit message(UrlMask::maskText(text));
}

QString AppCore::maskUrl(const QString &text) const
{
    return UrlMask::maskText(text);
}

void AppCore::startBrowserLogin(const QString &server)
{
    m_browserLogin->start(server);
}

void AppCore::cancelBrowserLogin()
{
    m_browserLogin->cancel();
}

bool AppCore::browserLoginActive() const
{
    return m_browserLogin->isActive();
}

// --- Folders (45) -------------------------------------------------------------

QVariantList AppCore::folders() const
{
    QVariantList list;
    const QList<FolderRow> rows = m_database->folders();
    for (int i = 0; i < rows.size(); ++i) {
        QVariantMap map;
        map.insert(QStringLiteral("folderId"), int(rows.at(i).id));
        map.insert(QStringLiteral("name"), rows.at(i).name);
        map.insert(QStringLiteral("manualUpdate"), rows.at(i).manualUpdate);
        map.insert(QStringLiteral("podcastCount"), rows.at(i).podcastCount);
        list.append(map);
    }
    return list;
}

int AppCore::createFolder(const QString &name)
{
    const qint64 id = m_database->createFolder(name);
    emit dataChanged();
    return int(id);
}

void AppCore::renameFolder(int folderId, const QString &name)
{
    m_database->renameFolder(folderId, name);
    emit dataChanged();
}

void AppCore::moveFolder(int folderId, int step)
{
    m_database->moveFolder(folderId, step);
    emit dataChanged();
}

void AppCore::setFolderManualUpdate(int folderId, bool manual)
{
    m_database->setFolderManualUpdate(folderId, manual);
    emit dataChanged();
}

bool AppCore::folderManualUpdate(int folderId) const
{
    const QList<FolderRow> rows = m_database->folders();
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).id == folderId) {
            return rows.at(i).manualUpdate;
        }
    }
    return false;
}

void AppCore::deleteFolder(int folderId)
{
    m_database->deleteFolder(folderId);
    emit dataChanged();
}

QVariantList AppCore::podcastIdsInFolder(int folderId) const
{
    QVariantList list;
    const QList<qint64> ids = m_database->podcastIdsInFolder(folderId);
    for (int i = 0; i < ids.size(); ++i) {
        list.append(int(ids.at(i)));
    }
    return list;
}

void AppCore::setPodcastFolder(int podcastId, int folderId)
{
    m_database->setPodcastFolder(podcastId, folderId);
    emit dataChanged();
}

void AppCore::markFolderPlayed(int folderId)
{
    const QList<qint64> ids = m_database->podcastIdsInFolder(folderId);
    int count = 0;
    for (int i = 0; i < ids.size(); ++i) {
        count += m_database->markAllPlayed(ids.at(i));
        if (!m_database->podcast(ids.at(i)).archiveFolder.isEmpty()) {
            m_metadataDirty.insert(ids.at(i));
            m_metadataTimer->start();
        }
    }
    afterPlayed();
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

QString AppCore::otherFolderName() const
{
    return tr("Other podcasts");
}

// --- Bookmarks (47) -------------------------------------------------------------

QVariantList AppCore::bookmarks(int episodeId) const
{
    QVariantList list;
    const QList<BookmarkRow> rows = m_database->bookmarks(episodeId);
    for (int i = 0; i < rows.size(); ++i) {
        const BookmarkRow &b = rows.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("bookmarkId"), int(b.id));
        map.insert(QStringLiteral("episodeId"), int(b.episodeId));
        map.insert(QStringLiteral("position"), b.position);
        map.insert(QStringLiteral("note"), b.note);
        map.insert(QStringLiteral("title"), b.title);
        map.insert(QStringLiteral("created"), double(b.created));
        map.insert(QStringLiteral("episodeTitle"), b.episodeTitle);
        map.insert(QStringLiteral("podcastTitle"), b.podcastTitle);
        map.insert(QStringLiteral("imageUrl"), b.imageUrl);
        list.append(map);
    }
    return list;
}

int AppCore::addBookmark(int episodeId, int seconds)
{
    const qint64 id = m_database->addBookmark(episodeId, seconds);
    if (id > 0) {
        bookmarkArchiveChanged(episodeId);
        emit bookmarksChanged(episodeId);
        emit episodeChanged(episodeId);     // star in the lists
    }
    return int(id);
}

void AppCore::editBookmark(int bookmarkId, const QString &title, const QString &note)
{
    m_database->setBookmarkTitle(bookmarkId, title);
    setBookmarkNote(bookmarkId, note);
}

void AppCore::setBookmarkNote(int bookmarkId, const QString &note)
{
    m_database->setBookmarkNote(bookmarkId, note);
    const QList<BookmarkRow> all = m_database->bookmarks(0);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == bookmarkId) {
            bookmarkArchiveChanged(all.at(i).episodeId);
        }
    }
    emit bookmarksChanged(0);
}

void AppCore::removeBookmark(int bookmarkId)
{
    const qint64 episodeId = m_database->removeBookmark(bookmarkId);
    bookmarkArchiveChanged(episodeId);
    emit bookmarksChanged(int(episodeId));
    if (episodeId > 0) {
        emit episodeChanged(int(episodeId));
    }
}

int AppCore::bookmarkCount() const
{
    return m_database->bookmarkCount();
}

// episodes.json of an archived podcast holds its bookmarks (49): written
// again after a while, like after listening
void AppCore::bookmarkArchiveChanged(qint64 episodeId)
{
    const qint64 podcastId = m_database->episode(episodeId).podcastId;
    if (podcastId > 0 && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
}

void AppCore::markPodcastSeen(int podcastId)
{
    if (!m_database->podcast(podcastId).freshEpisodes) {
        return;
    }
    m_database->setSetting(QStringLiteral("podcast.%1.seenAt").arg(podcastId),
                           QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
    emit dataChanged();
}

// --- gpodder.net devices (51) ---------------------------------------------------

void AppCore::listDevices(const QString &server, const QString &user, const QString &password)
{
    m_devices->setAccount(server, user, password);
    m_devices->list();
}

void AppCore::chooseDevice(const QString &deviceId, bool existing)
{
    const QString id = deviceId.trimmed();
    if (id.isEmpty()) {
        return;
    }
    if (m_database->setting(QStringLiteral("sync.device")) != id) {
        // Another device has other subscriptions: start over with a first
        // sync (union, nothing is lost on either side)
        m_database->resetSyncState();
    }
    m_database->setSetting(QStringLiteral("sync.device"), id);
    m_database->setSetting(QStringLiteral("sync.deviceExisting"), existing ? QStringLiteral("1") : QStringLiteral("0"));
    emit settingsChanged();
}

void AppCore::synchronizeDevices(const QStringList &deviceIds, const QString &newDevice)
{
    m_devices->synchronize(deviceIds, newDevice.trimmed());
}

QStringList AppCore::coverUrls() const
{
    QStringList urls;
    const QList<PodcastRow> rows = m_database->podcasts();
    for (int i = 0; i < rows.size(); ++i) {
        if (!rows.at(i).coverUrl.isEmpty()) {
            urls.append(rows.at(i).coverUrl);
        }
    }
    return urls;
}

void AppCore::resetHints()
{
    m_database->removeSettings(QStringLiteral("hint."));
}

void AppCore::markOlderPlayed(int episodeId)
{
    const qint64 podcastId = m_database->episode(episodeId).podcastId;
    const int count = m_database->markOlderPlayed(episodeId);
    afterPlayed();
    if (podcastId > 0 && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "downloadmanager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QSet>
#include <QTimer>

#include "database.h"
#include "feedupdater.h"
#include "filenames.h"
#include "netutil.h"

namespace {
const int MaxRedirects = 10;
const int StallTimeoutMs = 60000;
const char PartSuffix[] = ".part";
}

DownloadManager::DownloadManager(Database *database, FeedUpdater *updater, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_updater(updater)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
    if (m_updater) {
        // Retry downloads that waited for a feed update (expired links)
        connect(m_updater, &FeedUpdater::podcastUpdated, this,
                [this](qint64 podcastId, int) { onFeedUpdated(podcastId); });
        connect(m_updater, &FeedUpdater::podcastFailed, this,
                [this](qint64 podcastId, const QString &) { onFeedUpdated(podcastId); });
    }
}

DownloadManager::~DownloadManager()
{
    for (QHash<qint64, JobPtr>::iterator it = m_active.begin(); it != m_active.end(); ++it) {
        if (it.value()->reply) {
            it.value()->reply->abort();
        }
        closeFile(it.value());
    }
}

void DownloadManager::setMaxParallel(int count)
{
    m_maxParallel = qBound(1, count, 8);
}

void DownloadManager::setPassword(qint64 podcastId, const QString &password)
{
    if (password.isEmpty()) {
        m_passwords.remove(podcastId);
    } else {
        m_passwords.insert(podcastId, password);
    }
}

QString DownloadManager::defaultTarget(qint64 episodeId) const
{
    const EpisodeRow episode = m_database->episode(episodeId);
    if (!episode.id) {
        return QString();
    }
    return m_directory + QLatin1Char('/') + FileNames::safe(episode.podcastTitle)
            + QLatin1Char('/') + FileNames::forEpisode(episode);
}

void DownloadManager::download(qint64 episodeId, const QString &targetPath)
{
    if (m_active.contains(episodeId)) {
        return;
    }
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).episodeId == episodeId) {
            return;
        }
    }
    Queued queued;
    queued.episodeId = episodeId;
    queued.target = targetPath;
    m_queue.append(queued);
    // Started from the event loop, so callers can queue several episodes
    // and connect to the signals first
    QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
}

bool DownloadManager::cancel(qint64 episodeId)
{
    for (int i = 0; i < m_queue.size(); ++i) {
        if (m_queue.at(i).episodeId == episodeId) {
            m_queue.removeAt(i);
            emit failed(episodeId, cancelledError());
            QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
            return true;
        }
    }
    const JobPtr job = m_active.value(episodeId);
    if (!job) {
        return false;
    }
    job->cancelled = true;
    if (job->reply) {
        // onFinished() sees "cancelled" and cleans up
        job->reply->abort();
    } else {
        // Waiting for a feed update - no request running
        job->waitingForFeed = false;
        fail(job, cancelledError());
    }
    return true;
}

QList<qint64> DownloadManager::pendingIds() const
{
    QList<qint64> ids = m_active.keys();
    for (int i = 0; i < m_queue.size(); ++i) {
        ids.append(m_queue.at(i).episodeId);
    }
    return ids;
}

void DownloadManager::pump()
{
    while (m_active.size() < m_maxParallel && !m_queue.isEmpty()) {
        m_running = true;
        start(m_queue.takeFirst());
    }
    // pump() can be queued several times - report the end only once
    if (!isBusy() && m_running) {
        m_running = false;
        emit finished();
    }
}

void DownloadManager::start(const Queued &queued)
{
    JobPtr job(new Job);
    job->episodeId = queued.episodeId;
    m_active.insert(job->episodeId, job);

    const EpisodeRow episode = m_database->episode(queued.episodeId);
    if (!episode.id) {
        fail(job, QStringLiteral("No episode with id %1").arg(queued.episodeId));
        return;
    }
    if (episode.url.isEmpty()) {
        fail(job, QStringLiteral("The episode has no media URL"));
        return;
    }
    job->podcastId = episode.podcastId;
    job->url = QUrl(episode.url);
    job->target = queued.target.isEmpty() ? defaultTarget(queued.episodeId) : queued.target;
    const Database::FeedState feed = m_database->feedState(episode.podcastId);
    job->authUser = feed.authUser;
    job->authHost = QUrl(feed.url).host();

    if (!QDir().mkpath(QFileInfo(job->target).absolutePath())) {
        fail(job, QStringLiteral("Cannot create the folder %1").arg(QFileInfo(job->target).absolutePath()));
        return;
    }
    job->file = new QFile(job->target + QLatin1String(PartSuffix));
    if (!job->file->open(QIODevice::WriteOnly | QIODevice::Append)) {
        const QString error = job->file->errorString();
        fail(job, QStringLiteral("Cannot write %1: %2").arg(job->file->fileName(), error));
        return;
    }
    job->resumeFrom = job->file->size();
    send(job);
}

void DownloadManager::send(const JobPtr &job)
{
    QNetworkRequest request(job->url);
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    if (job->resumeFrom > 0) {
        request.setRawHeader("Range", "bytes=" + QByteArray::number(job->resumeFrom) + '-');
    }
    if (!job->authUser.isEmpty() && m_passwords.contains(job->podcastId)
            && job->url.host().compare(job->authHost, Qt::CaseInsensitive) == 0) {
        const QByteArray credentials = (job->authUser + QLatin1Char(':')
                                        + m_passwords.value(job->podcastId)).toUtf8();
        request.setRawHeader("Authorization", "Basic " + credentials.toBase64());
    }
    job->status = 0;
    QNetworkReply *reply = m_network->get(request);
    job->reply = reply;

    QTimer *stall = new QTimer(reply);
    stall->setSingleShot(true);
    stall->setInterval(StallTimeoutMs);
    connect(stall, &QTimer::timeout, reply, &QNetworkReply::abort);
    stall->start();

    connect(reply, &QNetworkReply::readyRead, this, [this, job]() { onReadyRead(job); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this, job, stall](qint64 received, qint64 total) {
        stall->start();
        if (job->status == 200 || job->status == 206) {
            job->total = total > 0 ? job->resumeFrom + total : 0;
            emit progress(job->episodeId, job->resumeFrom + received, job->total);
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, job]() { onFinished(job); });
}

void DownloadManager::onReadyRead(const JobPtr &job)
{
    QNetworkReply *reply = job->reply;
    if (!reply) {
        return;
    }
    if (job->status == 0) {
        job->status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (job->status == 200 && job->resumeFrom > 0) {
            // The server ignored the Range header: start from the beginning
            job->file->resize(0);
            job->resumeFrom = 0;
        } else if (job->status == 206 && job->resumeFrom > 0) {
            emit resumed(job->episodeId, job->resumeFrom);
        }
    }
    const QByteArray data = reply->readAll();
    if ((job->status == 200 || job->status == 206) && job->file) {
        if (job->file->write(data) != data.size()) {
            reply->abort();
        }
    }
    // Bodies of redirects and error pages are discarded
}

void DownloadManager::onFinished(const JobPtr &job)
{
    QNetworkReply *reply = job->reply;
    if (!reply) {
        return;
    }
    if (reply->bytesAvailable() > 0) {
        onReadyRead(job);
    }
    job->reply = nullptr;
    reply->deleteLater();
    if (job->cancelled) {
        fail(job, cancelledError());
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!target.isValid() || ++job->redirects > MaxRedirects) {
            fail(job, QStringLiteral("Too many or invalid redirects"));
            return;
        }
        job->url = reply->url().resolved(target);
        send(job);
        return;
    }
    if (status == 416 && job->resumeFrom > 0 && !job->restarted) {
        // The stored part does not fit the file on the server any more
        job->restarted = true;
        job->file->resize(0);
        job->resumeFrom = 0;
        send(job);
        return;
    }
    if (status == 401 || status == 403 || status == 404 || status == 410) {
        if (!job->refreshed && m_updater) {
            // Possibly an expired (signed) link: get the current URL from
            // the feed and try once more
            job->refreshed = true;
            job->waitingForFeed = true;
            m_updater->updatePodcast(job->podcastId);
            return;
        }
        fail(job, status == 401 ? QStringLiteral("Login required or wrong (HTTP 401)")
                                : QStringLiteral("Not available (HTTP %1)").arg(status));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        fail(job, reply->error() == QNetworkReply::OperationCanceledError
             ? QStringLiteral("Timeout or write error - can be resumed")
             : reply->errorString());
        return;
    }
    if (status != 200 && status != 206) {
        fail(job, QStringLiteral("Unexpected answer (HTTP %1)").arg(status));
        return;
    }
    succeed(job);
}

void DownloadManager::onFeedUpdated(qint64 podcastId)
{
    const QList<JobPtr> jobs = m_active.values();
    for (int i = 0; i < jobs.size(); ++i) {
        const JobPtr &job = jobs.at(i);
        if (!job->waitingForFeed || job->podcastId != podcastId) {
            continue;
        }
        job->waitingForFeed = false;
        const EpisodeRow episode = m_database->episode(job->episodeId);
        if (!episode.id || episode.url.isEmpty()) {
            fail(job, QStringLiteral("The episode is no longer in the feed"));
            continue;
        }
        job->url = QUrl(episode.url);
        job->redirects = 0;
        send(job);
    }
}

void DownloadManager::succeed(const JobPtr &job)
{
    const QString part = job->file->fileName();
    closeFile(job);
    if (QFile::exists(job->target) && !QFile::remove(job->target)) {
        fail(job, QStringLiteral("Cannot replace %1").arg(job->target));
        return;
    }
    if (!QFile::rename(part, job->target)) {
        fail(job, QStringLiteral("Cannot rename %1").arg(part));
        return;
    }
    m_database->setDownloaded(job->episodeId, job->target);
    // Out of the active list before the signal (55): the episode page asks
    // isDownloading() while handling it and stayed at "100 %" otherwise
    const JobPtr keep = job;
    done(keep);
    emit downloaded(keep->episodeId, keep->target);
}

void DownloadManager::fail(const JobPtr &job, const QString &error)
{
    // The .part file stays: a later download continues from there - unless
    // the download was cancelled
    QString part;
    if (job->cancelled && job->file) {
        part = job->file->fileName();
    }
    closeFile(job);
    if (!part.isEmpty()) {
        QFile::remove(part);
    }
    const JobPtr keep = job;
    done(keep);
    emit failed(keep->episodeId, error);
}

void DownloadManager::closeFile(const JobPtr &job)
{
    if (job->file) {
        job->file->close();
        delete job->file;
        job->file = nullptr;
    }
}

void DownloadManager::done(const JobPtr &job)
{
    m_active.remove(job->episodeId);
    QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
}

int DownloadManager::removeStaleParts(const QString &archiveRoot, int days)
{
    QSet<QString> running;
    for (QHash<qint64, JobPtr>::const_iterator it = m_active.constBegin(); it != m_active.constEnd(); ++it) {
        if (it.value()) {
            running.insert(QFileInfo(it.value()->target + QLatin1String(PartSuffix)).absoluteFilePath());
        }
    }
    const QDateTime limit = QDateTime::currentDateTime().addDays(-days);
    const QStringList pattern = QStringList() << (QStringLiteral("*") + QLatin1String(PartSuffix));
    int removed = 0;
    const auto removeIfStale = [&](const QFileInfo &info) {
        if (!running.contains(info.absoluteFilePath()) && info.lastModified() < limit
                && QFile::remove(info.absoluteFilePath())) {
            ++removed;
        }
    };

    // The app's own download folder: only Sailcast writes there (Sailjail)
    if (!m_directory.isEmpty() && QDir(m_directory).exists()) {
        QDirIterator it(m_directory, pattern, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            removeIfStale(QFileInfo(it.next()));
        }
    }

    // Music is shared with other apps (their own podcast folders, maybe
    // even the same names): only the half downloads of Sailcast's own
    // files - in the folders of its podcasts, named as its episodes there
    if (!archiveRoot.isEmpty() && QDir(archiveRoot).exists()) {
        const QList<PodcastRow> podcasts = m_database->podcasts();
        for (int p = 0; p < podcasts.size(); ++p) {
            if (podcasts.at(p).archiveFolder.isEmpty()) {
                continue;
            }
            const QDir folder(QDir(archiveRoot).absoluteFilePath(podcasts.at(p).archiveFolder));
            if (!folder.exists()) {
                continue;
            }
            const QFileInfoList parts = folder.entryInfoList(pattern, QDir::Files);
            if (parts.isEmpty()) {
                continue;
            }
            QSet<QString> own;
            const QList<EpisodeRow> episodes = m_database->episodes(podcasts.at(p).id);
            for (int e = 0; e < episodes.size(); ++e) {
                const EpisodeRow &row = episodes.at(e);
                own.insert((row.archiveFile.isEmpty() ? FileNames::forEpisode(row) : row.archiveFile)
                           + QLatin1String(PartSuffix));
            }
            for (int i = 0; i < parts.size(); ++i) {
                if (own.contains(parts.at(i).fileName())) {
                    removeIfStale(parts.at(i));
                }
            }
        }
    }
    return removed;
}

CleanupResult DownloadManager::cleanup(int days, const QString &archiveRoot, qint64 keepEpisodeId,
                                       const QHash<qint64, int> &podcastDays, bool all)
{
    CleanupResult result;
    QSet<qint64> archivedPodcasts;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        if (podcasts.at(i).archived) {
            archivedPodcasts.insert(podcasts.at(i).id);
        }
    }
    const QString root = archiveRoot.isEmpty() ? QString()
                                               : QDir(archiveRoot).absolutePath() + QLatin1Char('/');
    const QString videoRoot = m_videoRoot.isEmpty() ? QString()
                                                    : QDir(m_videoRoot).absolutePath() + QLatin1Char('/');
    const qint64 now = Database::now();

    const QList<EpisodeRow> downloads = m_database->episodes(-1, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        const EpisodeRow &e = downloads.at(i);
        if (m_active.contains(e.id) || e.id == keepEpisodeId) {
            continue;
        }
        QFileInfo info(e.downloadPath);
        if (!info.exists()) {
            m_database->clearDownload(e.id);    // deleted outside the app
            continue;
        }
        // The podcast's own choice (117), else the global one
        const bool own = podcastDays.contains(e.podcastId);
        int keepDays = own ? podcastDays.value(e.podcastId) : days;
        if (all) {
            keepDays = own && podcastDays.value(e.podcastId) < 0 ? -1 : days;
        }
        if (keepDays < 0) {
            continue;
        }
        const qint64 limit = now - static_cast<qint64>(keepDays) * 86400;
        if (!e.played || e.lastPlayed <= 0 || e.lastPlayed > limit
                || archivedPodcasts.contains(e.podcastId)
                // (209) In Music: only files kept or archived (named in the
                // archive) stay; "Downloads in Music" are cleaned up
                || (!root.isEmpty() && info.absoluteFilePath().startsWith(root) && !e.archiveFile.isEmpty())
                || (!videoRoot.isEmpty() && info.absoluteFilePath().startsWith(videoRoot) && !e.archiveFile.isEmpty())) {
            continue;
        }
        const qint64 size = info.size();
        if (QFile::remove(e.downloadPath)) {
            m_database->clearDownload(e.id);
            ++result.files;
            result.bytes += size;
        }
    }
    return result;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DOWNLOADMANAGER_H
#define DOWNLOADMANAGER_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QUrl>

class Database;
class FeedUpdater;
class QFile;
class QNetworkAccessManager;
class QNetworkReply;

struct CleanupResult
{
    int files = 0;
    qint64 bytes = 0;
};

// Downloads episode files.
//
// - Up to maxParallel() downloads at the same time (default 2).
// - Written to "<target>.part" while loading, renamed when complete.
//   An interrupted download continues where it stopped (HTTP Range), even
//   after a restart of the app.
// - Redirects are followed by hand (max. 10); the feed's login is only sent
//   to the host of the feed URL (premium feeds often protect the files too).
// - Expired links: on 401/403/404/410 the feed is updated once and the
//   download retried with the episode's new URL (signed links, e.g. Patreon).
// - 60 seconds without data abort a download (it can be resumed).
class DownloadManager : public QObject
{
    Q_OBJECT
public:
    DownloadManager(Database *database, FeedUpdater *updater, QObject *parent = nullptr);
    ~DownloadManager();

    void setMaxParallel(int count);
    // Folder for normal downloads: <directory>/<podcast>/<episode file>
    void setDownloadDirectory(const QString &directory) { m_directory = directory; }
    QString downloadDirectory() const { return m_directory; }
    void setPassword(qint64 podcastId, const QString &password);

    // Queues an episode. Empty targetPath: the default file in the download
    // directory. Ignored if the episode is already queued or loading.
    void download(qint64 episodeId, const QString &targetPath = QString());
    bool isBusy() const { return !m_queue.isEmpty() || !m_active.isEmpty(); }
    QString defaultTarget(qint64 episodeId) const;

    // Stops a running or queued download and deletes its .part file;
    // failed() follows with CancelledError (the archive counts it as a
    // failure). Returns false if the episode is not being downloaded.
    bool cancel(qint64 episodeId);
    static QString cancelledError() { return QStringLiteral("Cancelled"); }
    // Running downloads first, then the waiting ones (in their order)
    QList<qint64> pendingIds() const;
    bool isActive(qint64 episodeId) const { return m_active.contains(episodeId); }

    // Deletes the downloads of played episodes last played more than `days`
    // days ago (-1 = never) - never in archived podcasts, never files inside
    // `archiveRoot` and never the file of `keepEpisodeId` (the episode in
    // the player). `podcastDays` (117): a podcast's own choice instead of
    // `days` (-1 never, 0 right after listening, N days). `all`: the user
    // asked to delete all played downloads - every podcast as `days`,
    // except those set to "never".
    // (209) Removes ".part" files left by failed downloads that were never
    // tried again: older than `days`, not of a running download - in the
    // download folder and below `archiveRoot`; returns how many
    int removeStaleParts(const QString &archiveRoot, int days = 7);
    // (245) A second root whose kept files (archive name) stay, as in the
    // archive root: ~/Videos/Podcasts for video files
    void setVideoRoot(const QString &root) { m_videoRoot = root; }
    CleanupResult cleanup(int days, const QString &archiveRoot, qint64 keepEpisodeId = 0,
                          const QHash<qint64, int> &podcastDays = QHash<qint64, int>(),
                          bool all = false);

signals:
    void progress(qint64 episodeId, qint64 received, qint64 total);
    // An interrupted download continues at `offset` bytes (server sent 206)
    void resumed(qint64 episodeId, qint64 offset);
    void downloaded(qint64 episodeId, const QString &path);
    void failed(qint64 episodeId, const QString &error);
    void finished();

private slots:
    void pump();

private:
    QString m_videoRoot;
    struct Job
    {
        qint64 episodeId = 0;
        qint64 podcastId = 0;
        QUrl url;
        QString target;
        QString authUser;
        QString authHost;
        int redirects = 0;
        bool refreshed = false;     // feed already updated for this job
        bool restarted = false;     // already restarted without Range (416)
        bool waitingForFeed = false;
        bool cancelled = false;
        qint64 resumeFrom = 0;
        qint64 total = 0;
        int status = 0;
        QFile *file = nullptr;
        QNetworkReply *reply = nullptr;
    };
    typedef QSharedPointer<Job> JobPtr;

    struct Queued
    {
        qint64 episodeId;
        QString target;
    };

    void start(const Queued &queued);
    void send(const JobPtr &job);
    void onReadyRead(const JobPtr &job);
    void onFinished(const JobPtr &job);
    void onFeedUpdated(qint64 podcastId);
    void succeed(const JobPtr &job);
    void fail(const JobPtr &job, const QString &error);
    void closeFile(const JobPtr &job);
    void done(const JobPtr &job);

    Database *m_database;
    FeedUpdater *m_updater;
    QNetworkAccessManager *m_network;
    QString m_directory;
    QList<Queued> m_queue;
    QHash<qint64, JobPtr> m_active;     // by episode id
    QHash<qint64, QString> m_passwords;
    int m_maxParallel = 2;
    bool m_running = false;
};

#endif // DOWNLOADMANAGER_H

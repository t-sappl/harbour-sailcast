// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDUPDATER_H
#define FEEDUPDATER_H

#include <QByteArray>
#include <QHash>
#include <QList>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QThreadPool>
#include <QUrl>

#include "feedtypes.h"

class Database;
class QNetworkAccessManager;
class QNetworkReply;

// Downloads and stores podcast feeds.
//
// - Up to maxParallel() feeds are fetched at the same time.
// - Conditional requests (ETag / Last-Modified): unchanged feeds answer
//   304 and cost neither parsing nor database work.
// - Redirects are followed by hand (max. 5); if every step was permanent
//   (301/308) the stored feed URL is updated, as with itunes:new-feed-url.
// - Parsing runs in a worker thread (QThreadPool), the database is only
//   written in the thread of the updater (main thread).
// - A feed that sends no data for 30 seconds is aborted.
// - Feeds with login: HTTP Basic Auth with the user name from the database
//   and a password set with setPassword(). The credentials are only sent to
//   the host of the stored feed URL, never to a redirect target on another
//   host.
class FeedUpdater : public QObject
{
    Q_OBJECT
public:
    explicit FeedUpdater(Database *database, QObject *parent = nullptr);
    ~FeedUpdater();

    int maxParallel() const { return m_maxParallel; }
    void setMaxParallel(int count);
    bool isBusy() const { return !m_active.isEmpty() || !m_queue.isEmpty(); }

    // Password for a feed with login. Kept in memory only - the app loads
    // it from Sailfish Secrets, the CLI from its own credentials file.
    // An empty password removes it.
    void setPassword(qint64 podcastId, const QString &password);

    // Queues all podcasts that are not paused
    void updateAll();
    // Queues one podcast (ignored if it is already queued or running).
    // forceFull: no conditional request (always the whole feed) and the
    // raw XML is emitted with feedData() - used by the archive.
    void updatePodcast(qint64 podcastId, bool forceFull = false);
    // Adds the podcast (or finds the existing subscription) and updates it.
    // Returns the podcast id, -1 on error.
    qint64 subscribe(const QString &url);

signals:
    void podcastUpdated(qint64 podcastId, int newEpisodes);
    // The server answered 304: the feed has not changed since the last
    // update (emitted before podcastUpdated with 0 new episodes)
    void podcastNotModified(qint64 podcastId);
    // The raw feed XML of a forced update (see updatePodcast)
    void feedData(qint64 podcastId, const QByteArray &xml);
    void podcastFailed(qint64 podcastId, const QString &error);
    // The feed was stored, but its XML broke off: episodes after the
    // broken spot are missing. The warning is also stored as last_error.
    void podcastWarning(qint64 podcastId, const QString &warning);
    void progress(int done, int total);
    void finished();

private slots:
    void onParsed(int jobId);
    void pump();

private:
    struct Job
    {
        int id = 0;
        qint64 podcastId = 0;
        QUrl url;
        QString authUser;
        QString authHost;       // credentials are only sent to this host
        bool sentCredentials = false;
        QString etag;
        QString lastModified;
        int redirects = 0;
        int attempts = 0;       // retries after connection-level errors
        bool forceFull = false;
        bool permanentRedirects = true;
        QByteArray body;
        QString responseEtag;
        QString responseLastModified;
        ParsedFeed parsed;
        QString parseError;
    };
    typedef QSharedPointer<Job> JobPtr;

    void startJob(qint64 podcastId);
    void sendRequest(const JobPtr &job);
    void onReplyFinished(int jobId, QNetworkReply *reply);
    void failJob(const JobPtr &job, const QString &error);
    void finishJob(const JobPtr &job);

    Database *m_database;
    QNetworkAccessManager *m_network;
    QThreadPool m_parsers;
    QList<qint64> m_queue;
    QSet<qint64> m_forceFull;
    QHash<int, JobPtr> m_active;
    QHash<qint64, QString> m_passwords;
    int m_maxParallel = 4;
    int m_nextJobId = 1;
    int m_done = 0;
    int m_total = 0;

    friend class ParseTask;
};

#endif // FEEDUPDATER_H

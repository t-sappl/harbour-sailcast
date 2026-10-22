// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedupdater.h"

#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRunnable>
#include <QTimer>

#include "database.h"
#include "feedparser.h"
#include "netutil.h"

namespace {
const int MaxRedirects = 5;
const int StallTimeoutMs = 30000;
// Connection-level failures are retried after 2 s and 5 s
const int MaxAttempts = 2;

// Errors of the connection, not of the feed. On Sailfish OS (Qt 5.6
// bearer management) "Network session error" shows up for some of the
// many parallel requests at app start, while the network session is being
// set up. These are retried and, if they persist, not stored as the feed's
// error: the feed itself is fine.
bool isConnectionError(QNetworkReply::NetworkError error)
{
    return error == QNetworkReply::NetworkSessionFailedError
            || error == QNetworkReply::TemporaryNetworkFailureError
            || error == QNetworkReply::BackgroundRequestNotAllowedError
            || error == QNetworkReply::UnknownNetworkError;
}
}

// Parses one downloaded feed in a worker thread. Only the job (shared
// pointer, not touched by the main thread meanwhile) and the job id cross
// the thread boundary; the result is picked up in onParsed().
class ParseTask : public QRunnable
{
public:
    ParseTask(FeedUpdater *updater, const QSharedPointer<FeedUpdater::Job> &job)
        : m_updater(updater), m_job(job) {}

    void run() override
    {
        m_job->parsed = FeedParser::parse(m_job->body, &m_job->parseError);
        m_job->body.clear();
        QMetaObject::invokeMethod(m_updater, "onParsed", Qt::QueuedConnection,
                                  Q_ARG(int, m_job->id));
    }

private:
    FeedUpdater *m_updater;
    QSharedPointer<FeedUpdater::Job> m_job;
};

FeedUpdater::FeedUpdater(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
    m_parsers.setMaxThreadCount(2);
}

FeedUpdater::~FeedUpdater()
{
    // Parse tasks post to this object - let them finish first
    m_parsers.waitForDone();
}

void FeedUpdater::setMaxParallel(int count)
{
    m_maxParallel = qBound(1, count, 16);
}

void FeedUpdater::setPassword(qint64 podcastId, const QString &password)
{
    if (password.isEmpty()) {
        m_passwords.remove(podcastId);
    } else {
        m_passwords.insert(podcastId, password);
    }
}

void FeedUpdater::updateAll()
{
    const QList<qint64> ids = m_database->podcastIdsForUpdate();
    for (int i = 0; i < ids.size(); ++i) {
        updatePodcast(ids.at(i));
    }
}

void FeedUpdater::updatePodcast(qint64 podcastId, bool forceFull)
{
    if (forceFull) {
        m_forceFull.insert(podcastId);
    }
    if (m_queue.contains(podcastId)) {
        return;
    }
    for (QHash<int, JobPtr>::const_iterator it = m_active.constBegin(); it != m_active.constEnd(); ++it) {
        if (it.value()->podcastId == podcastId) {
            return;
        }
    }
    m_queue.append(podcastId);
    ++m_total;
    emit progress(m_done, m_total);
    // Started from the event loop, so callers can queue several podcasts
    // and connect to the signals first
    QMetaObject::invokeMethod(this, "pump", Qt::QueuedConnection);
}

qint64 FeedUpdater::subscribe(const QString &url)
{
    const qint64 id = m_database->addPodcast(url);
    if (id >= 0) {
        updatePodcast(id);
    }
    return id;
}

void FeedUpdater::pump()
{
    while (m_active.size() < m_maxParallel && !m_queue.isEmpty()) {
        startJob(m_queue.takeFirst());
    }
}

void FeedUpdater::startJob(qint64 podcastId)
{
    const Database::FeedState state = m_database->feedState(podcastId);
    JobPtr job(new Job);
    job->id = m_nextJobId++;
    job->podcastId = podcastId;
    job->url = QUrl(state.url);
    job->etag = state.etag;
    job->lastModified = state.lastModified;
    job->authUser = state.authUser;
    job->forceFull = m_forceFull.remove(podcastId);
    job->authHost = job->url.host();
    m_active.insert(job->id, job);

    if (!job->url.isValid() || job->url.scheme().isEmpty()) {
        failJob(job, QStringLiteral("Invalid feed URL: %1").arg(state.url));
        return;
    }
    sendRequest(job);
}

void FeedUpdater::sendRequest(const JobPtr &job)
{
    QNetworkRequest request(job->url);
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    // Conditional request only for the stored URL, not after a redirect
    if (job->redirects == 0 && !job->forceFull) {
        if (!job->etag.isEmpty()) {
            request.setRawHeader("If-None-Match", job->etag.toUtf8());
        }
        if (!job->lastModified.isEmpty()) {
            request.setRawHeader("If-Modified-Since", job->lastModified.toUtf8());
        }
    }

    job->sentCredentials = false;
    if (!job->authUser.isEmpty() && m_passwords.contains(job->podcastId)
            && job->url.host().compare(job->authHost, Qt::CaseInsensitive) == 0) {
        const QByteArray credentials = (job->authUser + QLatin1Char(':')
                                        + m_passwords.value(job->podcastId)).toUtf8();
        request.setRawHeader("Authorization", "Basic " + credentials.toBase64());
        job->sentCredentials = true;
    }

    QNetworkReply *reply = m_network->get(request);
    const int jobId = job->id;

    // Abort when nothing arrives for a while (Qt 5.6 has no transfer timeout)
    QTimer *stall = new QTimer(reply);
    stall->setSingleShot(true);
    stall->setInterval(StallTimeoutMs);
    connect(stall, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::downloadProgress, stall, [stall](qint64, qint64) { stall->start(); });
    stall->start();

    connect(reply, &QNetworkReply::finished, this, [this, jobId, reply]() {
        onReplyFinished(jobId, reply);
    });
}

void FeedUpdater::onReplyFinished(int jobId, QNetworkReply *reply)
{
    reply->deleteLater();
    const JobPtr job = m_active.value(jobId);
    if (!job) {
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

    if (status == 301 || status == 302 || status == 303 || status == 307 || status == 308) {
        const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
        if (!target.isValid() || ++job->redirects > MaxRedirects) {
            failJob(job, QStringLiteral("Too many or invalid redirects"));
            return;
        }
        if (status != 301 && status != 308) {
            job->permanentRedirects = false;
        }
        job->url = reply->url().resolved(target);
        sendRequest(job);
        return;
    }
    if (status == 304) {
        m_database->markNotModified(job->podcastId);
        emit podcastNotModified(job->podcastId);
        emit podcastUpdated(job->podcastId, 0);
        finishJob(job);
        return;
    }
    if (status == 401 || status == 403) {
        QString error;
        if (job->sentCredentials) {
            error = QStringLiteral("Wrong user name or password (HTTP %1)").arg(status);
        } else if (!job->authUser.isEmpty()) {
            error = QStringLiteral("Password missing (HTTP %1)").arg(status);
        } else if (status == 401) {
            error = QStringLiteral("Login required (HTTP 401) - set user name and password");
        } else {
            error = QStringLiteral("Access denied (HTTP 403)");
        }
        failJob(job, error);
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        if (isConnectionError(reply->error())) {
            if (job->attempts < MaxAttempts) {
                ++job->attempts;
                const int delayMs = job->attempts == 1 ? 2000 : 5000;
                QTimer::singleShot(delayMs, this, [this, job]() {
                    if (m_active.contains(job->id)) {
                        sendRequest(job);
                    }
                });
                return;
            }
            // Counted as failed, but the feed keeps its stored state
            emit podcastFailed(job->podcastId, reply->errorString());
            finishJob(job);
            return;
        }
        const QString error = reply->error() == QNetworkReply::OperationCanceledError
                ? QStringLiteral("Timeout") : reply->errorString();
        failJob(job, error);
        return;
    }

    job->body = reply->readAll();
    if (job->forceFull) {
        emit feedData(job->podcastId, job->body);
    }
    job->responseEtag = QString::fromLatin1(reply->rawHeader("ETag"));
    job->responseLastModified = QString::fromLatin1(reply->rawHeader("Last-Modified"));
    m_parsers.start(new ParseTask(this, job));   // the pool deletes the task
}

void FeedUpdater::onParsed(int jobId)
{
    const JobPtr job = m_active.value(jobId);
    if (!job) {
        return;
    }
    if (job->parsed.isEmpty()) {
        failJob(job, job->parseError.isEmpty() ? QStringLiteral("Not a podcast feed")
                                               : job->parseError);
        return;
    }

    // Moved feed: permanent redirects, or the feed itself names a new URL
    QString newUrl;
    if (job->redirects > 0 && job->permanentRedirects) {
        newUrl = job->url.toString();
    }
    if (!job->parsed.newUrl.isEmpty()) {
        newUrl = job->parsed.newUrl.trimmed();
    }
    QString etag = job->responseEtag;
    QString lastModified = job->responseLastModified;
    if (!newUrl.isEmpty() && newUrl != m_database->feedState(job->podcastId).url) {
        if (m_database->changeUrl(job->podcastId, newUrl)) {
            // Validators belong to the old URL when itunes:new-feed-url moved it
            if (newUrl != job->url.toString()) {
                etag.clear();
                lastModified.clear();
            }
        }
    }

    const int newEpisodes = m_database->applyFeed(job->podcastId, job->parsed, etag, lastModified);
    if (newEpisodes < 0) {
        failJob(job, m_database->lastError());
        return;
    }
    emit podcastUpdated(job->podcastId, newEpisodes);
    if (!job->parseError.isEmpty()) {
        // Partial result (applyFeed cleared last_error, so set it afterwards)
        const QString warning = QStringLiteral("%1 - only %2 episodes read")
                .arg(job->parseError).arg(job->parsed.episodes.size());
        m_database->setUpdateError(job->podcastId, warning);
        emit podcastWarning(job->podcastId, warning);
    }
    finishJob(job);
}

void FeedUpdater::failJob(const JobPtr &job, const QString &error)
{
    m_database->setUpdateError(job->podcastId, error);
    emit podcastFailed(job->podcastId, error);
    finishJob(job);
}

void FeedUpdater::finishJob(const JobPtr &job)
{
    m_active.remove(job->id);
    ++m_done;
    emit progress(m_done, m_total);
    pump();
    if (m_active.isEmpty() && m_queue.isEmpty()) {
        m_done = 0;
        m_total = 0;
        emit finished();
    }
}

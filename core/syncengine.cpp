// SPDX-License-Identifier: GPL-3.0-or-later
#include "syncengine.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSet>
#include <QTimer>
#include <QUrlQuery>
#include <QVariant>

#include "feedupdater.h"
#include "netutil.h"

namespace {

const int StallTimeoutMs = 60000;
const int UploadBatchSize = 100;
const int MaxUploadRounds = 200;    // safety net against an endless loop

const char NextcloudPrefix[] = "/index.php/apps/gpoddersync/";

// The other scheme of a URL (http <-> https), empty if neither
QString otherScheme(const QString &url)
{
    if (url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return QStringLiteral("http://") + url.mid(8);
    }
    if (url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
        return QStringLiteral("https://") + url.mid(7);
    }
    return QString();
}

// Feed URLs are compared like Database::findPodcast(): http and https and
// upper/lower case count as the same feed. The set must hold lower case URLs
// (see toSet()).
bool containsFeed(const QSet<QString> &urls, const QString &url)
{
    const QString key = url.toLower();
    if (urls.contains(key)) {
        return true;
    }
    const QString other = otherScheme(key);
    return !other.isEmpty() && urls.contains(other);
}

// QList::toSet() is deprecated from Qt 5.14 on, the range constructor
// does not exist in Qt 5.6
// (lower case, for containsFeed())
QSet<QString> toSet(const QStringList &list)
{
    QSet<QString> set;
    for (int i = 0; i < list.size(); ++i) {
        set.insert(list.at(i).toLower());
    }
    return set;
}

QStringList stringList(const QJsonValue &value)
{
    QStringList result;
    const QJsonArray array = value.toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QString text = array.at(i).toString().trimmed();
        if (!text.isEmpty()) {
            result.append(text);
        }
    }
    return result;
}

// Integers sometimes arrive as strings; missing values become -1
int intValue(const QJsonObject &object, const QString &key)
{
    const QJsonValue value = object.value(key);
    if (value.isUndefined() || value.isNull()) {
        return -1;
    }
    bool ok = false;
    const int number = value.toVariant().toInt(&ok);
    return ok ? number : -1;
}

EpisodeAction actionFromJson(const QJsonObject &object)
{
    EpisodeAction a;
    a.podcastUrl = object.value(QStringLiteral("podcast")).toString().trimmed();
    a.episodeUrl = object.value(QStringLiteral("episode")).toString().trimmed();
    a.guid = object.value(QStringLiteral("guid")).toString().trimmed();
    a.action = object.value(QStringLiteral("action")).toString().trimmed().toLower();
    const QJsonValue timestamp = object.value(QStringLiteral("timestamp"));
    a.timestamp = timestamp.isDouble() ? static_cast<qint64>(timestamp.toDouble())
                                       : SyncEngine::parseTimestamp(timestamp.toString());
    a.started = intValue(object, QStringLiteral("started"));
    a.position = intValue(object, QStringLiteral("position"));
    a.total = intValue(object, QStringLiteral("total"));
    return a;
}

QJsonObject actionToJson(const EpisodeAction &a, const QString &device)
{
    QJsonObject object;
    object.insert(QStringLiteral("podcast"), a.podcastUrl);
    object.insert(QStringLiteral("episode"), a.episodeUrl);
    if (!a.guid.isEmpty()) {
        object.insert(QStringLiteral("guid"), a.guid);
    }
    if (!device.isEmpty()) {
        object.insert(QStringLiteral("device"), device);
    }
    object.insert(QStringLiteral("action"), a.action);
    object.insert(QStringLiteral("timestamp"), SyncEngine::formatTimestamp(a.timestamp));
    if (a.action == QLatin1String("play") && a.position >= 0) {
        object.insert(QStringLiteral("started"), qMax(0, a.started));
        object.insert(QStringLiteral("position"), a.position);
        if (a.total > 0) {
            object.insert(QStringLiteral("total"), a.total);
        }
    }
    return object;
}

} // namespace

SyncEngine::SyncEngine(Database *database, FeedUpdater *updater, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_updater(updater)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

bool SyncEngine::backendFromName(const QString &name, Backend *backend)
{
    const QString key = name.trimmed().toLower();
    if (key == QLatin1String("nextcloud")) {
        *backend = Nextcloud;
        return true;
    }
    if (key == QLatin1String("gpodder")) {
        *backend = GpodderApi;
        return true;
    }
    return false;
}

QString SyncEngine::backendName(Backend backend)
{
    return backend == Nextcloud ? QStringLiteral("nextcloud") : QStringLiteral("gpodder");
}

QString SyncEngine::formatTimestamp(qint64 seconds)
{
    return QDateTime::fromMSecsSinceEpoch(seconds * 1000, Qt::UTC)
            .toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
}

qint64 SyncEngine::parseTimestamp(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }
    bool isNumber = false;
    const qint64 number = trimmed.toLongLong(&isNumber);
    if (isNumber) {
        return number;
    }
    QDateTime dt = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!dt.isValid()) {
        QString noFraction = trimmed;
        noFraction.remove(QRegularExpression(QStringLiteral("\\.\\d+")));
        dt = QDateTime::fromString(noFraction, Qt::ISODate);
    }
    if (!dt.isValid()) {
        return 0;
    }
    // Without zone the API means UTC (Qt would read it as local time)
    if (dt.timeSpec() == Qt::LocalTime) {
        dt.setTimeSpec(Qt::UTC);
    }
    return dt.toMSecsSinceEpoch() / 1000;
}

// --- Flow ---------------------------------------------------------------------

void SyncEngine::start()
{
    if (m_running) {
        return;
    }
    if (!backendFromName(m_database->setting(QStringLiteral("sync.backend")), &m_backend)) {
        emit failed(QStringLiteral("No sync account configured"));
        return;
    }
    m_server = m_database->setting(QStringLiteral("sync.server")).trimmed();
    while (m_server.endsWith(QLatin1Char('/'))) {
        m_server.chop(1);
    }
    m_user = m_database->setting(QStringLiteral("sync.user"));
    m_device = m_database->setting(QStringLiteral("sync.device"), QStringLiteral("sailcast"));
    if (m_server.isEmpty() || m_user.isEmpty()) {
        emit failed(QStringLiteral("Sync account incomplete (server or user missing)"));
        return;
    }
    if (m_password.isEmpty()) {
        emit failed(QStringLiteral("Sync password missing"));
        return;
    }

    m_running = true;
    m_result = SyncResult();
    m_newPodcasts.clear();
    m_uploadedAdds.clear();
    m_uploadedRemoves.clear();
    m_uploadRounds = 0;
    m_startedMs = QDateTime::currentMSecsSinceEpoch();
    m_firstSync = m_database->setting(QStringLiteral("sync.subs_since")).isEmpty();
    m_result.firstSync = m_firstSync;

    // A device chosen from the account's list (51) exists already - posting
    // it again would rename it to "Sailcast"
    if (m_backend == GpodderApi && m_database->setting(QStringLiteral("sync.deviceExisting")) != QLatin1String("1")) {
        registerDevice();
    } else if (m_backend == GpodderApi) {
        fetchSubscriptions();
    } else {
        fetchSubscriptions();
    }
}

void SyncEngine::registerDevice()
{
    emit progress(QStringLiteral("Registering device"));
    QJsonObject device;
    device.insert(QStringLiteral("caption"), QStringLiteral("Sailcast"));
    device.insert(QStringLiteral("type"), QStringLiteral("mobile"));
    post(endpoint(QStringLiteral("devices/%1/%2.json").arg(userPath(), m_device)),
         QJsonDocument(device).toJson(QJsonDocument::Compact),
         [this](const QByteArray &) { fetchSubscriptions(); });
}

void SyncEngine::fetchSubscriptions()
{
    emit progress(QStringLiteral("Getting subscriptions"));
    const qint64 since = m_database->setting(QStringLiteral("sync.subs_since"), QStringLiteral("0")).toLongLong();
    const QString path = m_backend == Nextcloud
            ? QStringLiteral("subscriptions")
            : QStringLiteral("subscriptions/%1/%2.json").arg(userPath(), m_device);
    get(endpoint(path, since), [this](const QByteArray &body) { applySubscriptions(body); });
}

void SyncEngine::applySubscriptions(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    if (!object.contains(QStringLiteral("timestamp"))) {
        fail(QStringLiteral("Unexpected answer to the subscription request"));
        return;
    }
    const QStringList adds = stringList(object.value(QStringLiteral("add")));
    const QStringList removes = stringList(object.value(QStringLiteral("remove")));

    // Local changes not uploaded yet win over the server's older state
    const QSet<QString> localAdds = toSet(m_database->outboxSubscriptions(QStringLiteral("add")));
    const QSet<QString> localRemoves = toSet(m_database->outboxSubscriptions(QStringLiteral("remove")));

    for (int i = 0; i < adds.size(); ++i) {
        const QString &url = adds.at(i);
        if (containsFeed(localRemoves, url) || m_database->findPodcast(url) >= 0) {
            continue;
        }
        const qint64 id = m_database->addPodcast(url, QString(), false);
        if (id >= 0) {
            m_newPodcasts.append(id);
            ++m_result.podcastsAdded;
        }
    }
    if (!m_firstSync) {
        for (int i = 0; i < removes.size(); ++i) {
            const QString &url = removes.at(i);
            if (containsFeed(localAdds, url)) {
                continue;
            }
            const qint64 id = m_database->findPodcast(url);
            if (id >= 0 && m_database->removePodcast(id, false)) {
                ++m_result.podcastsRemoved;
            }
        }
    }

    // First sync: everything subscribed here but missing on the server is
    // uploaded (the union of both lists)
    if (m_firstSync) {
        const QSet<QString> server = toSet(adds);
        const QStringList local = m_database->podcastUrls();
        for (int i = 0; i < local.size(); ++i) {
            if (!containsFeed(server, local.at(i))) {
                m_uploadedAdds.append(local.at(i));
            }
        }
        // The outbox is covered by the union
        m_database->removeOutboxSubscriptions(m_database->outboxSubscriptions(QStringLiteral("add")));
        m_database->removeOutboxSubscriptions(m_database->outboxSubscriptions(QStringLiteral("remove")));
    }

    m_database->setSetting(QStringLiteral("sync.subs_since"),
                           QString::number(object.value(QStringLiteral("timestamp")).toVariant().toLongLong()));
    uploadSubscriptions();
}

void SyncEngine::uploadSubscriptions()
{
    if (!m_firstSync) {
        m_uploadedAdds = m_database->outboxSubscriptions(QStringLiteral("add"));
        m_uploadedRemoves = m_database->outboxSubscriptions(QStringLiteral("remove"));
    }
    if (m_uploadedAdds.isEmpty() && m_uploadedRemoves.isEmpty()) {
        loadNewFeeds();
        return;
    }
    emit progress(QStringLiteral("Uploading subscription changes"));
    QJsonObject changes;
    changes.insert(QStringLiteral("add"), QJsonArray::fromStringList(m_uploadedAdds));
    changes.insert(QStringLiteral("remove"), QJsonArray::fromStringList(m_uploadedRemoves));
    const QString path = m_backend == Nextcloud
            ? QStringLiteral("subscription_change/create")
            : QStringLiteral("subscriptions/%1/%2.json").arg(userPath(), m_device);
    post(endpoint(path), QJsonDocument(changes).toJson(QJsonDocument::Compact),
         [this](const QByteArray &) {
        m_result.subscriptionsUploaded = m_uploadedAdds.size() + m_uploadedRemoves.size();
        if (!m_firstSync) {
            m_database->removeOutboxSubscriptions(m_uploadedAdds + m_uploadedRemoves);
        }
        loadNewFeeds();
    });
}

void SyncEngine::loadNewFeeds()
{
    if (m_newPodcasts.isEmpty() || !m_updater) {
        fetchActions();
        return;
    }
    emit progress(QStringLiteral("Loading %1 new podcasts").arg(m_newPodcasts.size()));
    m_result.feedsLoaded = m_newPodcasts.size();
    m_updaterConnection = connect(m_updater, &FeedUpdater::finished, this, [this]() {
        disconnect(m_updaterConnection);
        fetchActions();
    });
    for (int i = 0; i < m_newPodcasts.size(); ++i) {
        m_updater->updatePodcast(m_newPodcasts.at(i));
    }
}

void SyncEngine::fetchActions()
{
    emit progress(QStringLiteral("Getting episode actions"));
    const qint64 since = m_database->setting(QStringLiteral("sync.actions_since"), QStringLiteral("0")).toLongLong();
    const QString path = m_backend == Nextcloud
            ? QStringLiteral("episode_action")
            : QStringLiteral("episodes/%1.json").arg(userPath());
    get(endpoint(path, since), [this](const QByteArray &body) { applyActions(body); });
}

void SyncEngine::applyActions(const QByteArray &body)
{
    const QJsonObject object = QJsonDocument::fromJson(body).object();
    if (!object.contains(QStringLiteral("actions"))) {
        fail(QStringLiteral("Unexpected answer to the episode action request"));
        return;
    }
    QList<EpisodeAction> actions;
    const QJsonArray array = object.value(QStringLiteral("actions")).toArray();
    for (int i = 0; i < array.size(); ++i) {
        const EpisodeAction action = actionFromJson(array.at(i).toObject());
        if (!action.action.isEmpty() && (!action.episodeUrl.isEmpty() || !action.guid.isEmpty())) {
            actions.append(action);
        }
    }
    m_result.actionsReceived = actions.size();
    m_database->addPendingActions(actions);
    if (object.contains(QStringLiteral("timestamp"))) {
        m_database->setSetting(QStringLiteral("sync.actions_since"),
                               QString::number(object.value(QStringLiteral("timestamp")).toVariant().toLongLong()));
    }
    emit progress(QStringLiteral("Applying %1 episode actions").arg(actions.size()));
    m_result.applied = m_database->applyPendingActions();
    // Actions that have waited 90 days for their episode will not find it
    // any more (episode no longer in the feed) - drop them (49)
    m_database->prunePendingActions(90);

    // First sync: local states are not pushed over the server's
    if (m_firstSync) {
        QList<qint64> ids;
        QList<EpisodeAction> outbox = m_database->outboxActions(1000000);
        for (int i = 0; i < outbox.size(); ++i) {
            ids.append(outbox.at(i).id);
        }
        m_database->removeOutboxActions(ids);
    }
    uploadActions();
}

void SyncEngine::uploadActions()
{
    const QList<EpisodeAction> batch = m_database->outboxActions(UploadBatchSize);
    if (batch.isEmpty() || ++m_uploadRounds > MaxUploadRounds) {
        finish();
        return;
    }
    emit progress(QStringLiteral("Uploading episode actions"));
    QJsonArray array;
    QList<qint64> ids;
    const QString device = m_backend == GpodderApi ? m_device : QString();
    for (int i = 0; i < batch.size(); ++i) {
        array.append(actionToJson(batch.at(i), device));
        ids.append(batch.at(i).id);
    }
    const QString path = m_backend == Nextcloud
            ? QStringLiteral("episode_action/create")
            : QStringLiteral("episodes/%1.json").arg(userPath());
    post(endpoint(path), QJsonDocument(array).toJson(QJsonDocument::Compact),
         [this, ids](const QByteArray &) {
        m_database->removeOutboxActions(ids);
        m_result.actionsUploaded += ids.size();
        uploadActions();
    });
}

void SyncEngine::finish()
{
    m_database->setSetting(QStringLiteral("sync.last_sync"), QString::number(Database::now()));
    m_result.elapsedMs = QDateTime::currentMSecsSinceEpoch() - m_startedMs;
    m_running = false;
    emit finished(m_result);
}

void SyncEngine::fail(const QString &error)
{
    if (m_updaterConnection) {
        disconnect(m_updaterConnection);
    }
    m_running = false;
    emit failed(error);
}

// --- HTTP -------------------------------------------------------------------

QString SyncEngine::userPath() const
{
    return QString::fromLatin1(QUrl::toPercentEncoding(m_user));
}

QUrl SyncEngine::endpoint(const QString &path, qint64 since) const
{
    QString base = m_server;
    if (m_backend == Nextcloud) {
        base += QLatin1String(NextcloudPrefix);
    } else {
        base += QStringLiteral("/api/2/");
    }
    QUrl url(base + path);
    if (since >= 0) {
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("since"), QString::number(since));
        url.setQuery(query);
    }
    return url;
}

void SyncEngine::get(const QUrl &url, const Handler &onSuccess)
{
    send(QByteArrayLiteral("GET"), url, QByteArray(), onSuccess);
}

void SyncEngine::post(const QUrl &url, const QByteArray &json, const Handler &onSuccess)
{
    send(QByteArrayLiteral("POST"), url, json, onSuccess);
}

void SyncEngine::send(const QByteArray &verb, const QUrl &url, const QByteArray &json,
                      const Handler &onSuccess)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setRawHeader("Accept", "application/json");
    const QByteArray credentials = (m_user + QLatin1Char(':') + m_password).toUtf8();
    request.setRawHeader("Authorization", "Basic " + credentials.toBase64());

    QNetworkReply *reply = nullptr;
    if (verb == "POST") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        reply = m_network->post(request, json);
    } else {
        reply = m_network->get(request);
    }

    QTimer *stall = new QTimer(reply);
    stall->setSingleShot(true);
    stall->setInterval(StallTimeoutMs);
    connect(stall, &QTimer::timeout, reply, &QNetworkReply::abort);
    connect(reply, &QNetworkReply::downloadProgress, stall, [stall](qint64, qint64) { stall->start(); });
    connect(reply, &QNetworkReply::uploadProgress, stall, [stall](qint64, qint64) { stall->start(); });
    stall->start();

    connect(reply, &QNetworkReply::finished, this, [this, reply, onSuccess]() {
        reply->deleteLater();
        if (!m_running) {
            return;
        }
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 401 || status == 403) {
            fail(QStringLiteral("Login failed (HTTP %1) - check user name and password%2")
                 .arg(status)
                 .arg(m_backend == Nextcloud ? QStringLiteral(" (use an app password)") : QString()));
            return;
        }
        if (status == 404 && m_backend == Nextcloud) {
            fail(QStringLiteral("Not found (HTTP 404) - is the Nextcloud app \"GPodder Sync\" enabled?"
                                " Server address: %1").arg(m_server));
            return;
        }
        if (status >= 300 && status < 400) {
            const QUrl target = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
            fail(QStringLiteral("The server redirects (HTTP %1) to %2 - check the server address")
                 .arg(status).arg(reply->url().resolved(target).toString()));
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            fail(reply->error() == QNetworkReply::OperationCanceledError
                 ? QStringLiteral("Timeout (%1)").arg(reply->url().path())
                 : reply->errorString());
            return;
        }
        onSuccess(reply->readAll());
    });
}

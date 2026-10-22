// SPDX-License-Identifier: GPL-3.0-or-later
#include "podcastindex.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QtGlobal>
#include <QUrl>
#include <QUrlQuery>

#include "netutil.h"

namespace {
const char ApiBase[] = "https://api.podcastindex.org/api/1.0/";
const int TimeoutMs = 30000;
}

PodcastIndex::PodcastIndex(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_key(builtInKey())
    , m_secret(builtInSecret())
{
    NetworkAccess::keepAccessible(m_network);
}

QString PodcastIndex::builtInKey()
{
#ifdef PODCASTINDEX_KEY
    return QStringLiteral(PODCASTINDEX_KEY);
#else
    return QString();
#endif
}

QString PodcastIndex::builtInSecret()
{
#if defined(PODCASTINDEX_SECRET_HEX)
    // Hex-encoded (38): secrets contain characters such as # and $ that
    // qmake (comment), make and the shell would change on the way
    return QString::fromUtf8(QByteArray::fromHex(QByteArrayLiteral(PODCASTINDEX_SECRET_HEX)));
#elif defined(PODCASTINDEX_SECRET)
    return QStringLiteral(PODCASTINDEX_SECRET);
#else
    return QString();
#endif
}

void PodcastIndex::setCredentials(const QString &key, const QString &secret)
{
    m_key = key.trimmed();
    m_secret = secret.trimmed();
}

void PodcastIndex::search(const QString &term, int max)
{
    if (m_reply) {
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    const QString text = term.trimmed();
    if (!isAvailable()) {
        QTimer::singleShot(0, this, [this, text]() {
            emit searchFailed(text, QStringLiteral("no API key built in"));
        });
        return;
    }
    m_retried = false;
    send(text, max);
}

void PodcastIndex::send(const QString &text, int max)
{
    QUrl url(QString::fromLatin1(ApiBase) + QStringLiteral("search/byterm"));
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("q"), text);
    query.addQueryItem(QStringLiteral("max"), QString::number(max));
    url.setQuery(query);

    const QByteArray date = QByteArray::number(QDateTime::currentMSecsSinceEpoch() / 1000);
    const QByteArray hash = QCryptographicHash::hash(m_key.toUtf8() + m_secret.toUtf8() + date,
                                                     QCryptographicHash::Sha1).toHex();
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setRawHeader("X-Auth-Key", m_key.toUtf8());
    request.setRawHeader("X-Auth-Date", date);
    request.setRawHeader("Authorization", hash);

    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    QTimer::singleShot(TimeoutMs, reply, SLOT(abort()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, text, max]() { onFinished(reply, text, max); });
}

void PodcastIndex::onFinished(QNetworkReply *reply, const QString &term, int max)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        emit searchFailed(term, QStringLiteral("API key rejected (HTTP %1)").arg(status));
        return;
    }
    const QByteArray body = reply->readAll();
    if (reply->error() != QNetworkReply::NoError || status >= 400) {
        // A server error (5xx, 429) or a broken connection: once more after
        // two seconds before reporting it (59: a single "server replied:
        // <none>" was shown)
        const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError;
        const bool retryable = !timedOut && (status == 0 || status == 429 || status >= 500);
        if (retryable && !m_retried) {
            m_retried = true;
            QTimer::singleShot(2000, this, [this, term, max]() {
                if (!m_reply) {
                    send(term, max);
                }
            });
            return;
        }
        // Readable message instead of Qt's "Error transferring <url> -
        // server replied: …": the HTTP status and the API's own text
        // For the log (61): what Podcast Index or Cloudflare answered
        qWarning("Podcast Index: HTTP %d, error %d, server %s, cf-ray %s, body %s", status, int(reply->error()),
                 reply->rawHeader("Server").constData(), reply->rawHeader("CF-RAY").constData(),
                 body.left(300).constData());
        QString message;
        if (timedOut) {
            message = QStringLiteral("no answer from Podcast Index (timeout)");
        } else if (status > 0) {
            const QString description = QJsonDocument::fromJson(body).object()
                    .value(QStringLiteral("description")).toString().trimmed();
            message = QStringLiteral("Podcast Index answered HTTP %1").arg(status);
            if (!description.isEmpty()) {
                message += QStringLiteral(" (%1)").arg(description.left(120));
            }
        } else {
            message = QStringLiteral("no connection to Podcast Index (network error %1)").arg(int(reply->error()));
        }
        emit searchFailed(term, message);
        return;
    }
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    const QJsonArray feeds = root.value(QStringLiteral("feeds")).toArray();
    QList<PodcastSearchResult> results;
    for (int i = 0; i < feeds.size(); ++i) {
        const QJsonObject f = feeds.at(i).toObject();
        PodcastSearchResult r;
        r.id = qint64(f.value(QStringLiteral("id")).toDouble());
        r.title = f.value(QStringLiteral("title")).toString();
        r.author = f.value(QStringLiteral("author")).toString();
        r.url = f.value(QStringLiteral("url")).toString();
        r.imageUrl = f.value(QStringLiteral("artwork")).toString();
        if (r.imageUrl.isEmpty()) {
            r.imageUrl = f.value(QStringLiteral("image")).toString();
        }
        r.description = f.value(QStringLiteral("description")).toString();
        r.language = f.value(QStringLiteral("language")).toString();
        r.episodeCount = f.value(QStringLiteral("episodeCount")).toInt();
        r.link = f.value(QStringLiteral("link")).toString();
        r.ownerName = f.value(QStringLiteral("ownerName")).toString();
        // "categories": {"77": "Society", "78": "Culture"} (may be null)
        const QJsonObject categories = f.value(QStringLiteral("categories")).toObject();
        for (QJsonObject::const_iterator it = categories.constBegin(); it != categories.constEnd(); ++it) {
            const QString name = it.value().toString();
            if (!name.isEmpty() && !r.categories.contains(name)) {
                r.categories.append(name);
            }
        }
        r.lastUpdate = qint64(f.value(QStringLiteral("lastUpdateTime")).toDouble());
        r.explicitContent = f.value(QStringLiteral("explicit")).toBool();
        const QJsonObject funding = f.value(QStringLiteral("funding")).toObject();
        r.fundingUrl = funding.value(QStringLiteral("url")).toString();
        r.fundingMessage = funding.value(QStringLiteral("message")).toString();
        if (!r.url.isEmpty()) {
            results.append(r);
        }
    }
    emit searchFinished(term, results);
}

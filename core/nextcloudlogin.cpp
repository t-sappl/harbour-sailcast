// SPDX-License-Identifier: GPL-3.0-or-later
#include "nextcloudlogin.h"
#include "netutil.h"

#include <QDateTime>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>

static const int PollIntervalMs = 2000;
static const qint64 TimeoutMs = 20 * 60 * 1000;

NextcloudLogin::NextcloudLogin(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
    , m_timer(new QTimer(this))
{
    NetworkAccess::keepAccessible(m_network);
    m_timer->setSingleShot(true);
    m_timer->setInterval(PollIntervalMs);
    connect(m_timer, &QTimer::timeout, this, &NextcloudLogin::poll);
}

void NextcloudLogin::start(const QString &server)
{
    cancel();
    QString base = server.trimmed();
    if (!base.contains(QStringLiteral("://"))) {
        base.prepend(QStringLiteral("https://"));
    }
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    const QUrl url(base + QStringLiteral("/index.php/login/v2"));
    if (!url.isValid() || url.host().isEmpty()) {
        emit failed(InvalidAddress, QString());
        return;
    }
    m_active = true;
    emit activeChanged();
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", "Sailcast (Sailfish OS)");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    m_reply = m_network->post(request, QByteArray());
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply != m_reply) {
            return;
        }
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (reply->error() != QNetworkReply::NoError || status != 200) {
            stop();
            if (status == 404) {
                emit failed(NoNextcloud, QString());
            } else {
                // With the address (62): "Connection refused" alone did not say where
                emit failed(ServerError, (status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString())
                            + QStringLiteral(" (%1)").arg(reply->url().toString(QUrl::RemoveQuery)));
            }
            return;
        }
        const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
        const QJsonObject pollObject = root.value(QStringLiteral("poll")).toObject();
        m_token = pollObject.value(QStringLiteral("token")).toString().toUtf8();
        m_endpoint = QUrl(pollObject.value(QStringLiteral("endpoint")).toString());
        const QString login = root.value(QStringLiteral("login")).toString();
        if (m_token.isEmpty() || !m_endpoint.isValid() || login.isEmpty()) {
            stop();
            emit failed(NotStarted, QString());
            return;
        }
        m_deadline = QDateTime::currentMSecsSinceEpoch() + TimeoutMs;
        emit loginUrl(login);
        m_timer->start();
    });
}

void NextcloudLogin::poll()
{
    if (!m_active) {
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() > m_deadline) {
        stop();
        emit failed(TimedOut, QString());
        return;
    }
    QNetworkRequest request(m_endpoint);
    request.setRawHeader("User-Agent", "Sailcast (Sailfish OS)");
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/x-www-form-urlencoded"));
    m_reply = m_network->post(request, QByteArray("token=") + QUrl::toPercentEncoding(QString::fromUtf8(m_token)));
    QNetworkReply *reply = m_reply;
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply != m_reply) {
            return;
        }
        m_reply = nullptr;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            const QJsonObject root = QJsonDocument::fromJson(reply->readAll()).object();
            const QString server = root.value(QStringLiteral("server")).toString();
            const QString user = root.value(QStringLiteral("loginName")).toString();
            const QString password = root.value(QStringLiteral("appPassword")).toString();
            stop();
            if (server.isEmpty() || user.isEmpty() || password.isEmpty()) {
                emit failed(Incomplete, QString());
            } else {
                emit finished(server, user, password);
            }
            return;
        }
        // 404: not confirmed yet; network hiccups: just try again
        if (m_active) {
            m_timer->start();
        }
    });
}

void NextcloudLogin::cancel()
{
    if (m_active) {
        stop();
    }
}

void NextcloudLogin::stop()
{
    m_timer->stop();
    if (m_reply) {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->abort();
    }
    m_token.clear();
    if (m_active) {
        m_active = false;
        emit activeChanged();
    }
}

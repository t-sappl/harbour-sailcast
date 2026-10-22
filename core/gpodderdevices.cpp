// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpodderdevices.h"
#include "netutil.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

GpodderDevices::GpodderDevices(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

void GpodderDevices::setAccount(const QString &server, const QString &user, const QString &password)
{
    m_server = server.trimmed();
    while (m_server.endsWith(QLatin1Char('/'))) {
        m_server.chop(1);
    }
    if (!m_server.contains(QStringLiteral("://"))) {
        m_server.prepend(QStringLiteral("https://"));
    }
    m_user = user.trimmed();
    m_password = password;
}

QNetworkReply *GpodderDevices::send(const QByteArray &verb, const QString &path, const QByteArray &json)
{
    QNetworkRequest request(QUrl(m_server + QStringLiteral("/api/2/") + path));
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setRawHeader("Accept", "application/json");
    const QByteArray credentials = (m_user + QLatin1Char(':') + m_password).toUtf8();
    request.setRawHeader("Authorization", "Basic " + credentials.toBase64());
    if (verb == "POST") {
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/json"));
        return m_network->post(request, json);
    }
    return m_network->get(request);
}

QString GpodderDevices::errorText(QNetworkReply *reply) const
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 401 || status == 403) {
        return QStringLiteral("Login failed (HTTP %1)").arg(status);
    }
    if (status > 0) {
        return QStringLiteral("HTTP %1").arg(status);
    }
    return reply->errorString();
}

void GpodderDevices::list()
{
    const QString user = QString::fromLatin1(QUrl::toPercentEncoding(m_user));
    QNetworkReply *reply = send("GET", QStringLiteral("devices/%1.json").arg(user), QByteArray());
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(errorText(reply));
            return;
        }
        const QJsonArray array = QJsonDocument::fromJson(reply->readAll()).array();
        QList<GpodderDevice> devices;
        for (int i = 0; i < array.size(); ++i) {
            const QJsonObject o = array.at(i).toObject();
            GpodderDevice d;
            d.id = o.value(QStringLiteral("id")).toString();
            d.caption = o.value(QStringLiteral("caption")).toString();
            d.type = o.value(QStringLiteral("type")).toString();
            d.subscriptions = o.value(QStringLiteral("subscriptions")).toInt();
            if (!d.id.isEmpty()) {
                devices.append(d);
            }
        }
        emit listed(devices);
    });
}

void GpodderDevices::synchronize(const QStringList &deviceIds, const QString &registerFirst)
{
    if (registerFirst.isEmpty()) {
        postGroup(deviceIds);
        return;
    }
    // Same request as SyncEngine::registerDevice (caption, type)
    QJsonObject device;
    device.insert(QStringLiteral("caption"), QStringLiteral("Sailcast"));
    device.insert(QStringLiteral("type"), QStringLiteral("mobile"));
    const QString user = QString::fromLatin1(QUrl::toPercentEncoding(m_user));
    const QString id = QString::fromLatin1(QUrl::toPercentEncoding(registerFirst));
    QNetworkReply *reply = send("POST", QStringLiteral("devices/%1/%2.json").arg(user, id),
                                QJsonDocument(device).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply, deviceIds]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit synchronizeFailed(errorText(reply));
            return;
        }
        postGroup(deviceIds);
    });
}

void GpodderDevices::postGroup(const QStringList &deviceIds)
{
    QJsonArray group;
    for (int i = 0; i < deviceIds.size(); ++i) {
        group.append(deviceIds.at(i));
    }
    QJsonArray groups;
    groups.append(group);
    QJsonObject body;
    body.insert(QStringLiteral("synchronize"), groups);
    body.insert(QStringLiteral("stop-synchronize"), QJsonArray());
    const QString user = QString::fromLatin1(QUrl::toPercentEncoding(m_user));
    QNetworkReply *reply = send("POST", QStringLiteral("sync-devices/%1.json").arg(user),
                                QJsonDocument(body).toJson(QJsonDocument::Compact));
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit synchronizeFailed(errorText(reply));
            return;
        }
        emit synchronized();
    });
}

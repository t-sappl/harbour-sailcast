// SPDX-License-Identifier: GPL-3.0-or-later
#include "webdavbackup.h"
#include "netutil.h"

#include <QBuffer>
#include <QDateTime>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QStringList>
#include <QUrl>
#include <QXmlStreamReader>

#include <algorithm>

WebDavBackup::WebDavBackup(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

bool WebDavBackup::prepare(const QString &server, const QString &user, const QString &password,
                           const QString &folder)
{
    QString base = server.trimmed();
    while (base.endsWith(QLatin1Char('/'))) {
        base.chop(1);
    }
    if (base.isEmpty() || user.trimmed().isEmpty() || password.isEmpty()) {
        return false;
    }
    m_folderUrl = base + QStringLiteral("/remote.php/dav/files/")
            + QString::fromLatin1(QUrl::toPercentEncoding(user.trimmed()))
            + QLatin1Char('/') + QString::fromLatin1(QUrl::toPercentEncoding(folder));
    m_auth = "Basic " + (user.trimmed() + QLatin1Char(':') + password).toUtf8().toBase64();
    return true;
}

void WebDavBackup::upload(const QString &server, const QString &user, const QString &password,
                          const QString &localPath, const QString &folder, int keep)
{
    if (m_running) {
        return;
    }
    if (!prepare(server, user, password, folder)) {
        emit finished(false, QStringLiteral("No Nextcloud account"));
        return;
    }
    m_running = true;
    m_localPath = localPath;
    m_keep = keep;
    makeFolder();
}

void WebDavBackup::list(const QString &server, const QString &user, const QString &password,
                        const QString &folder)
{
    if (!prepare(server, user, password, folder)) {
        emit listed(false, QList<Entry>(), QStringLiteral("No Nextcloud account"));
        return;
    }
    const QByteArray body =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:getlastmodified/><d:getcontentlength/></d:prop></d:propfind>";
    QNetworkReply *reply = send("PROPFIND", m_folderUrl + QLatin1Char('/'), body, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            // No backup folder yet: no backups
            emit listed(true, QList<Entry>(), QString());
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            emit listed(false, QList<Entry>(), errorText(reply));
            return;
        }
        // One <d:response> per file: href, getlastmodified (RFC 1123), getcontentlength
        QList<Entry> entries;
        Entry current;
        QXmlStreamReader xml(reply->readAll());
        while (!xml.atEnd()) {
            xml.readNext();
            if (!xml.isStartElement() && !xml.isEndElement()) {
                continue;
            }
            if (xml.namespaceUri() != QLatin1String("DAV:")) {
                continue;
            }
            const QStringRef tag = xml.name();
            if (xml.isStartElement() && tag == QLatin1String("response")) {
                current = Entry();
            } else if (xml.isStartElement() && tag == QLatin1String("href")) {
                const QString href = QUrl::fromPercentEncoding(xml.readElementText().toUtf8());
                current.name = href.section(QLatin1Char('/'), -1);
            } else if (xml.isStartElement() && tag == QLatin1String("getlastmodified")) {
                QString text = xml.readElementText().trimmed();
                // "Thu, 23 Oct 2026 09:21:00 GMT" - Qt's RFC 2822 parser
                // wants the zone as an offset
                text.replace(QLatin1String("GMT"), QLatin1String("+0000"));
                const QDateTime time = QDateTime::fromString(text, Qt::RFC2822Date);
                current.modified = time.isValid() ? time.toMSecsSinceEpoch() : 0;
            } else if (xml.isStartElement() && tag == QLatin1String("getcontentlength")) {
                current.size = xml.readElementText().toLongLong();
            } else if (xml.isEndElement() && tag == QLatin1String("response")) {
                if (current.name.startsWith(QLatin1String("sailcast-")) && current.name.endsWith(QLatin1String(".db"))) {
                    entries.append(current);
                }
            }
        }
        // Newest first (names carry the date)
        std::sort(entries.begin(), entries.end(), [](const Entry &a, const Entry &b) {
            return a.name > b.name;
        });
        emit listed(true, entries, QString());
    });
}

void WebDavBackup::fetch(const QString &server, const QString &user, const QString &password,
                         const QString &folder, const QString &name, const QString &localPath)
{
    if (!prepare(server, user, password, folder)) {
        emit fetched(false, localPath, QStringLiteral("No Nextcloud account"));
        return;
    }
    QNetworkReply *reply = send("GET", m_folderUrl + QLatin1Char('/')
                                + QString::fromLatin1(QUrl::toPercentEncoding(name)));
    connect(reply, &QNetworkReply::finished, this, [this, reply, localPath]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            emit fetched(false, localPath, errorText(reply));
            return;
        }
        const QByteArray data = reply->readAll();
        // A SQLite database starts with this header
        if (!data.startsWith("SQLite format 3")) {
            emit fetched(false, localPath, QStringLiteral("Not a Sailcast database"));
            return;
        }
        QFile file(localPath);
        if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size()) {
            emit fetched(false, localPath, QStringLiteral("Cannot write %1").arg(localPath));
            return;
        }
        file.close();
        emit fetched(true, localPath, QString());
    });
}

QNetworkReply *WebDavBackup::send(const QByteArray &verb, const QString &url, const QByteArray &body,
                                  bool depthOne)
{
    QNetworkRequest request{QUrl(url)};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setRawHeader("Authorization", m_auth);
    if (depthOne) {
        request.setRawHeader("Depth", "1");
        request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/xml; charset=utf-8"));
    }
    // Qt 5.6 has no sendCustomRequest with a QByteArray (5.8): a buffer
    // that lives as long as the reply
    QBuffer *buffer = new QBuffer;
    buffer->setData(body);
    buffer->open(QIODevice::ReadOnly);
    QNetworkReply *reply = m_network->sendCustomRequest(request, verb, buffer);
    buffer->setParent(reply);
    return reply;
}

QString WebDavBackup::errorText(QNetworkReply *reply) const
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

void WebDavBackup::makeFolder()
{
    QNetworkReply *reply = send("MKCOL", m_folderUrl);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        // 405: the folder exists already
        if (status == 201 || status == 405) {
            put();
        } else {
            done(false, errorText(reply));
        }
    });
}

void WebDavBackup::put()
{
    QFile *file = new QFile(m_localPath);
    if (!file->open(QIODevice::ReadOnly)) {
        delete file;
        done(false, QStringLiteral("Cannot read %1").arg(m_localPath));
        return;
    }
    QNetworkRequest request{QUrl(m_folderUrl + QLatin1Char('/')
                                 + QString::fromLatin1(QUrl::toPercentEncoding(QFileInfo(m_localPath).fileName())))};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setRawHeader("Authorization", m_auth);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QStringLiteral("application/octet-stream"));
    request.setHeader(QNetworkRequest::ContentLengthHeader, file->size());
    QNetworkReply *reply = m_network->put(request, file);
    file->setParent(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            done(false, errorText(reply));
            return;
        }
        listAndPrune();
    });
}

void WebDavBackup::listAndPrune()
{
    const QByteArray body =
            "<?xml version=\"1.0\" encoding=\"utf-8\"?>"
            "<d:propfind xmlns:d=\"DAV:\"><d:prop><d:resourcetype/></d:prop></d:propfind>";
    QNetworkReply *reply = send("PROPFIND", m_folderUrl + QLatin1Char('/'), body, true);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            // The backup is there; only the clean-up failed
            done(true);
            return;
        }
        // File names of the backups (from the <d:href> of each entry)
        QStringList names;
        QXmlStreamReader xml(reply->readAll());
        while (!xml.atEnd()) {
            xml.readNext();
            if (xml.isStartElement() && xml.name() == QLatin1String("href")
                    && xml.namespaceUri() == QLatin1String("DAV:")) {
                const QString href = QUrl::fromPercentEncoding(xml.readElementText().toUtf8());
                const QString name = href.section(QLatin1Char('/'), -1);
                if (name.startsWith(QLatin1String("sailcast-")) && name.endsWith(QLatin1String(".db"))) {
                    names.append(name);
                }
            }
        }
        std::sort(names.begin(), names.end());
        for (int i = 0; i + m_keep < names.size(); ++i) {
            QNetworkReply *remove = send("DELETE", m_folderUrl + QLatin1Char('/')
                                         + QString::fromLatin1(QUrl::toPercentEncoding(names.at(i))));
            connect(remove, &QNetworkReply::finished, remove, &QNetworkReply::deleteLater);
        }
        done(true);
    });
}

void WebDavBackup::done(bool ok, const QString &error)
{
    m_running = false;
    emit finished(ok, error);
}

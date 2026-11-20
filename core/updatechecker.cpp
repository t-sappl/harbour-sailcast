// SPDX-License-Identifier: GPL-3.0-or-later
#include "updatechecker.h"

#include <QDir>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QSysInfo>
#include <QUrl>

#include "netutil.h"
#include "releasenotes.h"

UpdateChecker::UpdateChecker(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

QString UpdateChecker::releasesUrl()
{
    return QStringLiteral("https://api.github.com/repos/t-sappl/harbour-sailcast/releases/latest");
}

QString UpdateChecker::trustedAssetPrefix()
{
    return QStringLiteral("https://github.com/t-sappl/harbour-sailcast/releases/download/");
}

QList<int> UpdateChecker::parseVersion(const QString &text)
{
    return ReleaseNotes::parseVersion(text);
}

int UpdateChecker::compareVersions(const QString &a, const QString &b)
{
    return ReleaseNotes::compareVersions(a, b);
}

QString UpdateChecker::architectureFor(const QString &cpu)
{
    if (cpu == QLatin1String("arm64") || cpu == QLatin1String("aarch64")) {
        return QStringLiteral("aarch64");
    }
    if (cpu == QLatin1String("arm")) {
        return QStringLiteral("armv7hl");
    }
    if (cpu == QLatin1String("i386") || cpu == QLatin1String("x86_64")) {
        return QStringLiteral("i486");    // the SDK emulator
    }
    return QString();
}

QString UpdateChecker::architecture()
{
    // currentCpuArchitecture: of the running system (a 32-bit app on a
    // 64-bit kernel would say arm64); the RPM must match the app, so the
    // build architecture decides first
    const QString built = architectureFor(QSysInfo::buildCpuArchitecture());
    return built.isEmpty() ? architectureFor(QSysInfo::currentCpuArchitecture()) : built;
}

UpdateInfo UpdateChecker::parseRelease(const QByteArray &json, const QString &arch)
{
    UpdateInfo info;
    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError || !doc.isObject() || arch.isEmpty()) {
        return info;
    }
    const QJsonObject release = doc.object();
    if (release.value(QLatin1String("draft")).toBool() || release.value(QLatin1String("prerelease")).toBool()) {
        return info;
    }
    const QString tag = release.value(QLatin1String("tag_name")).toString();
    if (parseVersion(tag).isEmpty()) {
        return info;
    }
    const QJsonArray assets = release.value(QLatin1String("assets")).toArray();
    const QString suffix = QLatin1Char('.') + arch + QLatin1String(".rpm");
    for (int i = 0; i < assets.size(); ++i) {
        const QJsonObject asset = assets.at(i).toObject();
        const QString name = asset.value(QLatin1String("name")).toString();
        const QString url = asset.value(QLatin1String("browser_download_url")).toString();
        if (!name.endsWith(suffix) || !isOwnPackage(name)
                || !url.startsWith(trustedAssetPrefix()) || name.contains(QLatin1Char('/'))) {
            continue;
        }
        // (review 346) The package must be the tagged version: a release
        // built with an old spec version would offer the old app again
        if (compareVersions(name.mid(QStringLiteral("harbour-sailcast-").size()), tag) != 0) {
            continue;
        }
        info.valid = true;
        info.tag = tag;
        const QList<int> v = parseVersion(tag);
        info.version = QStringLiteral("%1.%2.%3").arg(v.at(0)).arg(v.at(1)).arg(v.at(2));
        const QString body = release.value(QLatin1String("body")).toString();
        info.notes = plainNotes(ReleaseNotes::localized(body, false));
        info.notesDe = plainNotes(ReleaseNotes::localized(body, true));
        info.assetName = name;
        info.assetUrl = url;
        info.assetSize = static_cast<qint64>(asset.value(QLatin1String("size")).toDouble());
        return info;
    }
    return info;    // no RPM for this architecture (yet): no update to offer
}

QString UpdateChecker::plainNotes(const QString &markdown)
{
    return ReleaseNotes::plain(markdown);
}

bool UpdateChecker::isOwnPackage(const QString &fileName)
{
    static const QRegularExpression re(QStringLiteral(
        "^harbour-sailcast-\\d+\\.\\d+(?:\\.\\d+)?-\\d+\\.(?:aarch64|armv7hl|i486)\\.rpm(?:\\.part)?$"));
    return re.match(fileName).hasMatch();
}

QStringList UpdateChecker::obsoletePackages(const QStringList &fileNames, const QString &runningVersion)
{
    QStringList result;
    for (int i = 0; i < fileNames.size(); ++i) {
        const QString &name = fileNames.at(i);
        if (!isOwnPackage(name)) {
            continue;
        }
        if (name.endsWith(QLatin1String(".part"))) {
            result << name;    // no download is running at the start
            continue;
        }
        // harbour-sailcast-<version>-<release>...: the version is the first number group
        const QString version = name.mid(QStringLiteral("harbour-sailcast-").size());
        if (compareVersions(version, runningVersion) <= 0) {
            result << name;
        }
    }
    return result;
}

void UpdateChecker::check(const QString &etag)
{
    if (m_checkReply) {
        return;
    }
    QNetworkRequest request{QUrl(releasesUrl())};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setRawHeader("Accept", "application/vnd.github+json");
    if (!etag.isEmpty()) {
        request.setRawHeader("If-None-Match", etag.toUtf8());
    }
    m_checkReply = m_network->get(request);
    connect(m_checkReply, &QNetworkReply::finished, this, &UpdateChecker::onCheckFinished);
}

void UpdateChecker::onCheckFinished()
{
    QNetworkReply *reply = m_checkReply;
    m_checkReply = nullptr;
    reply->deleteLater();
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status == 304) {
        emit checkFinished(true, QByteArray(), QString());
        return;
    }
    if (reply->error() != QNetworkReply::NoError || status != 200) {
        // 404: no release yet; 403: rate limit
        emit checkFailed(status > 0 ? QStringLiteral("HTTP %1").arg(status) : reply->errorString());
        return;
    }
    emit checkFinished(false, reply->readAll(), QString::fromUtf8(reply->rawHeader("ETag")));
}

void UpdateChecker::download(const UpdateInfo &info, const QString &directory)
{
    if (m_downloadReply || !info.valid) {
        return;
    }
    QDir().mkpath(directory);
    m_path = QDir(directory).filePath(info.assetName);
    m_file.setFileName(m_path + QLatin1String(".part"));
    if (!m_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        emit downloadFailed(m_file.errorString());
        return;
    }
    m_expectedSize = info.assetSize;
    m_cancelled = false;
    QNetworkRequest request{QUrl(info.assetUrl)};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    m_downloadReply = m_network->get(request);
    m_writeFailed = false;
    connect(m_downloadReply, &QNetworkReply::readyRead, this, [this]() {
        if (m_downloadReply && m_file.isOpen() && !m_writeFailed) {
            const QByteArray data = m_downloadReply->readAll();
            // (review 346) Disk full: stop instead of offering a cut file
            if (m_file.write(data) != data.size()) {
                m_writeFailed = true;
                m_downloadReply->abort();
            }
        }
    });
    connect(m_downloadReply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
        emit downloadProgress(received, total > 0 ? total : m_expectedSize);
    });
    connect(m_downloadReply, &QNetworkReply::finished, this, &UpdateChecker::onDownloadFinished);
}

void UpdateChecker::cancelDownload()
{
    if (m_downloadReply) {
        m_cancelled = true;
        m_downloadReply->abort();
    }
}

void UpdateChecker::removePart()
{
    m_file.close();
    QFile::remove(m_path + QLatin1String(".part"));
}

void UpdateChecker::onDownloadFinished()
{
    QNetworkReply *reply = m_downloadReply;
    m_downloadReply = nullptr;
    reply->deleteLater();
    if (m_file.isOpen() && !m_writeFailed) {
        const QByteArray rest = reply->readAll();
        if (m_file.write(rest) != rest.size() || !m_file.flush()) {
            m_writeFailed = true;
        }
    }
    if (m_writeFailed) {
        removePart();
        emit downloadFailed(QStringLiteral("write: %1").arg(m_file.errorString()));
        return;
    }
    const bool cancelled = m_cancelled;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (cancelled) {
        removePart();
        return;
    }
    if (reply->error() != QNetworkReply::NoError || (status != 0 && status != 200)) {
        const QString error = reply->error() != QNetworkReply::NoError ? reply->errorString()
                                                                          : QStringLiteral("HTTP %1").arg(status);
        removePart();
        emit downloadFailed(error);
        return;
    }
    const qint64 size = m_file.size();
    m_file.close();
    if (m_expectedSize > 0 && size != m_expectedSize) {
        QFile::remove(m_path + QLatin1String(".part"));
        emit downloadFailed(QStringLiteral("size %1 of %2").arg(size).arg(m_expectedSize));
        return;
    }
    QFile::remove(m_path);
    if (!QFile::rename(m_path + QLatin1String(".part"), m_path)) {
        QFile::remove(m_path + QLatin1String(".part"));
        emit downloadFailed(QStringLiteral("rename"));
        return;
    }
    emit downloadFinished(m_path);
}

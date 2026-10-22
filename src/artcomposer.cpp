// SPDX-License-Identifier: GPL-3.0-or-later
#include "artcomposer.h"
#include "imagefingerprint.h"
#include "networkaccess.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPainter>
#include <QPainterPath>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#include <QDebug>

namespace {
// Give up on a single download after this time
const int RequestTimeoutMs = 15000;
// Keep the last few images: the lock screen may still be reading the
// previous one while the new one is announced
const int FilesToKeep = 2;

QString artDirectory()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/cover-art");
}
}

ArtComposer::ArtComposer(QObject *parent)
    : QObject(parent)
    // Own disk cache (separate directory from the QML image cache)
    , m_network(new NetworkAccess::ImageCachingManager(QStringLiteral("composer"),
                                                       NetworkAccess::ComposerImageCacheBytes, this))
    , m_imageSize(512)
    , m_jobId(0)
    , m_logoIndex(0)
    , m_albumDone(true)
    , m_logoDone(true)
{
    NetworkAccess::keepAccessible(m_network);

    // Unlike QML, C++ can create the folder and clean up files from earlier sessions
    QDir().mkpath(artDirectory());
    removeOldFiles();
}

void ArtComposer::setUserAgent(const QString &userAgent)
{
    if (m_userAgent != userAgent) {
        m_userAgent = userAgent;
        emit userAgentChanged();
    }
}

void ArtComposer::setImageSize(int size)
{
    if (size > 0 && m_imageSize != size) {
        m_imageSize = size;
        emit imageSizeChanged();
    }
}

void ArtComposer::compose(const QString &albumUrl, const QStringList &logoUrls, const QString &key)
{
    // A new job replaces the previous one; replies of older jobs are ignored
    ++m_jobId;
    m_key = key;
    m_logoUrls = logoUrls;
    m_logoIndex = 0;
    m_album = QImage();
    m_logo = QImage();
    m_albumDone = albumUrl.isEmpty();
    m_logoDone = logoUrls.isEmpty();

    if (!m_albumDone) {
        fetch(albumUrl, true);
    }
    if (!m_logoDone) {
        fetch(m_logoUrls.at(0), false);
    }
    finishIfReady();
}

void ArtComposer::fetch(const QString &url, bool isAlbum)
{
    QNetworkRequest request(QUrl(url, QUrl::TolerantMode));
    if (!m_userAgent.isEmpty()) {
        request.setHeader(QNetworkRequest::UserAgentHeader, m_userAgent);
    }
    // Image URLs often redirect (http -> https, CDNs)
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    // Some servers only deliver images when asked for them explicitly
    request.setRawHeader("Accept", "image/*,*/*;q=0.8");

    QNetworkReply *reply = m_network->get(request);
    reply->setProperty("jobId", m_jobId);
    reply->setProperty("isAlbum", isAlbum);
    reply->setProperty("sourceUrl", url);

    // QNetworkAccessManager has no timeout of its own in Qt 5.6
    QTimer::singleShot(RequestTimeoutMs, reply, SLOT(abort()));

    connect(reply, &QNetworkReply::finished, this, [this, reply]() {
        onReplyFinished(reply);
    });
}

void ArtComposer::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();

    if (reply->property("jobId").toULongLong() != m_jobId) {
        return; // belongs to an outdated job
    }

    const bool isAlbum = reply->property("isAlbum").toBool();
    const QString url = reply->property("sourceUrl").toString();

    QImage image;
    if (reply->error() == QNetworkReply::NoError) {
        const QByteArray data = reply->readAll();
        if (!image.loadFromData(data)) {
            // Loaded, but Qt cannot decode it (e.g. .svg without plugin)
            qWarning() << "[ArtComposer] Unsupported image format:" << url
                       << "(" << data.size() << "bytes )";
        }
    } else {
        qWarning() << "[ArtComposer] Download failed:" << url << "-" << reply->errorString();
    }

    if (isAlbum) {
        m_album = image;
        m_albumDone = true;
    } else if (!image.isNull()) {
        m_logo = image;
        m_logoDone = true;
    } else {
        emit logoFailed(url);
        // Try the next cover candidate, if any
        ++m_logoIndex;
        if (m_logoIndex < m_logoUrls.size()) {
            fetch(m_logoUrls.at(m_logoIndex), false);
        } else {
            m_logoDone = true;
        }
    }

    finishIfReady();
}

void ArtComposer::finishIfReady()
{
    if (!m_albumDone || !m_logoDone) {
        return;
    }
    if (m_album.isNull() && m_logo.isNull()) {
        emit failed(m_key);
        return;
    }
    const QString path = save(render());
    if (path.isEmpty()) {
        emit failed(m_key);
    } else {
        emit composed(m_key, path);
    }
}

QImage ArtComposer::render() const
{
    const int size = m_imageSize;
    QImage canvas(size, size, QImage::Format_ARGB32_Premultiplied);
    canvas.fill(Qt::transparent);

    QPainter painter(&canvas);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);

    // The episode image is often just the podcast cover again: then the
    // cover alone, without the badge
    const bool sameAsCover = !m_album.isNull() && !m_logo.isNull() && ImageFingerprint::same(m_album, m_logo);

    if (!m_album.isNull() && !sameAsCover) {
        // Album cover fills the square (cropped to the centre if not square)
        const QImage scaled = m_album.scaled(size, size, Qt::KeepAspectRatioByExpanding,
                                             Qt::SmoothTransformation);
        painter.drawImage(0, 0, scaled, (scaled.width() - size) / 2, (scaled.height() - size) / 2,
                          size, size);

        if (!m_logo.isNull()) {
            // Podcast cover as a small rounded badge with a white frame in
            // the bottom right corner
            const qreal badge = size * 0.32;
            const qreal margin = size * 0.04;
            const QRectF badgeRect(size - badge - margin, size - badge - margin, badge, badge);
            painter.setPen(Qt::NoPen);
            painter.setBrush(Qt::white);
            painter.drawRoundedRect(badgeRect, badge * 0.2, badge * 0.2);

            const qreal inset = badge * 0.06;
            const QRectF coverArea = badgeRect.adjusted(inset, inset, -inset, -inset);
            const QSize areaSize = coverArea.size().toSize();
            const QImage cover = m_logo.scaled(areaSize, Qt::KeepAspectRatioByExpanding,
                                               Qt::SmoothTransformation);
            QPainterPath clip;
            clip.addRoundedRect(coverArea, badge * 0.15, badge * 0.15);
            painter.save();
            painter.setClipPath(clip);
            painter.drawImage(coverArea.topLeft(), cover,
                              QRectF((cover.width() - areaSize.width()) / 2.0,
                                     (cover.height() - areaSize.height()) / 2.0,
                                     areaSize.width(), areaSize.height()));
            painter.restore();
        }
    } else {
        // Podcast cover alone, filling the square (cropped to the centre)
        const QImage logo = m_logo.scaled(size, size, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        painter.drawImage(0, 0, logo, (logo.width() - size) / 2, (logo.height() - size) / 2, size, size);
    }

    painter.end();
    return canvas;
}

QString ArtComposer::save(const QImage &image)
{
    const QString dir = artDirectory();
    QDir().mkpath(dir);

    // A new file name for every image: the lock screen does not reload a file
    // whose name it already knows
    const QString path = dir + QStringLiteral("/art-")
            + QString::number(QDateTime::currentMSecsSinceEpoch()) + QStringLiteral(".png");
    if (!image.save(path, "PNG")) {
        qWarning() << "[ArtComposer] Could not save" << path;
        return QString();
    }

    m_writtenFiles.append(path);
    while (m_writtenFiles.size() > FilesToKeep) {
        QFile::remove(m_writtenFiles.takeFirst());
    }
    return path;
}

void ArtComposer::removeOldFiles()
{
    QDir dir(artDirectory());
    const QStringList files = dir.entryList(QStringList() << QStringLiteral("art-*.png"), QDir::Files);
    for (const QString &file : files) {
        dir.remove(file);
    }
}

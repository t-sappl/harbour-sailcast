// SPDX-License-Identifier: GPL-3.0-or-later
#include "imagecache.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QMetaObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QRunnable>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#include "imagefingerprint.h"
#include "netutil.h"

namespace {

ImageCache *s_instance = nullptr;

// Largest side of a cached image: sharp on the episode page of a phone
// (about 2/3 of the screen width) and still small on disk (~50-100 KB)
const int MaxSize = 600;
const int ParallelDownloads = 4;
const int RequestTimeoutMs = 30000;
// Cleaned up on start: above the high mark the oldest files go until the
// low mark is reached
const qint64 HighMarkBytes = 150LL * 1024 * 1024;
const qint64 LowMarkBytes = 100LL * 1024 * 1024;

// Decodes (scaled down while decoding where the format supports it, e.g.
// JPEG) and saves the image; runs in the thread pool
class ScaleTask : public QRunnable
{
public:
    ScaleTask(ImageCache *cache, const QString &remote, const QByteArray &data, const QString &path)
        : m_cache(cache), m_remote(remote), m_data(data), m_path(path) {}

    void run() override
    {
        QBuffer buffer(&m_data);
        buffer.open(QIODevice::ReadOnly);
        QImageReader reader(&buffer);
        const QSize size = reader.size();
        if (size.isValid() && (size.width() > MaxSize || size.height() > MaxSize)) {
            reader.setScaledSize(size.scaled(MaxSize, MaxSize, Qt::KeepAspectRatio));
        }
        QImage image = reader.read();
        bool ok = !image.isNull();
        if (ok && (image.width() > MaxSize || image.height() > MaxSize)) {
            // Formats without scaled decoding (or an unknown size up front)
            image = image.scaled(MaxSize, MaxSize, Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }
        if (ok) {
            // JPEG has no transparency: put transparent logos on white
            if (image.hasAlphaChannel()) {
                QImage flat(image.size(), QImage::Format_RGB32);
                flat.fill(Qt::white);
                QImage source = image.convertToFormat(QImage::Format_ARGB32_Premultiplied);
                for (int y = 0; y < flat.height(); ++y) {
                    QRgb *out = reinterpret_cast<QRgb *>(flat.scanLine(y));
                    const QRgb *in = reinterpret_cast<const QRgb *>(source.constScanLine(y));
                    for (int x = 0; x < flat.width(); ++x) {
                        const int a = qAlpha(in[x]);
                        // premultiplied: c + white * (1 - a)
                        out[x] = qRgb(qRed(in[x]) + 255 - a, qGreen(in[x]) + 255 - a,
                                      qBlue(in[x]) + 255 - a);
                    }
                }
                image = flat;
            }
            const QString part = m_path + QStringLiteral(".part");
            ok = image.save(part, "JPEG", 88);
            if (ok) {
                QFile::remove(m_path);
                ok = QFile::rename(part, m_path);
            }
            if (!ok) {
                QFile::remove(part);
            }
        }
        if (m_cache) {
            QMetaObject::invokeMethod(m_cache.data(), "scaled", Qt::QueuedConnection,
                                      Q_ARG(QString, m_remote), Q_ARG(bool, ok));
        }
    }

private:
    QPointer<ImageCache> m_cache;
    QString m_remote;
    QByteArray m_data;
    QString m_path;
};

QString localUrl(const QString &path)
{
    return QUrl::fromLocalFile(path).toString();
}

} // namespace

ImageCache::ImageCache(QObject *parent)
    : QObject(parent)
    , m_directory(QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/covers"))
    , m_network(new QNetworkAccessManager(this))
{
    s_instance = this;
    NetworkAccess::keepAccessible(m_network);
    QDir().mkpath(m_directory);
    m_pool.setMaxThreadCount(2);
    // Not during start-up
    QTimer::singleShot(10000, this, &ImageCache::prune);
}

ImageCache::~ImageCache()
{
    m_pool.clear();
    m_pool.waitForDone();
    s_instance = nullptr;
}

ImageCache *ImageCache::instance()
{
    return s_instance;
}

QString ImageCache::pathFor(const QString &remote) const
{
    return m_directory + QLatin1Char('/')
            + QString::fromLatin1(QCryptographicHash::hash(remote.toUtf8(), QCryptographicHash::Sha1).toHex())
            + QStringLiteral(".jpg");
}

QString ImageCache::url(const QString &remote)
{
    if (remote.isEmpty()) {
        return QString();
    }
    // Local files and theme images need no cache
    if (!remote.startsWith(QLatin1String("http:"), Qt::CaseInsensitive)
            && !remote.startsWith(QLatin1String("https:"), Qt::CaseInsensitive)) {
        return remote;
    }
    const QString path = pathFor(remote);
    if (m_known.contains(remote)) {
        return localUrl(path);
    }
    if (m_failed.contains(remote)) {
        return remote;
    }
    if (QFile::exists(path)) {
        m_known.insert(remote);
        return localUrl(path);
    }
    if (!m_busy.contains(remote)) {
        m_busy.insert(remote);
        // Newest request first: what is on screen now matters most
        m_queue.prepend(remote);
        startNext();
    }
    return QString();
}

QVector<int> ImageCache::fingerprint(const QString &remote)
{
    if (m_fingerprints.contains(remote)) {
        return m_fingerprints.value(remote);
    }
    if (!m_known.contains(remote) && url(remote) != localUrl(pathFor(remote))) {
        return QVector<int>();   // not cached (yet)
    }
    // The cached JPEG decoded at a tiny size is enough and fast
    QImageReader reader(pathFor(remote));
    reader.setScaledSize(QSize(ImageFingerprint::Size * 2, ImageFingerprint::Size * 2));
    const QVector<int> values = ImageFingerprint::of(reader.read());
    if (!values.isEmpty()) {
        m_fingerprints.insert(remote, values);
    }
    return values;
}

bool ImageCache::differentImages(const QString &remoteA, const QString &remoteB)
{
    if (remoteA.isEmpty() || remoteB.isEmpty() || remoteA == remoteB) {
        return false;
    }
    const QVector<int> a = fingerprint(remoteA);
    const QVector<int> b = fingerprint(remoteB);
    return !a.isEmpty() && !b.isEmpty() && !ImageFingerprint::same(a, b);
}

void ImageCache::startNext()
{
    while (m_running < ParallelDownloads && !m_queue.isEmpty()) {
        const QString remote = m_queue.takeFirst();
        QNetworkRequest request{QUrl(remote, QUrl::TolerantMode)};
        request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
        request.setHeader(QNetworkRequest::UserAgentHeader, QStringLiteral("Sailcast"));
        request.setRawHeader("Accept", "image/*,*/*;q=0.8");
        QNetworkReply *reply = m_network->get(request);
        reply->setProperty("remote", remote);
        // QNetworkAccessManager has no timeout of its own in Qt 5.6
        QTimer::singleShot(RequestTimeoutMs, reply, SLOT(abort()));
        connect(reply, &QNetworkReply::finished, this, [this, reply]() { onReplyFinished(reply); });
        ++m_running;
    }
}

void ImageCache::onReplyFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    --m_running;
    const QString remote = reply->property("remote").toString();
    if (reply->error() == QNetworkReply::NoError) {
        m_pool.start(new ScaleTask(this, remote, reply->readAll(), pathFor(remote)));
    } else {
        qWarning() << "[ImageCache] Download failed:" << remote << "-" << reply->errorString();
        scaled(remote, false);
    }
    startNext();
}

void ImageCache::scaled(const QString &remote, bool ok)
{
    m_busy.remove(remote);
    if (ok) {
        m_known.insert(remote);
        emit cached(remote, localUrl(pathFor(remote)));
    } else {
        m_failed.insert(remote);
        // The QML loader gets the remote URL as a last resort
        emit cached(remote, remote);
    }
}

void ImageCache::prune()
{
    QDir dir(m_directory);
    QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.jpg"), QDir::Files);
    qint64 total = 0;
    for (int i = 0; i < files.size(); ++i) {
        total += files.at(i).size();
    }
    if (total <= HighMarkBytes) {
        return;
    }
    std::sort(files.begin(), files.end(), [](const QFileInfo &a, const QFileInfo &b) {
        return a.lastModified() < b.lastModified();
    });
    for (int i = 0; i < files.size() && total > LowMarkBytes; ++i) {
        total -= files.at(i).size();
        QFile::remove(files.at(i).absoluteFilePath());
    }
    // Which remote URL a removed file belonged to is unknown here
    m_known.clear();
}

void ImageCache::clear()
{
    QDir dir(m_directory);
    const QStringList files = dir.entryList(QDir::Files);
    for (int i = 0; i < files.size(); ++i) {
        dir.remove(files.at(i));
    }
    m_known.clear();
    m_failed.clear();
    m_fingerprints.clear();
}

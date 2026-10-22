// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef IMAGECACHE_H
#define IMAGECACHE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QThreadPool>
#include <QVector>

class QNetworkAccessManager;
class QNetworkReply;

// Local copies of podcast covers and episode images, scaled down once.
//
// Podcast covers are often 3000 x 3000 pixels (several MB). Loaded through
// the QML network image loader they were decoded again and again while
// scrolling: the covers flickered and kept reloading. This cache downloads
// each image once, scales it to at most MaxSize pixels in a worker thread
// and stores it as a small JPEG. QML then only loads local files, which
// is fast and is served from QML's own pixmap cache afterwards.
//
// QML (context property "covers", see CoverImage.qml):
//   covers.url(remote) -> local file URL if cached, else "" and the image
//                         is fetched; cached(remote, localUrl) follows.
//                         If the download or decoding fails, the remote
//                         URL itself is returned (the QML loader may
//                         still manage, e.g. formats Qt cannot read here).
//   covers.differentImages(a, b) -> true only if both images are cached and
//                         show different pictures (see imagefingerprint.h);
//                         false for the same picture under two URLs and
//                         while one of them is still being fetched.
class ImageCache : public QObject
{
    Q_OBJECT

public:
    explicit ImageCache(QObject *parent = nullptr);
    ~ImageCache();

    static ImageCache *instance();

    Q_INVOKABLE QString url(const QString &remote);
    Q_INVOKABLE bool differentImages(const QString &remoteA, const QString &remoteB);
    // Empties the cache directory (images on screen stay until restart)
    Q_INVOKABLE void clear();

signals:
    void cached(const QString &remote, const QString &localUrl);

private slots:
    void scaled(const QString &remote, bool ok);

private:
    QString pathFor(const QString &remote) const;
    void startNext();
    void onReplyFinished(QNetworkReply *reply);
    void prune();
    QVector<int> fingerprint(const QString &remote);

    QString m_directory;
    QNetworkAccessManager *m_network;
    QThreadPool m_pool;
    QSet<QString> m_known;      // remote URLs with a local file
    QSet<QString> m_failed;     // remote URLs that could not be cached
    QSet<QString> m_busy;       // queued, downloading or scaling
    QStringList m_queue;
    QHash<QString, QVector<int> > m_fingerprints;
    int m_running = 0;
};

#endif // IMAGECACHE_H

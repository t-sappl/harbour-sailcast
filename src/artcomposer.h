// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ARTCOMPOSER_H
#define ARTCOMPOSER_H

#include <QObject>
#include <QImage>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

// Composes the cover art image for MPRIS (lock screen) entirely in C++
// (taken over from Sailwave).
//
// QML's grabToImage() only works while the main window is being rendered,
// which Sailfish OS does not do while the app is in the background or the
// device is locked. QImage/QPainter need no window at all, so this class
// produces the image in every situation - e.g. when the next episode starts
// while the phone is locked.
//
// Result:
//   - episode image present: episode image filling the image, the podcast
//     cover as a small rounded badge (white frame) in the bottom right corner
//   - otherwise (or if the episode image shows the same picture as the
//     cover): the podcast cover alone
//
// Usage from QML:
//   composer.compose(episodeImageUrl, [coverUrl], key)
//   onComposed: (key, path) -> path is a local PNG file
//
// URLs may be local files (ImageCache). Cover URLs are tried in the given
// order; the first one that loads and decodes wins. Only the most recent
// compose() request is completed - older ones are dropped as soon as a new
// one arrives.
class ArtComposer : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString userAgent READ userAgent WRITE setUserAgent NOTIFY userAgentChanged)
    Q_PROPERTY(int imageSize READ imageSize WRITE setImageSize NOTIFY imageSizeChanged)

public:
    explicit ArtComposer(QObject *parent = 0);

    QString userAgent() const { return m_userAgent; }
    void setUserAgent(const QString &userAgent);

    int imageSize() const { return m_imageSize; }
    void setImageSize(int size);

    Q_INVOKABLE void compose(const QString &albumUrl, const QStringList &logoUrls, const QString &key);

signals:
    void userAgentChanged();
    void imageSizeChanged();

    // The image for "key" is ready as a local PNG file
    void composed(const QString &key, const QString &path);
    // Neither album cover nor any logo could be loaded for "key"
    void failed(const QString &key);
    // A logo URL could not be loaded or decoded (informational; the next
    // candidate is tried automatically)
    void logoFailed(const QString &url);

private:
    void fetch(const QString &url, bool isAlbum);
    void onReplyFinished(QNetworkReply *reply);
    void finishIfReady();
    QImage render() const;
    QString save(const QImage &image);
    void removeOldFiles();

    QNetworkAccessManager *m_network;
    QString m_userAgent;
    int m_imageSize;

    // State of the current (most recent) job
    quint64 m_jobId;
    QString m_key;
    QStringList m_logoUrls;
    int m_logoIndex;
    bool m_albumDone;
    bool m_logoDone;
    QImage m_album;
    QImage m_logo;

    // Recently written files; older ones are deleted
    QStringList m_writtenFiles;
};

#endif // ARTCOMPOSER_H

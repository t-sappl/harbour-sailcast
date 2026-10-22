// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDPROBE_H
#define FEEDPROBE_H

#include <QObject>
#include <QString>
#include <QUrl>

class QNetworkAccessManager;
class QNetworkReply;

// Result of a feed check (see FeedProbe)
struct FeedProbeResult
{
    enum Status { Ok, LoginRequired, NotAFeed, HttpError, NetworkError, Timeout, InvalidUrl };

    Status status = InvalidUrl;
    QString requestedUrl;   // as given to check()
    QString url;            // the feed URL to subscribe (a website: the feed it names)
    QString title;
    QString author;
    QString coverUrl;
    int episodes = 0;       // episodes with a media file
    QString description;    // of the podcast (50)
    // The newest episodes (50, at most 5): title, published, duration
    struct Latest { QString title; qint64 published; int duration; };
    QList<Latest> latest;
    int httpStatus = 0;
    bool discovered = false; // url is the feed named by a website
    QString error;          // technical detail (network error text)

    bool ok() const { return status == Ok; }
};

// Checks an address before subscribing (no database, no subscription):
// loads it, parses it as a feed and reports title, author, cover and the
// number of episodes. If the address is a website that names its feed
// (<link rel="alternate" type="application/rss+xml" href="...">), that feed
// is checked instead. 401/403 count as "login required" - such feeds can
// still be subscribed and get their login on the podcast page.
//
// One check at a time; a new check() cancels the running one.
class FeedProbe : public QObject
{
    Q_OBJECT
public:
    explicit FeedProbe(QObject *parent = nullptr);

    void check(const QString &url);

    // "example.com/feed" -> "https://example.com/feed", "feed://" -> "https://"
    static QString normalize(const QString &url);
    // Feed address named by an HTML page, empty if none
    static QString discoverFeed(const QByteArray &html, const QUrl &base);

signals:
    void finished(const FeedProbeResult &result);

private:
    void start(const QUrl &url);
    void onFinished(QNetworkReply *reply);
    void finish();

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    FeedProbeResult m_result;
    bool m_discovered = false;  // already following a feed named by a website
};

#endif // FEEDPROBE_H

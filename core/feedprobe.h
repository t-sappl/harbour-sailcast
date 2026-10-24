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
    // A web page: its title (og:title, else <title>) - the add page searches
    // for it when the page has no podcast feed (103: "spotify" was searched
    // for a Spotify page)
    QString pageTitle;
    QString error;          // technical detail (network error text)

    bool ok() const { return status == Ok; }
};

// Checks an address before subscribing (no database, no subscription):
// loads it, parses it as a feed and reports title, author, cover and the
// number of episodes. If the address is a website that names its feed
// (<link rel="alternate" type="application/rss+xml" href="...">), that feed
// is checked instead - since 102 all feeds a page names (at most 8; audio /
// podcast feeds first, comment feeds last) are loaded and the one with the
// most episodes with a media file (audio or video) wins, the earlier one on
// a tie (as Sailfeed's FeedFinder decides "podcast"; a blog or news feed
// named first made "cre.fm" or "orf.at" report a feed with 0 episodes).
// None with episodes: the result has 0 episodes (the add page says "no
// podcast"). 401/403 count as "login required" - such feeds can
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
    // All feeds named by an HTML page, the likely podcast feeds first (102)
    static QStringList discoverFeeds(const QByteArray &html, const QUrl &base);
    // The title of a web page: og:title, else <title>, entities decoded;
    // empty if there is none (103)
    static QString pageTitle(const QByteArray &html);

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
    QStringList m_candidates;   // further feeds named by the website (102)
    // Goes on with the next feed the website named; false if there is none
    bool tryNextCandidate();
    // A website's feeds: the best one so far (most episodes) - reported
    // when all are checked
    FeedProbeResult m_best;
    bool m_hasBest = false;
    void finishDiscovered();
    // For a website: the next feed, else the best result (or "no feed");
    // false if the address was no website with feeds
    bool nextOrBest();
};

#endif // FEEDPROBE_H

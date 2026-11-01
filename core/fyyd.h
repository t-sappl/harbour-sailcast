// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FYYD_H
#define FYYD_H

#include <functional>

#include <QByteArray>
#include <QJsonObject>
#include <QJsonValue>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QUrl>
#include <QUrlQuery>

#include "apicache.h"

class QNetworkAccessManager;
class QNetworkReply;

// One category of fyyd's tree (two levels: parentId 0 = top level)
struct FyydCategory
{
    int id = 0;
    int parentId = 0;
    QString slug;
    QString name;       // English
    QString nameDe;     // German
};

// A podcast as fyyd describes it
struct FyydPodcast
{
    int id = 0;
    QString title;
    QString subtitle;
    QString author;
    QString url;            // feed address (xmlURL)
    QString link;           // website (htmlURL)
    QString imageUrl;       // original cover (imgURL)
    QString thumbUrl;       // fyyd's small copy, if any
    QString description;
    QString language;
    QList<int> categories;  // fyyd category ids
    qint64 lastPub = 0;     // Unix time of the newest episode, 0 = unknown
    int episodeCount = 0;
    int rank = 0;
    QString fyydUrl;
};

// A podcast list of a fyyd user ("collection")
struct FyydCollection
{
    int id = 0;
    int userId = 0;
    QString title;
    QString description;
    QString slug;
    QString imageUrl;
    QString url;            // page on fyyd.de
};

// An episode list of a fyyd user ("curation")
struct FyydCuration
{
    int id = 0;
    int userId = 0;
    QString title;
    QString description;
    QString slug;
    QString imageUrl;
    QString url;            // page on fyyd.de
    QString feedUrl;        // the curation as a feed (xmlURL)
    bool isPublic = true;
};

struct FyydEpisode
{
    int id = 0;
    int podcastId = 0;
    QString title;
    QString guid;
    QString link;           // episode web page
    QString enclosure;      // media address
    QString contentType;
    QString imageUrl;
    QString description;
    qint64 published = 0;   // Unix time, 0 = unknown
    int duration = 0;       // seconds
    int season = 0;
    int episodeNumber = 0;
    qint64 added = 0;       // (149) put into the curation (favedDate), 0 = unknown
};

// Read access to the fyyd podcast directory (https://fyyd.de), API 0.2
// (documentation: codeberg.org/eazy/fyyd-api). Public reads need no login.
//
// Used for Discover (126): category tree with German names, the most active
// podcasts of the last days ("hot" - not charts), podcasts of a category,
// similar podcasts (based on users' collections), collections that contain a
// podcast, and curated episode lists. Only what a request names leaves the
// phone - a category, a language, the address of one podcast.
//
// Requests run in parallel. Answers are cached (ApiCache, 24 h by default);
// when the network fails an older cached answer is used instead. A podcast
// fyyd does not know is no error: the "Ready" signal comes with id 0 and an
// empty list.
class Fyyd : public QObject
{
    Q_OBJECT
public:
    explicit Fyyd(QObject *parent = nullptr);

    ApiCache &cache() { return m_cache; }

    void categories();
    // The category tree from the cache (any age), without the network -
    // empty if it was never loaded (135, for translating keywords)
    QList<FyydCategory> cachedCategories() const;
    // Podcast search by a term (135, keywords of "Für dich"); parallel, cached
    void search(const QString &term, int count = 30);
    // Language: "de", "en" … or empty for all languages (a category is not
    // possible: fyyd ignores it, 130)
    void hot(const QString &language, int count = 30);
    void category(int categoryId, int page = 0, int count = 50);
    // Similar podcasts - by fyyd id, or by feed address (looked up first;
    // title and author find the podcast when fyyd has another address, 128)
    void similar(int podcastId, int count = 10);
    void similarForFeed(const QString &feedUrl, int count = 10, const QString &title = QString(),
                        const QString &author = QString(), const QString &website = QString());
    // A web page without scheme, "www." and trailing slash (157)
    static QString siteKey(const QString &url);
    // Collections that contain a podcast
    void podcastCollections(int podcastId, int count = 10);
    void podcastCollectionsForFeed(const QString &feedUrl, int count = 10, const QString &title = QString(),
                                   const QString &author = QString());
    void collection(int collectionId);
    // Public curations of a category, and the episodes of one curation
    void categoryCurations(int categoryId, int page = 0, int count = 50);
    void curation(int curationId);
    // One podcast by its fyyd id (149, the "Neu" tab: the curation names only
    // the id) - podcastReady with id 0 if fyyd does not know it
    void podcast(int podcastId);
    // fyyd's id of a feed (0 = unknown to fyyd): by address, else by title
    // and author (128)
    void lookup(const QString &feedUrl, const QString &title = QString(), const QString &author = QString());

    void cancelAll();

    // Same feed address? (http = https, case and a trailing slash ignored)
    static bool sameFeed(const QString &a, const QString &b);
    // Letters and digits only, lower case - for comparing titles and authors
    static QString normalized(const QString &text);

signals:
    void categoriesReady(const QList<FyydCategory> &categories);
    void searchReady(const QString &term, const QList<FyydPodcast> &podcasts);
    void hotReady(const QString &language, const QList<FyydPodcast> &podcasts);
    void categoryReady(int categoryId, const QList<FyydPodcast> &podcasts, bool more);
    // feedUrl is empty when asked by id; podcastId 0 = podcast unknown to fyyd
    void similarReady(const QString &feedUrl, int podcastId, const QList<FyydPodcast> &podcasts);
    void podcastCollectionsReady(const QString &feedUrl, int podcastId, const QList<FyydCollection> &collections);
    void collectionReady(const FyydCollection &collection, const QList<FyydPodcast> &podcasts);
    void categoryCurationsReady(int categoryId, const QList<FyydCuration> &curations, bool more);
    void curationReady(const FyydCuration &curation, const QList<FyydEpisode> &episodes);
    void podcastReady(int requestedId, const FyydPodcast &podcast);
    // byTitle: found by title and author, fyyd has another address (128)
    void lookupReady(const QString &feedUrl, int podcastId, bool byTitle);
    // request: what was asked, e.g. "hot de", "similar 1234"
    void failed(const QString &request, const QString &error);
    // Every answer as it arrived (for the CLI's --raw)
    void received(const QUrl &url, const QByteArray &body, bool fromCache);

private:
    // root = the whole answer, data = its "data" member (empty for a 404)
    typedef std::function<void(const QJsonObject &root, const QJsonValue &data)> Handler;

    void get(const QString &path, const QUrlQuery &query, const QString &request, Handler handler);
    void send(const QUrl &url, const QString &request, Handler handler, bool retried);
    void onFinished(QNetworkReply *reply, const QUrl &url, const QString &request, Handler handler, bool retried);
    void deliver(const QUrl &url, const QByteArray &body, const QString &request, Handler handler, bool fromCache);
    // Resolves the address (or title + author), then calls next(id, byTitle)
    // - id 0 if fyyd does not know it
    // website (157): the podcast's web page - a hit with the same page counts
    // too (found by title, or by a full-text search for the title)
    void lookupThen(const QString &feedUrl, const QString &title, const QString &author,
                    std::function<void(int, bool)> next, const QString &website = QString());

    QNetworkAccessManager *m_network;
    QSet<QNetworkReply *> m_replies;
    ApiCache m_cache;
};

#endif // FYYD_H

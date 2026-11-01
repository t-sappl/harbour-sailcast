// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PODCASTINDEX_H
#define PODCASTINDEX_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include "apicache.h"

class QJsonObject;
class QNetworkAccessManager;
class QNetworkReply;

// One podcast found in the Podcast Index directory
struct PodcastSearchResult
{
    qint64 id = 0;
    QString title;
    QString author;
    QString url;            // feed URL
    QString imageUrl;
    QString description;
    QString language;
    int episodeCount = 0;
    // More details for the "Add podcast" dialog (44)
    QString link;           // website
    QString ownerName;
    QStringList categories;
    qint64 lastUpdate = 0;  // Unix time of the last feed change, 0 = unknown
    bool explicitContent = false;
    QString fundingUrl;
    QString fundingMessage;
    // Trending (126)
    qint64 newestItemTime = 0;  // Unix time of the newest episode
    int trendScore = 0;
};

// Search in the Podcast Index directory (https://podcastindex.org), API 1.0.
//
// Every request is signed: headers X-Auth-Key, X-Auth-Date (Unix time) and
// Authorization = SHA-1 (hex) of key + secret + date. The app's key is built
// in from the local, unpublished file podcastindex.pri (see
// podcastindex.pri.example); without it, isAvailable() is false.
//
// One search at a time; a new search() cancels the running one. Trending
// lists (126, Discover) run in parallel to each other and to a search and
// are cached (ApiCache, 24 h; an older answer when the network fails).
class PodcastIndex : public QObject
{
    Q_OBJECT
public:
    explicit PodcastIndex(QObject *parent = nullptr);

    // The key built into this binary (empty strings if none)
    static QString builtInKey();
    static QString builtInSecret();

    void setCredentials(const QString &key, const QString &secret);
    bool isAvailable() const { return !m_key.isEmpty() && !m_secret.isEmpty(); }

    // A term "itunes:<id>" looks the podcast up by its Apple Podcasts id
    // (podcasts/byitunesid, 103) - one result or none
    void search(const QString &term, int max = 40);

    // Podcasts trending now (podcasts/trending): language "de", "en" … or
    // empty, category a Podcast Index category name ("Science") or empty
    void trending(const QString &language, const QString &category, int max = 40);
    void cancelTrending();
    // A search like search(), but in parallel to others and cached 24 h
    // (135, keywords of "Für dich"); the typed search stays search()
    void searchCached(const QString &term, int max = 40);
    // (193) One podcast by its feed address (podcasts/byfeedurl), in
    // parallel to others and cached 24 h - for "Neu" in English (Podnews
    // names only the feed): byFeedUrlFinished with one result, or none when
    // the directory does not know the address
    void byFeedUrl(const QString &feedUrl);

    ApiCache &cache() { return m_cache; }

    // The "feeds" (or single "feed") of an answer
    static QList<PodcastSearchResult> parseFeeds(const QJsonObject &root);

signals:
    void searchFinished(const QString &term, const QList<PodcastSearchResult> &results);
    void searchFailed(const QString &term, const QString &error);
    void trendingFinished(const QString &language, const QString &category,
                          const QList<PodcastSearchResult> &results);
    void trendingFailed(const QString &language, const QString &category, const QString &error);
    // searchCached(): category is empty, language is the term
    void searchCachedFinished(const QString &term, const QList<PodcastSearchResult> &results);
    void searchCachedFailed(const QString &term, const QString &error);
    void byFeedUrlFinished(const QString &feedUrl, const QList<PodcastSearchResult> &results);
    void byFeedUrlFailed(const QString &feedUrl, const QString &error);

private:
    QNetworkReply *get(const QUrl &url);
    void send(const QString &term, int max);
    void onFinished(QNetworkReply *reply, const QString &term, int max);
    // term: a searchCached() request (language and category then unused)
    void sendTrending(const QUrl &url, const QString &language, const QString &category, bool retried,
                      const QString &term = QString());
    void onTrendingFinished(QNetworkReply *reply, const QUrl &url, const QString &language,
                            const QString &category, bool retried, const QString &term);
    void deliverParallel(const QString &language, const QString &category, const QString &term,
                         const QList<PodcastSearchResult> &results);
    static QString errorMessage(QNetworkReply *reply, int status, const QByteArray &body);

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_key;
    QString m_secret;
    // One automatic retry on a server or connection error (59)
    bool m_retried = false;
    QSet<QNetworkReply *> m_trendingReplies;
    ApiCache m_cache;
};

#endif // PODCASTINDEX_H

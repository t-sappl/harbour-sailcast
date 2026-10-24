// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PODCASTINDEX_H
#define PODCASTINDEX_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

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
};

// Search in the Podcast Index directory (https://podcastindex.org), API 1.0.
//
// Every request is signed: headers X-Auth-Key, X-Auth-Date (Unix time) and
// Authorization = SHA-1 (hex) of key + secret + date. The app's key is built
// in from the local, unpublished file podcastindex.pri (see
// podcastindex.pri.example); without it, isAvailable() is false.
//
// One search at a time; a new search() cancels the running one.
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

signals:
    void searchFinished(const QString &term, const QList<PodcastSearchResult> &results);
    void searchFailed(const QString &term, const QString &error);

private:
    void send(const QString &term, int max);
    void onFinished(QNetworkReply *reply, const QString &term, int max);

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    QString m_key;
    QString m_secret;
    // One automatic retry on a server or connection error (59)
    bool m_retried = false;
};

#endif // PODCASTINDEX_H

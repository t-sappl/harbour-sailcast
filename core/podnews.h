// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PODNEWS_H
#define PODNEWS_H

#include <QList>
#include <QObject>
#include <QString>

#include "apicache.h"

class QNetworkAccessManager;
class QNetworkReply;

// One show of Podnews' "New Podcasts" (193): the trailer of a new or
// noteworthy podcast, with the show's own feed
struct PodnewsTrailer
{
    QString id;             // Podnews' guid of the trailer
    QString title;          // the show
    QString author;
    QString feedUrl;        // <source url>: the show's own feed
    QString link;           // the show's web page
    QString imageUrl;
    QString genre;          // Apple's name ("History", "Society & Culture"), may be empty
    QString description;    // plain text
    QString audioUrl;       // the trailer (via Podnews)
    int duration = 0;       // seconds, 0 = unknown
    qint64 published = 0;   // seconds since the epoch, 0 = unknown
};

// Podnews' "New Podcasts - trailers for new and noteworthy shows"
// (https://newpodcasts.net), mostly English: one public RSS feed, curated
// by Podnews from submissions and its newsletter, updated daily. "Neu" in
// English (193), as Hörsuppe's "Nullnummern" on fyyd are for German.
//
// No key; cached as the directory APIs (ApiCache, 6 h; an older answer when
// the network fails).
class Podnews : public QObject
{
    Q_OBJECT
public:
    explicit Podnews(QObject *parent = nullptr);

    static QString feedUrl();

    void trailers();
    ApiCache &cache() { return m_cache; }

    // The items of the feed, newest first (exposed for tests and the CLI)
    static QList<PodnewsTrailer> parse(const QByteArray &xml);

signals:
    void trailersReady(const QList<PodnewsTrailer> &trailers);
    void failed(const QString &error);

private:
    void send(bool retried);
    void onFinished(QNetworkReply *reply, bool retried);

    QNetworkAccessManager *m_network;
    QNetworkReply *m_reply = nullptr;
    ApiCache m_cache;
};

#endif // PODNEWS_H

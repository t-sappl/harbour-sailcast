// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDTYPES_H
#define FEEDTYPES_H

#include <QList>
#include <QString>

// Plain data structures shared by the feed parser, the database and the
// importers. No Qt object model, so they can be copied between threads.

struct ParsedChapter
{
    qint64 startMs = 0;
    QString title;
    QString href;
    QString image;
};

struct ParsedEpisode
{
    QString guid;           // falls back to the media URL if the feed has none
    QString title;
    QString description;    // plain description / summary
    QString descriptionHtml; // content:encoded or Atom content, if present
    QString link;
    QString url;            // media URL (enclosure)
    QString mimeType;
    qint64 fileSize = 0;
    qint64 published = 0;   // seconds since the epoch, 0 = unknown
    int duration = 0;       // seconds, 0 = unknown
    QString imageUrl;       // episode image (itunes:image), may be empty
    QString chaptersUrl;    // podcast:chapters (JSON), loaded later
    QList<ParsedChapter> chapters; // Podlove Simple Chapters in the feed
    int season = 0;         // itunes:season / podcast:season, 0 = none (83)
    int episodeNumber = 0;  // itunes:episode / podcast:episode, 0 = none (83)
};

// podcast:funding (83): a link to support the podcast
struct ParsedFunding
{
    QString url;
    QString title;
};

struct ParsedFeed
{
    QString title;
    QString link;
    QString description;
    QString author;
    QString coverUrl;
    QString newUrl;         // itunes:new-feed-url
    QList<ParsedFunding> funding; // podcast:funding (83)
    QList<ParsedEpisode> episodes;
    bool repaired = false;  // broken XML was repaired completely (see FeedParser)

    bool isEmpty() const { return title.isEmpty() && episodes.isEmpty(); }
};

#endif // FEEDTYPES_H

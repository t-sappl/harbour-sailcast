// SPDX-License-Identifier: GPL-3.0-or-later
//
// (296) Made-up listening log for screenshots and demos. Pure QtCore: the
// caller gives the podcasts (with weights and the episodes' lengths) and gets
// one entry per day and episode, the same shape as the database's listening
// log. The same seed gives the same log.

#ifndef SAILCAST_DEMOSTATS_H
#define SAILCAST_DEMOSTATS_H

#include <QDate>
#include <QList>
#include <QString>

namespace DemoStats {

struct Episode
{
    qint64 id = 0;
    int duration = 0;       // seconds, 0 = unknown
};

struct Podcast
{
    qint64 id = 0;
    QString title;
    double weight = 0;      // relative share of the listening time, 0 = never
    QList<Episode> episodes;    // oldest first
};

struct Entry
{
    int day = 0;            // yyyyMMdd
    qint64 episodeId = 0;
    qint64 podcastId = 0;
    QString podcastTitle;
    int seconds = 0;        // wall clock
    int mediaSeconds = 0;   // episode time (more at higher speed)
};

struct Options
{
    QDate today;            // the log ends the day before
    int months = 12;
    double hoursPerDay = 1.5;   // average on a day with listening
    unsigned seed = 1;
};

QList<Entry> generate(const QList<Podcast> &podcasts, const Options &options);

// Weights file: "Title = number" per line, "#" starts a comment. A title
// matches a podcast exactly (ignoring case), else as part of its title.
// Returns the lines that matched nothing in `unmatched`.
void applyWeights(QList<Podcast> *podcasts, const QString &text, QStringList *unmatched);
QString weightsTemplate(const QList<Podcast> &podcasts);

// A spread that looks like real listening: a few favourites, a middle, a tail
void defaultWeights(QList<Podcast> *podcasts, unsigned seed);

} // namespace DemoStats

#endif

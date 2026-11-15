// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef HTMLEXPORT_H
#define HTMLEXPORT_H

#include <QList>
#include <QString>
#include <QStringList>

// (294) HTML export of the subscriptions: ONE self-contained file to share
// (as in AntennaPod), readable without internet. Folders are cards,
// podcasts are rows that unfold (<details>) to the details, the last
// episodes (with chapters and bookmarks) and the links. The only script
// is optional: tick podcasts and save them as OPML; without it the OPML
// links of the folders still work. Only QtCore, so the CLI uses it, too.
namespace HtmlExport {

struct Episode
{
    QString title;
    QString date;            // formatted by the caller
    QString duration;        // "52 min", empty = unknown
    bool played = false;
    QString description;     // plain text or HTML, empty = none
    QStringList chapters;    // "04:10 The experiment"
    QStringList bookmarks;   // "12:30 Check the source"
};

struct Podcast
{
    QString title;
    QString author;
    QString language;       // "de", empty = unknown
    QStringList categories; // "Science/Astronomy"
    QString description;    // plain text or HTML (the export reduces it to text)
    QString link;           // website
    QString feedUrl;
    QString rhythm;         // "weekly", empty = unknown
    int episodeCount = 0;
    QString coverDataUri;   // "data:image/jpeg;base64,...", empty = none (initial)
    qint64 listenedSeconds = 0;  // only with Options::statistics
    int listenedEpisodes = 0;
    QList<Episode> episodes;     // the last ones, empty = none
};

// (294) The listening statistics block at the top, values formatted by the
// caller; level 0 = none, 1 = totals, 2 = everything
struct Bar
{
    QString label;
    QString value;
    double share = 0;       // 0..1 of the largest
};

struct Statistics
{
    int level = 0;
    QString title;          // "Listening statistics (since 2 Nov 2026)"
    QString total, totalLabel;
    QString saved, savedLabel;
    QString episodes, episodesLabel;
    QString perDay, perDayLabel;        // level 2
    QString streak, streakLabel;        // level 2
    QString topTitle;                   // "Top podcasts"
    QList<Bar> top;
    QString weekdaysTitle;              // "Weekdays"
    QList<Bar> weekdays;                // 7, label "Mo"
    QString note;                       // "Only data of this phone"
    // (297) Explanations and the parts of the statistics page (level 2)
    QString totalsNote;                 // under the totals (level 1 and 2)
    QString activeDays, activeDaysLabel;
    QString streakNote;                 // under the series box
    QString monthsTitle;                // course per month
    QList<Bar> months;                  // label "Nov"
    QString heatTitle;                  // the last year
    QList<int> heat;                    // 53 x 7, Monday first: -1 future, 0..4 level
    QList<QPair<int, QString>> heatMonths;  // week, month name
    QString categoriesTitle, categoriesNote;
    QList<Bar> categories;              // value "3 h · 25 %"
    QString recordsTitle;
    QList<QPair<QString, QString>> records;    // label, value
};

struct Folder
{
    QString name;           // empty = "Other podcasts"
    QString countText;      // "2 podcasts" (plural by the caller)
    QList<Podcast> podcasts;
};

// All texts the page shows, in the language of the app (filled by the caller);
// the English defaults are used for what stays empty
struct Labels
{
    QString lang;            // "de"
    QString title;           // "My podcasts"
    QString subtitle;        // "3 podcasts in 1 folder · exported 11 Nov 2026"
    QString intro;           // first sentence of the explanation at the top, empty = none
    QStringList introPoints; // what the buttons do
    QString privateNote;     // "2 private feeds ... are not part of this page", empty = none
    QString opmlAll;         // "All podcasts (OPML)"
    QString opml;            // "OPML" (folder header)
    QString otherPodcasts;   // "Other podcasts"
    QString category, language, episodes, rhythm, listened;     // "Category" ...
    QString subscribe, feed, website;
    QString fyyd;            // per podcast: search on fyyd.de
    QString podcastIndex;    // per podcast: search on podcastindex.org
    QString spotify, apple;  // per podcast: button texts "Spotify" / "Apple Podcasts" (search by name)
    QString discover;        // footer: "Find more podcasts"
    QString sourceCode;      // pill in the promo box: "Sailcast on GitHub"
    QString sailfishOs;      // pill in the promo box: "Sailfish OS"
    QString promo;           // footer: what Sailcast and Sailfish OS are (for people who do not know them)
    QString about;           // footer: "Sailcast finds podcasts with fyyd and Podcast Index."
    QString lastEpisodes;    // "Last episodes"
    QString moreEpisodes;    // "More episodes (%1)"
    QString chapters, bookmarks;   // + " (n)"
    QString showNotes;       // "Description"
    QString selected;        // "%1 selected"
    QString all, none, saveSelection;
    QString madeWith;        // "Made with Sailcast for Sailfish OS"
    QString hour, minute;    // "h", "min"
};

struct Options
{
    bool folders = true;        // false: one list without cards
    bool descriptions = true;
    bool statistics = false;    // time listened per podcast
    QString iconDataUri;        // small app icon next to the title, empty = none
    int visibleEpisodes = 5;    // shown directly, the rest folded under "More episodes"
    Statistics stats;
    Labels labels;
};

// The whole page (UTF-8, <!DOCTYPE html>)
QString build(const QList<Folder> &folders, const Options &options);

// OPML of folders (the page embeds them as download links)
QString opmlOf(const QList<Folder> &folders);

// Text of a description: tags removed, entities decoded, shortened at a word
QString plainText(const QString &html, int maxLength);

// Escaping for text and attribute values
QString escape(const QString &text);

} // namespace HtmlExport

#endif // HTMLEXPORT_H

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SHOWNOTESCHAPTERS_H
#define SHOWNOTESCHAPTERS_H

#include <QList>
#include <QString>
#include <QStringList>

#include "feedtypes.h"

// (344, preparation for 1.5) Chapters from the time stamps in an episode's
// show notes ("00:00 Intro", "1. (03:15) Topic", "Q&A – 45:10"), rules v3
// (DESIGN-INTERNAL "Plan: Shownotes-Kapitel-Parser"). A pure function: no
// database, no network. Not used by the app yet - only the CLI command
// "shownotes-chapters" and the tests in tools/chapterstest call it.
//
//  - lines from text or HTML (</p>, <br>, <li>, </div> ... break a line),
//    "|" splits a line only in front of a time stamp
//  - a time stamp at the start of a line (after a bullet, "1.", "1)", "#2",
//    "Kapitel 1:", in brackets) or at the end only after a bracket or a
//    separator ("(12:30)", "[12:30]", " – 12:30", "Title: 12:30");
//    "from – to" keeps the start; milliseconds are dropped
//  - blocked: "Uhr", "h", "AM"/"PM", "min", "Minuten" or a full stop right
//    after it; seconds and minutes of H:MM:SS 00-59, MM:SS up to 199:59
//  - a title needs, without URLs, two letters or digits, one of them a letter
//  - per group (start / end) the longest rising series of times; the group
//    with more chapters wins (start on a tie); at least 3 chapters, the first
//    at most 10 minutes in; chapters after the end (duration known) dropped
namespace ShownotesChapters {

QList<ParsedChapter> parse(const QString &notes, int durationSeconds = 0, QStringList *log = nullptr);

// The lines the parser looks at (HTML turned into text), for the CLI
QStringList lines(const QString &notes);

}

#endif // SHOWNOTESCHAPTERS_H

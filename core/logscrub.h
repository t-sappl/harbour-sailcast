// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef LOGSCRUB_H
#define LOGSCRUB_H

#include <QList>
#include <QString>

// (371, Thomas 20 Nov) "Replace names" on the log page: titles of podcasts and
// episodes, their folder names and web addresses become "Podcast #12" /
// "Episode #345" (the database ids - lines stay comparable, the user can look
// them up), so a shared log tells nothing about what someone listens to.
// Works on the text only; the log file itself is never changed.
namespace LogScrub {

struct Name {
    QString text;          // as it may appear in the log (title, folder, address)
    QString replacement;   // "Podcast #12", "Episode #345", "‹feed of Podcast #12›"
    bool address = false;  // a web address: replaced wherever it appears
};

// 1. the known addresses, 2. the known names (whole words only, at least
// three characters; a one-word name that is also a word of the app's own log
// lines - "Update", "Player" - is left out, it would hide what happened), 3. every other web address down to its domain
// ("https://…podigee.io/…"), 4. paths in the home folder or on the SD card
// after the standard folders ("~/Music/Podcasts/Podcast #12/…"), 5. folder
// names in lines that name a folder (unsubscribed podcasts are no longer
// known).
// used: the replacements that were made (each once), for "names replaced:
// 12 podcasts, 3 episodes"
QString scrub(const QString &text, const QList<Name> &names, QStringList *used = nullptr);

} // namespace LogScrub

#endif // LOGSCRUB_H

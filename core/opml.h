// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef OPML_H
#define OPML_H

#include <QList>
#include <QString>

struct OpmlEntry
{
    QString title;
    QString url;        // feed URL (xmlUrl)
    QString htmlUrl;
    QString folder;     // name of the enclosing outline without xmlUrl (45), empty = none
};

// Reading and writing OPML subscription lists (OPML 1.0 and 2.0). An
// outline without xmlUrl that contains feeds is a folder (as written by
// AntennaPod, Podcast Addict and Sailcast since 45); deeper nesting is
// flattened into the outermost folder.
namespace Opml {

QList<OpmlEntry> read(const QString &path, QString *error = nullptr);
bool write(const QString &path, const QList<OpmlEntry> &entries, QString *error = nullptr);

} // namespace Opml

#endif // OPML_H

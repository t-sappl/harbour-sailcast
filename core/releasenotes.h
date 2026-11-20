// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef RELEASENOTES_H
#define RELEASENOTES_H

#include <QList>
#include <QString>

// Release notes and the changelog as plain text (347). One source:
// CHANGELOG.md - tools/release_notes.py makes the GitHub release text from
// its section, the app shows the GitHub text of a new version (update) and
// the installed CHANGELOG.md for its own and older versions. Pure
// functions, also in the Jolla Store build (unit tests: tools/updatetest).
namespace ReleaseNotes {

// (351) One line of the notes as the About page lays it out (heading,
// item with a hanging indent, ...); the text is plain (no markup)
struct Block
{
    enum Kind { Heading, Item, SubItem, Numbered, Paragraph, Blank };
    Kind kind;
    QString text;
};

struct Section
{
    QString version;   // "1.4.1"
    QString date;      // "2026-11-17", may be empty
    QString text;      // plain(), without the "## " line
    QList<Block> blocks;   // (351) the same as blocks()
};

// "v1.5.0", "1.5", "1.4.1 (build 345)" -> {1,5,0}, {1,5,0}, {1,4,1}; empty = no version
QList<int> parseVersion(const QString &text);
// <0, 0, >0 (0 also when one side is no version)
int compareVersions(const QString &a, const QString &b);

// Markdown as plain lines: headings become paragraphs, "- " becomes "•",
// **bold**, `code` and links become their text, wrapped lines are joined.
// Left out: the section "Install" (the app installs itself), "---", the
// credit line and "Full Changelog".
QString plain(const QString &markdown);

// (351) The same lines as blocks; plain() is made from them
QList<Block> blocks(const QString &markdown);

// (365) The GitHub release text carries the German notes in a block
//   <details>
//   <summary>Deutsch</summary>
//   ...
//   </details>
// (folded on GitHub; tools/release_notes.py adds it from CHANGELOG.de.md).
// german: the inside of that block - or the whole text without it when
// there is none; else the text without the block.
QString localized(const QString &markdown, bool german);

// The "## <version> (<date>)" sections of CHANGELOG.md, newest first
QList<Section> sections(const QString &changelog);

} // namespace ReleaseNotes

#endif // RELEASENOTES_H

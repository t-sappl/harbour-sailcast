// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FEEDPARSER_H
#define FEEDPARSER_H

#include <QByteArray>
#include <QString>

#include "feedtypes.h"

// Parses RSS 2.0, RSS 1.0 and Atom podcast feeds with QXmlStreamReader.
// Understands the iTunes tags (image, duration, author, new-feed-url),
// content:encoded, Media RSS as an enclosure fallback, Podlove Simple
// Chapters and the URL of podcast:chapters (Podcasting 2.0).
//
// Pure function without network or database access, so it can run in a
// worker thread (see FeedUpdater) and be used by the command line tool.
namespace FeedParser {

// Returns the parsed feed. Episodes without a media URL are skipped.
//
// Broken XML is repaired when possible: if the first pass fails, the data
// is cleaned up (see sanitize()) and parsed again; ParsedFeed::repaired is
// then set. If the XML is still broken, the better of the two partial
// results is returned and *error is set - callers store it as a warning
// (a feed without title and episodes counts as failed).
ParsedFeed parse(const QByteArray &data, QString *error = nullptr);

// Repairs the most common faults of hand-made feeds, outside of CDATA
// sections and comments:
// - HTML entities that XML does not know (&nbsp;, &auml;, &ndash;, ...)
//   become numeric character references (&#160;)
// - unknown entity names and bare "&" become "&amp;" (shown as text)
// - control characters that XML forbids are removed
// Works on bytes, so it is correct for UTF-8 and other ASCII-compatible
// encodings (not for UTF-16 feeds, which are very rare).
QByteArray sanitize(const QByteArray &data);

} // namespace FeedParser

#endif // FEEDPARSER_H

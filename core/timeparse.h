// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TIMEPARSE_H
#define TIMEPARSE_H

#include <QString>

// Lenient parsers for the date and time formats found in real-world feeds.
namespace TimeParse {

// "3723", "3723.5", "62:03", "1:02:03", "01:02:03.500" -> milliseconds,
// -1 if the text is not a valid duration
qint64 durationMs(const QString &text);

// Same, in whole seconds (0 if invalid)
int durationSeconds(const QString &text);

// RFC 822 / 2822 dates as used in RSS ("Fri, 21 Nov 1997 09:55:06 -0600",
// also without weekday or seconds, two-digit years and zone names like
// "GMT" or "PST") and ISO 8601 dates as used in Atom. Returns seconds since
// the epoch, 0 if the text cannot be parsed.
qint64 pubDate(const QString &text);

} // namespace TimeParse

#endif // TIMEPARSE_H

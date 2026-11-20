// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef APPLOG_H
#define APPLOG_H

#include <QString>

// (351) The app's own log: every warning (C++ qWarning, QML console.warn -
// the player writes its decisions this way) also goes to a small file in
// the app data folder, so a user can send what happened without a
// terminal and journalctl ("Show log" on the About page, LogPage). The file is
// written line by line and flushed, so it survives the app being killed.
// Two files of at most 256 KB (sailcast.log, sailcast.log.1). Addresses
// are masked (UrlMask: tokens, user names; long runs of letters/digits in
// a path) and e-mail addresses removed before a line is written. The messages still go to the journal as before.
namespace AppLog {

// Installs the message handler; call once, right after the application
// object exists (the data folder needs the application name).
void install(const QString &version);

// Path of the current log file
QString path();

// (372, Thomas 20 Nov) "Keep a log" (on unless switched off): off closes
// and deletes both files and leaves a marker file (log-off) in the data
// folder, so the next start writes nothing either - read before the
// database exists. On again starts a new file.
bool isEnabled();
void setEnabled(bool on);

// (354) The last `maxLines` lines of both files (older first), for the
// Log page
QString tail(int maxLines);

// (354) Writes `text` (the possibly edited log of the Log page) into
// `directory` as sailcast-log-<date>-<time>.txt; returns the path, empty
// on failure
QString writeText(const QString &directory, const QString &text);

// Masks a line as it is written to the file (for tests)
QString clean(const QString &line);

// (365) Known harmless messages that stay out of the file (Silica's
// RemorsePopup on this OS version, D-Bus at exit, libpng colour profiles)
bool ignored(const QString &message);

// (365) How a kept message is marked: the player's reports are "I"
// (information), its problems and everything else stay "W"/"E"
char kindOf(QtMsgType type, const QString &file, const QString &message);

} // namespace AppLog

#endif // APPLOG_H

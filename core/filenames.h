// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FILENAMES_H
#define FILENAMES_H

#include <QString>

struct EpisodeRow;

// Readable, portable file and folder names for downloads and the archive.
// The names have to work on FAT/exFAT (SD cards), NTFS and in cloud storage
// (rclone), so the characters forbidden there are replaced.
namespace FileNames {

// Replaces / \ : * ? " < > | and control characters with "-", collapses
// white space, removes leading/trailing dots and spaces and cuts to
// maxLength characters. Never returns an empty string.
QString safe(const QString &name, int maxLength = 120);

// File extension (without dot) for a media file, from the URL path or the
// MIME type; "mp3" if neither tells
QString extension(const QString &url, const QString &mimeType);

// "2021-06-19 S01E01 - Auf dem Rummel.mp3" (date of publication in UTC)
QString forEpisode(const EpisodeRow &episode);

} // namespace FileNames

#endif // FILENAMES_H

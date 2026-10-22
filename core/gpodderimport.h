// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GPODDERIMPORT_H
#define GPODDERIMPORT_H

#include <QString>

class Database;

struct GpodderImportResult
{
    bool ok = false;
    QString error;
    int podcastsAdded = 0;
    int podcastsExisting = 0;
    int episodesAdded = 0;
    int episodesMerged = 0;    // already known, playback state merged
    int played = 0;
    int inProgress = 0;
};

// One-off import from the database of gpodder-sailfish (gPodder 4,
// SQLite file "Database" written by minidb, tables PodcastChannel and
// PodcastEpisode). Takes over subscriptions, episodes, played state,
// playback positions and chapters. Downloaded files are not taken over.
//
// gPodder has no separate "played" flag: is_new = 0 means played (or
// marked as old). An episode also counts as played when its position is
// at the end (same rule as gPodder's is_finished()).
//
// The file is opened read-only. Under Sailjail the app cannot read
// gpodder-sailfish's data folder, so the file has to be copied somewhere
// readable first (e.g. Downloads).
namespace GpodderImport {

GpodderImportResult run(Database *database, const QString &path);

} // namespace GpodderImport

#endif // GPODDERIMPORT_H

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EPISODEACTION_H
#define EPISODEACTION_H

#include <QString>

// One episode action in the format of the gpodder.net API, which the
// Nextcloud app "GPodder Sync" (gpoddersync) uses as well:
//   {"podcast": feed URL, "episode": media URL, "guid": ..., "action":
//    "play" | "new" | "download" | "delete", "timestamp":
//    "2026-10-09T05:20:00" (UTC), "started": s, "position": s, "total": s}
// Positions are in seconds. Used for the outbox (local actions not yet
// uploaded), for actions received from the server that could not be
// matched to an episode yet, and for the transfer itself.
struct EpisodeAction
{
    qint64 id = 0;          // row id in sync_outbox / sync_pending, 0 = not stored
    QString podcastUrl;
    QString episodeUrl;
    QString guid;
    QString action;         // lower case
    qint64 timestamp = 0;   // seconds since the epoch (UTC)
    int started = -1;       // -1 = not set (only "play" has positions)
    int position = -1;
    int total = -1;
};

#endif // EPISODEACTION_H

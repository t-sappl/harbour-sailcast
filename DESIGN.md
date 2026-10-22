# Sailcast – Design

This document describes the **architecture**, the **conventions** and the **design decisions** of Sailcast: what is solved the way it is and why – including rejected alternatives.

---

## 1. Code conventions

- **Code and comments entirely in English.**
- Every source file starts with `SPDX-License-Identifier: GPL-3.0-or-later`.
- **Qt 5.6 workarounds** that newer Qt deprecates are wrapped in `#if QT_VERSION < …` so the desktop CLI builds without warnings.
- **Qt 5.6 compatibility:** Sailfish OS ships Qt 5.6. The core must not use newer APIs (e.g. `QDateTime::fromSecsSinceEpoch`, `QOverload`, `QRandomGenerator`, `Qt::endl`), even though the CLI is built against Qt 5.15 on the desktop.
- **C++11** (`CONFIG += c++11`), no C++14 library features such as `std::make_unique`.
- **Log output:** only real problems, as `qWarning` / `console.warn`. No info logs.
- QML conventions as in Sailwave: Theme values instead of fixed pixels, Silica components, every delete action with a countdown.

## 2. Architecture

```
core/   libsailcast   QtCore, QtNetwork, QtSql – no QML
  database      SQLite schema, queries, migrations
  feedparser    XML -> ParsedFeed (pure function, thread-safe)
  feedupdater   network, conditional requests, redirects, parse in worker thread
  opml          OPML read/write
  gpodderimport one-off import from gpodder-sailfish
  syncengine    sync with Nextcloud GPodder Sync / gpodder.net API
  downloadmanager  episode downloads (resume, redirects, expired links)
  archiver      archive folders, import, playback sources
  chaptersloader   podcast:chapters (JSON)
  filenames     readable, portable file names
cli/    sailcast-cli  desktop test tool, uses core/ only
src/    app start-up, AppCore (core for QML), list models, Secrets, QML network access
qml/    Silica user interface, player
```

`core/core.pri` is included by both `harbour-sailcast.pro` and `cli/sailcast-cli.pro`, so the desktop tool tests exactly the code that runs on the phone.

### Why C++ instead of Python (gPodder 4)

gpodder-sailfish keeps all logic in Python (PyOtherSide). It loads every podcast and episode into Python objects at start-up, converts each episode into a dictionary for every list and fills QML `ListModel`s from JavaScript; feed parsing is serialized by the GIL. Sailcast keeps the data in SQLite and queries only what a page shows (C++ list models in stage 4), so start-up and long episode lists do not depend on the size of the archive.

## 3. Database

- SQLite through QtSql, one named connection, used only from the main thread.
- `PRAGMA journal_mode=WAL`, `synchronous=NORMAL`, `foreign_keys=ON` (deleting a podcast deletes its episodes and chapters).
- Schema version in `PRAGMA user_version`; `Database::migrate()` upgrades step by step.
- Text columns are `NOT NULL DEFAULT ''` – no NULL/empty distinction in the code. Exception: `podcasts.last_error` (NULL = no error).
- **Every string bound to a query goes through `sqlText()` (`core/sqltext.h`):** QtSql binds a null `QString` (`QString()`, e.g. a feed without author) as SQL NULL, not as `''`, which violates the NOT NULL columns.
- **Played vs. new:** `played` (listened to or marked) and `is_new` (arrived since the subscription) are separate. On the first update of a new subscription only the newest episode is "new"; the back catalog is unplayed but not new.
- **Counts are computed, not stored:** unplayed/new per podcast come from one grouped query with the index `episodes(podcast_id, played)`.
- **Episode image:** `image_url` is the episode's own image; queries also return `COALESCE(NULLIF(image_url, ''), cover_url)` as display image.
- **Chapters** live in their own table; `episodes.chapter_count` is kept for lists so they do not need a join. Chapters loaded from a podcast:chapters JSON file are kept when the feed changes, as long as the feed itself has no chapters for the episode.
- **Unchanged episodes are not written:** every episode stores an MD5 of its feed data (`meta_hash`); a feed update only writes episodes whose hash changed.
- **Duplicate feeds:** `http://` and `https://` of the same URL count as one subscription (OPML exports often contain both).

## 4. Feed updates

- Up to 4 feeds in parallel (configurable), conditional requests with `If-None-Match` / `If-Modified-Since`: unchanged feeds answer 304 and cost no parsing.
- Redirects are followed by hand (max. 5). If every step was permanent (301/308), the stored URL is updated. `itunes:new-feed-url` also updates it.
- Parsing in a `QThreadPool` (2 threads); the database is written in the main thread only.
- 30 s without data aborts a download ("Timeout"); Qt 5.6 has no transfer timeout.
- Errors are stored per podcast (`last_error`) and shown, the subscription is kept.

### Connection errors

Errors of the connection rather than of the feed (Qt 5.6 on Sailfish OS reports "Network session error" for some requests while the network session is being set up at app start; temporary failures) are retried twice (after 2 s and 5 s) and never stored as the feed's error – the podcast list only shows real feed problems (HTTP errors, broken XML, login).

### Podcast search (since 31)

Through the **Podcast Index** directory (`core/podcastindex.*`, API 1.0, `search/byterm`): open, free, current, with Podcasting 2.0 data. Every request is signed with the app's key: headers `X-Auth-Key`, `X-Auth-Date` (Unix time) and `Authorization` = SHA-1 (hex) of key + secret + date. **The key is built in at build time** from the private file `podcastindex.pri` next to the .pro (template `podcastindex.pri.example`, included by `core.pri` when present) – the secret is stored **hex-encoded** (`PODCASTINDEX_SECRET_HEX`, since 38): Podcast Index secrets contain `#` and `$`, which qmake (comment), make and the shell would change on the way into the compiler – the first real key arrived as "no key built in"; users enter nothing; the file must never be published (keep it out of the repository). Without it the app builds and the search page says that no key is built in. The CLI also takes `PODCASTINDEX_KEY`/`PODCASTINDEX_SECRET` from the environment for testing (`search TERM`).

UI: own page "Podcast suchen" (Podcasts pulley), search 0.8 s after typing stops; results: cover, title, author, episodes, language, "abonniert". A tap opens "Add podcast" with the feed address, where it is checked and subscribed as any typed address (one path for both). Rejected: gpodder.net's directory (outdated, gpoddersync has none); iTunes Search (no Podcasting 2.0 data, Apple terms); a key entered by the user (built in instead). Later: Discover page (trending per language/category), search by person.

### Checking a feed before subscribing (since 25)

"Add podcast" checks the address first (`FeedProbe`, core): loaded (redirects followed – no credentials are sent, so this is safe here), parsed with the normal `FeedParser`, nothing stored. The dialog shows title, author, cover and the number of episodes and allows "Subscribe" only for a working feed – or for one that answers 401/403 (premium feeds; the login is set on the podcast page afterwards). A website is accepted when it names its feed (`<link rel="alternate" type="application/rss+xml|atom+xml" href="…">`); that feed is subscribed. `itunes:new-feed-url` is followed. An address already subscribed is recognised (same matching as everywhere: http = https, case-insensitive). A web address in the clipboard is filled in when the dialog opens (as in Sailfeed). Before 25 every typed address was stored first – mistyped or dead addresses stayed as broken entries. The CLI offers the same check as `check URL`.

### Feeds with login

Some premium feeds (e.g. podcaster.de) require HTTP Basic Auth.

- The **user name** is stored in the database (`podcasts.auth_user`), the **password never**: the app keeps it in Sailfish Secrets, the desktop CLI in its own credentials file (testing only). `FeedUpdater::setPassword()` holds it in memory.
- The `Authorization` header is only sent to the **host of the stored feed URL**. A redirect to another host gets no credentials.
- 401/403 answers get clear messages: "Login required", "Password missing" or "Wrong user name or password".
- Sync services (Nextcloud gpoddersync, gpodder.net) only transfer feed URLs, never credentials – the login has to be entered once on every device.
- **App (since 18):** the podcast page offers "Login" when the feed has one or answered 401/403. The password is stored in Sailfish Secrets as `feed-<podcast id>` and handed to the feed updater and the download manager on every start, **before** the start-up update (the update waits for Secrets, at most 5 s). Unsubscribing deletes the secret.

**Private feeds with a token in the URL** (e.g. Patreon: `https://www.patreon.com/rss/<creator>?auth=<token>`) need no special handling for updates – the token is part of the feed URL. Consequences:

- The token is synchronized like any feed URL (subscription list on the sync server, OPML exports, all devices). With an own server this is wanted: the feed appears on all devices without entering anything.
- Such URLs are shown masked in the user interface and in messages (stage 4).
- The media links in such feeds are often signed and expire. Feed updates refresh changed episode URLs (the episode's `meta_hash` changes), but downloads and archiving must update the feed right before downloading (stage 3).

### Broken XML

XML knows only five named entities (`&amp;`, `&lt;`, `&gt;`, `&quot;`, `&apos;`). Hand-made feeds sometimes contain raw HTML entities (`&nbsp;`, `&auml;`, `&ndash;`), a bare `&` or control characters; QXmlStreamReader – like any conforming XML parser, including the expat parser of gPodder – stops at the first such fault. Without handling, all episodes after that spot would be missing **silently**.

- Valid feeds are parsed once, without any extra cost.
- Only if parsing fails, `FeedParser::sanitize()` repairs the data and it is parsed again: known HTML entities become numeric references, unknown names and bare `&` become `&amp;` (the text stays readable), forbidden control characters are removed. CDATA sections and comments are left untouched. The repair works on bytes – correct for UTF-8 and other ASCII-compatible encodings.
- A feed that is still broken (e.g. mismatched tags) is stored with what could be read, and the podcast gets a warning in `last_error` ("… – only N episodes read"). Existing episodes are never deleted by a partial feed.

Rejected: always running the repair first – it would cost a full copy of every feed on every update, for a fault that only a few feeds have. Rejected: a lenient HTML parser – no such parser in Qt 5.6, and an own one would be a large piece of code for a rare problem.

## 5. Sync (stage 2)

Subscriptions and listening state are synchronized with the other podcast apps of the user (e.g. AntennaPod on Android) through one of two server types:

| | Nextcloud "GPodder Sync" (gpoddersync) | gpodder.net API (gpodder.net, mygpo, goPodder, …) |
|---|---|---|
| Login | Basic Auth with a Nextcloud app password | Login session, Basic Auth |
| Endpoints | `/index.php/apps/gpoddersync/subscriptions`, `…/subscription_change/create`, `…/episode_action`, `…/episode_action/create` | `/api/2/subscriptions/{user}/{device}.json`, `/api/2/episodes/{user}.json`, … |
| Devices | none | device registration required |

gpoddersync implements only this subset of the gpodder.net API with its own paths. Both use the **same episode action format** (`podcast`, `episode`, `guid`, `action`, `timestamp`, `position`, `started`, `total`) and the same subscription change model (add/remove lists plus a timestamp).

**Structure:** a common sync core (outbox of local actions, "since" timestamps, matching episodes by URL and GUID, newest timestamp wins, first sync takes over the server state) and a small backend interface with two implementations. Everything that differs – paths, login, devices – stays inside the backend.

**Functional differences** apart from the API: gpodder.net additionally offers a podcast directory (search, top lists, tags, suggestions), per-device subscription lists with sync groups and a web interface; gpoddersync has one subscription list per user and keeps, as far as known, only the latest action per episode instead of a history. None of this matters for syncing the listening state. Podcast search in Sailcast therefore uses a separate, current directory (stage 4), not gpodder.net.

**Default is Nextcloud:** own server instead of a third-party service; the future of gpodder.net is uncertain (announced purge of inactive accounts in 2025). The password (app password) is kept in Sailfish Secrets.

### Implementation

- **`SyncEngine`** (`core/syncengine.*`) runs the steps one after the other, asynchronously: (gpodder API: register device) → get subscription changes since the stored timestamp → apply → upload local subscription changes → load the feeds of podcasts added by the server → get episode actions → apply → upload local actions in batches of 100. The "since" timestamps are stored right after each download, so an interrupted sync continues where it stopped. Errors stop the sync; nothing local is lost (outbox).
- **Outbox:** `sync_subscriptions` (latest change per feed URL) and `sync_outbox` (only the latest action per episode – newest wins anyway). Filled only while an account is configured; actions the sync itself causes are not queued.
- **Received actions** go into `sync_pending` first and are applied from there. An action whose episode is not (yet) known stays pending – e.g. the feed of a podcast added by the sync is loaded in the same run, but a feed may also no longer contain old episodes. Pending actions are tried again after every sync and every feed update; after **90 days** without a match (episode no longer in the feed) they are dropped at the next sync (49).
- **Matching an action to an episode:** podcast by feed URL (http = https, case-insensitive), then episode by GUID, by media URL, by media URL without query string (tracking parameters change). If the podcast is not found: a unique GUID or media URL across all podcasts.
- **Newest wins:** `episodes.position_updated` is the time of the last change of the listening state (local or from the sync). An action is only applied if it is newer. "play" sets the position; within the last 10 seconds or 99 % the episode is played. "new" marks it unplayed and new. "download"/"delete" are ignored.
- **First sync** (no timestamp stored, or another account): the server's subscriptions are added here, local ones missing on the server are uploaded (union); server-side removals and local listening states are neither applied nor sent – otherwise a fresh installation could overwrite the listening state of the other devices.
- **Timestamps** in actions: ISO 8601 without zone, meaning UTC (`2026-10-09T05:20:00`); Qt would read such a string as local time, so the zone is set explicitly.
- Local actions keep only the latest per episode in the outbox. Playback (stage 3) will call `recordPlayback()` regularly; only the last position is uploaded.


## 6. Downloads (stage 3)

- **`DownloadManager`** loads up to 2 files at a time. A file is written to `<name>.part` while loading and renamed when complete; an interrupted download continues with an HTTP Range request (also after a restart). If the server ignores the Range header (200 instead of 206) the part is started over; 416 also restarts once.
- **Redirects** are followed by hand (max. 10, media URLs often go through tracking services); the feed's login (Basic Auth) is only sent to the feed's own host.
- **Expired links:** on 401/403/404/410 the feed is updated once and the download retried with the episode's current URL – signed links (e.g. Patreon) expire.
- **File names:** `YYYY-MM-DD Title.ext` (date of publication, UTC), characters forbidden on FAT/exFAT/NTFS replaced (`FileNames`). Normal downloads go to `<downloads.dir>/<Podcast>/`.
- **Clean-up** deletes downloads of played episodes last played more than `cleanup.days` days ago – never in archived podcasts and never inside the archive folder. Files deleted outside the app are noticed and forgotten.
- **Cancel** (since 18): stops a running or waiting download and deletes its `.part` file; `failed()` follows with the error "Cancelled" (the archive counts it like a failure, the app shows no message).
- **Only on Wi-Fi** (setting `downloads.wifiOnly`, app only): the connection type comes from Qt's bearer management (`QNetworkConfigurationManager`, active configurations of type WLAN or Ethernet; unknown counts as "no Wi-Fi"). A download started by hand asks first ("No Wi-Fi connection – download anyway?"), automatic downloads simply do not start. Rejected: queueing downloads until Wi-Fi comes back – needs a persistent queue and watching the connection, for little gain (the next update on Wi-Fi loads them anyway).
- **Automatic downloads** (setting `downloads.auto`, off by default): after a feed update, the new episodes of that podcast (at most 3, newest first, unplayed, flagged new). Not on the first update of a subscription (all episodes are new then – an OPML import would load one episode per podcast) and not for archived podcasts or the one being archived.
- **Clean-up in the app:** `cleanup.days` = -1 never, 0 right after listening, N days; run once at every start and by "Clean up now" / "Delete played downloads" (0 days). With 0 ("right after listening") also **immediately** whenever an episode becomes played (24) – never the file of the episode in the player (`AppCore::setCurrentEpisode`, `cleanup(…, keepEpisodeId)`); that one goes with the next clean-up. Files inside the archive folder are never deleted, also not with "Delete download".
- **Rejected:** QNetworkRequest::FollowRedirectsAttribute for media files – Qt follows redirects to other hosts with the same headers, which would send the feed login to tracking services.

## 7. Archive and cloud (stage 3)

**Goal:** keep podcasts whose feed may disappear (example: a series that ended), independent of the hoster.

- **Episodes are never deleted by feed updates** (already the case in stage 1): a vanished, shortened or broken feed does not remove known episodes, their played state, positions or chapters. A podcast can be marked **archived**: no more updates, no error messages.
- **Archive folder per podcast** in a normally accessible place (e.g. `~/Music/Podcasts/<Podcast>/`): the media files with readable names, the original feed XML, the cover and `episodes.json` with all metadata and the listening state. The folder is complete on its own – readable without Sailcast, and Sailcast can import it again.
- **Source order when playing an episode:** local file → original URL → cloud archive. The user just taps "play".
- **Cloud backup and streaming are delegated to rclone**, not built into the app:
  - Backup: `rclone copy ~/Music/Podcasts <remote>:Podcast-Archive` on the phone or the PC (optionally as a systemd timer, e.g. only at night / on Wi-Fi). The remote can be Google Drive or the same Nextcloud that is used for the sync (rclone's WebDAV remote).
  - Streaming and single downloads: `rclone serve http <remote>:Podcast-Archive --addr 127.0.0.1:8080 --read-only` as a systemd user service on the phone. Sailcast only knows the base URL ("archive address"). rclone fetches only the requested ranges, so seeking works without loading the whole file. Reachable on localhost only.

**Implementation (`Archiver`):**

- `archive()`: update the feed **without** conditional request (fresh, possibly signed media links; the raw XML arrives with `FeedUpdater::feedData` and is saved as `feed.xml`) → cover → episodes → `episodes.json`. Existing files are kept, downloads from the normal download folder are moved (copied across file systems), missing ones downloaded. A dead feed does not stop the archive – the known episodes are archived with their last URLs.
- The **folder name** is chosen once from the title and stored (`podcasts.archive_folder`), so a changed title does not create a second folder; a clash with another podcast's folder gets " (ID)". The **file name** of each episode is stored too (`episodes.archive_file`) – it is also the path in the cloud archive.
- `episodes.json` (format "sailcast-archive", version 1): podcast data (title, feed URL, link, author, description, cover file, archived flag) and per episode guid, title, published (ISO, UTC), duration, media URL, MIME type, size, file name (only if present), link, image, chapters URL, descriptions, chapters, played, position, position_updated, last_played; since 49 also `bookmarks` (position, note, created – only when there are any; still version 1, older readers ignore it). Importing an archive folder adds bookmarks that are not there yet (same position = same bookmark); changing a bookmark of an archived podcast writes its `episodes.json` again 60 s later.
- `importFolder()` uses the same code as a feed update (`applyFeed`) for the episodes, then links the files and takes over listening states that are newer than the local ones (`position_updated`).
- **Archived state:** `podcastIdsForUpdate()` skips archived podcasts; an explicit update of one podcast still works.
- **Playback sources** (`Archiver::playbackSources`): local file (if it exists) → original URL → `<archive.url>/<folder>/<file>` (percent-encoded). The player tries the next one on a playback error (stage 4).

**In the app (since 18):** podcast page → "Archive podcast" (dialog with the number of episodes, the size declared in the feed and the folder; a warning without Wi-Fi), "Cancel archiving" (cancels its downloads; archiving again continues), "Stop updates (archived)" / "Resume updates". One archive at a time (`AppCore::archivingPodcastId`). The archive folder is a setting (`archive.dir`, default `~/Music/Podcasts`; Sailjail only allows writing inside Music – a folder that cannot be written is refused). `episodes.json` of a podcast with an archive folder is written again 60 s after its listening state changed and when the app closes. Archive folders (with `episodes.json`) can be taken over on the import/export page.

**Rejected:** Google Drive API directly in the app – needs an own Google Cloud project with OAuth; private projects in the "Testing" state have refresh tokens that expire after 7 days. rclone handles the Google login itself and supports over 40 storage services, so the app needs no cloud code at all. WebDAV built into the app (Sailwave's client) stays an option for later, should a real WebDAV storage be used.

## 8. User interface (stage 4)

**Layout** after Podcast Addict, adapted to Silica: instead of a side drawer three ring pages, swiped like in Sailwave (each page exists once for the whole session, the left neighbour is kept below, the right one attached):

- **New episodes** (start page): the newest unplayed episodes of all podcasts (max. 100); started ones show their progress, the one playing now live.
- **Podcasts**: cover grid with the number of unplayed episodes as a badge, or a list (with unplayed, episodes, downloaded and the listening time per podcast) – switchable in the pull-down menu, stored (`ui.podcastGrid`).
- **Playlist** (order since 24): "Now playing" (the current episode, always here; no context menu – it sits in the list header, see below; tap → episode page), "Playlist" (manual queue, filled with "Play next" / "Add to end of playlist", sorted by dragging a handle as the favourites in Sailwave; dragged onto the area below the last episode it is removed, with remorse), "Continue listening" (episodes in progress, collapsible: collapsed only the header with the number, expanded the latest 5 and "Show all" for a page with all of them; stored as `ui.continueExpanded`). The current episode appears only once. **Why this layout:** a growing header of a Qt 5.6 `ListView` grows *upwards* (`originY` moves, the rows stay in place) – context menus of rows in the header seemed to open upwards and their entries were no longer under the finger (A2), expanding a section in the header made the page jump. "Continue listening" therefore sits in the footer (grows downwards), and "Now playing" has no context menu. The same effect on the podcast page (description in the header) is countered by keeping the header's top at its place on screen while the header changes (24). Rejected: an inner scrolling list for "Continue listening" – nested flickables would block page scrolling.
- **Wording** (24): "Play next" / "Add to end of playlist" (German "Als Nächstes abspielen" / "Ans Ende der Playlist"), section "Playlist", "Now playing" = "Läuft gerade" ("Gerade gespielt" read like the past).
- **"Update"** (24): one pull-down entry ("Aktualisieren") on the start page and the podcasts page does both, first the sync (if an account is set up), then all feeds – as at app start (`appWindow.updateEverything`). Two entries ("Synchronize", "Update all") were hard to tell apart. Without a stored password the password dialog comes; cancelled, only the feeds are updated.

From there: podcast page (episodes, "only unplayed" stored as `ui.unplayedOnly`) and the **episode page, which is also the player** (since 12): image (a bit smaller since 19), title, slider (centered, narrower than the page since 19), ±30 s, chapter buttons, play (without a ring since 18), download/playlist, speed and sleep timer (one row), chapters, show notes. For another episode the slider shows its stored position and starts it there. The player bar opens the episode page of the current episode; it lies at the bottom of all pages except the episode pages (all of them since 19 – the page has its own controls, the bar for another episode only took space) and dialogs; lists end above it (`appWindow.playerBarHeight`).

**Player bar** in the design of Sailwave's: adaptive background colour (hue of the ambience's highlight colour, saturation capped, fixed lightness, contrast ≥ 3:1 to the icons), a fine highlight line on top with the listening progress in the same colour, image `iconSizeMedium` (space above and below), title/remaining time/chapter, buttons expand · moon · play. The moon opens a row of sleep durations above the bar (plus "End" of episode). Expanded: the chapters of the episode – 5 rows visible, more by scrolling inside the area, tap jumps there; no link to another page.

Since 19/20: no −30 button in the bar (skipping is on the episode page, the lock screen and the headset); the **moon takes its place** in the compact bar (20), so the sleep timer is always at hand, with or without chapters (in 19 it was only in the expanded bar, and without chapters not reachable from the bar). The expand arrow is only shown for episodes with chapters – the expanded area is about the chapters. Hidden rather than greyed out: Silica shows what is possible, a grey arrow reads as "not available right now".

**Countdowns from context menus** (26, the real cause of the unsubscribe and delete-download failures of 11–25): a function created inside a `ContextMenu` loses its QML context when the menu is destroyed after closing – when the countdown ended, the callback could not reach `core`, the row went blank (Silica expects it to be removed) and nothing happened. Rule: **never create a countdown callback inside a context menu**; context menus call `appWindow.remorseDeleteDownload()` / `appWindow.remorseUnsubscribe()`, whose countdown (`RemorsePopup` of the window) and callbacks live as long as the app. Countdowns started from pages and pull-down menus are fine.

**Unsubscribing from a list row** (attempts in 19–21, superseded by the above): the countdown is a `RemorsePopup` of the page, not `ListItem.remorseAction` of the row. A row is rebuilt when the list reloads (the feed update at start reloads the podcast list) and took its countdown – and with it the unsubscribe – along (reported in 11 and 18). The podcast page leaves first and unsubscribes afterwards. Since 21 the countdown callback calls `appWindow.unsubscribe()` **directly**: until 20 the call was deferred through an array in a `property var` (`push`) and a `Timer` with `interval: 0` – on Qt 5.6 the unsubscribe never arrived (countdown shown, nothing else happened). Rejected from now on: `push` on `property var` arrays and timers with interval 0 as "call later". The result is always reported: "Unsubscribed: <title>" or "Cannot unsubscribe: …".

**App icon** (19, more plastic since 41, after Sailfeed's icon): light spot top left and darker rim in the background, amber gradients and a glossy dot for the signal, a drop shadow under the symbol, headphones only lightly shaded (gradient, soft highlights). Since 19: Sailwave's drop shape (three round corners, bottom left pointed) and family style – white symbol, amber accent – with headphones and the podcast signal (dot with arcs to both sides) between the ear cups, plum gradient (#2a1747 → #8c3b6b) so it is not mistaken for Sailwave in the app grid. Template `design/harbour-sailcast.svg`; the PNGs in `icons/` are rendered from it (Chromium, once at 1376 px, then scaled down with Lanczos – small direct renders were broken). Rejected: the sailboat of Sailwave with a microphone (too close to Sailwave), waves at the bottom.

**Images:** episode image, else the podcast cover. If an episode has an image of its own that is a different picture than the cover (compared by content – many feeds repeat the cover under another URL; 16 × 16 fingerprint, `imagefingerprint.h`), the podcast cover is shown as a small badge bottom right (bar, episode page, app cover; the lock screen gets the same picture from `ArtComposer`, taken over from Sailwave, which draws it in C++ because QML cannot render while the screen is off). All covers come from `ImageCache`: each image is downloaded once, scaled to max. 600 px in a worker thread and stored as JPEG (cache folder `covers`, pruned to 100 MB above 150 MB); QML only loads these small local files. Before 12 the QML network loader decoded the original images (often 3000 px) again for every recycled delegate – the covers flickered while scrolling.

**Episode page toolbar** (36, after Podcast Addict): below the seek bar a row of four labelled buttons (`ToolbarButton.qml`): speed ("1,0×", label "Tempo" / "Tempo (Podcast)"), download ("Herunterladen" / "Löschen" with countdown / "N %" while loading – a tap cancels), playlist ("Zur Playlist" / highlighted "In Playlist"), sleep timer (moon drawn on a Canvas, label "Aus" / "N Min." / "Ende"). Speed and moon open a row of choices right below (one at a time; only for the episode playing now, greyed out otherwise). The play controls follow below the toolbar. Replaces the unlabelled ＋/✕ and download icons (B1) and the two combo boxes; the download progress bar is gone (percent in the label). Rejected: Podcast Addict's cover carousel (swiping already changes pages in Silica), bookmarks and favourites (for now).

**Podcast settings** (35, model: AntennaPod): page from the podcast page's pulley with the podcast's own **speed** (or "as set globally"), **skip intro** and **skip outro** (off, 5–120 s). Stored in the `settings` table as `podcast.<id>.rate`, `.skipIntro`, `.skipOutro` (no schema change, part of a database backup), removed when unsubscribing (`Database::removeSettings`). Player: an episode starting from its stored position never starts inside the intro (also restored or prepared ones; chapter taps and seeking are not changed); the outro ends the episode early like the real end (`_finished`: played, playlist/afterSingle logic), only for episodes longer than 2 × outro + intro; the speed control on the episode page changes the podcast's speed once it has its own ("Tempo (Podcast)"), else the global one. Rejected for now: the other AntennaPod per-podcast settings (rename, tags, notifications, per-podcast auto download/clean-up); volume adjustment needs the own player (v2).

**Played / unplayed** (since 23): an episode not started yet offers "Mark as played", a played one "Mark as unplayed", a **started one both** (finish it or start over) – context menu and episode page. Marking the episode playing now pauses it first; "unplayed" also seeks to the start.

**Episode row** (`EpisodeDelegate.qml`): episode image (else the podcast cover), title in up to two lines (bold while new), the podcast name (not on the podcast page), then date · length (or progress bar with the remaining time once started) – the date was next to the podcast name until 33, where a long name faded it out (compared with AntennaPod), a marker for downloaded and playlist; play button on the right; tap opens the episode page, long press the menu. **"› N Kapitel"** on its own line below the status line (29) unfolds the chapters right in the list (27): chevron turns by 90°, chapters indented below the title with a line on the left, time + title (long titles wrap, 30), tap = play from there, the current chapter highlighted; not kept when the row scrolls out of view. A 3-line model (title / date · duration / chapter trigger) was tried in 27 and dropped in 28 – the previous layout looked better.

**C++/QML split:** `AppCore` (context property `core`) is the only way from QML into the core library. The list models (`PodcastListModel`, `EpisodeListModel`) query SQL themselves and reload on `AppCore::dataChanged` – since 12 as a **diff** (`RowListModel`: rows removed, moved, inserted, the rest updated in place) instead of a model reset, so delegates, images and running remorse timers stay; single rows refresh on `episodeChanged` (position, download) – no list keeps more than it shows. Changes from QML that remove rows (unsubscribe) run after the delegate's own code (`appWindow.unsubscribeLater`).

**Player** (`PlayerController.qml`, QML `MediaPlayer` as in Sailwave):

- Sources in the order of `Archiver::playbackSources()`; on a playback error the next source is tried at the same position. If the last one fails, the feed is updated once (forced) and the episode tried again with the fresh links – signed or rotating media links expire, as with downloads.
- Start at the stored position; a played episode starts from the beginning. The seek waits until the media is seekable.
- Listening state saved on pause, after seeking (2 s after the last move), every 30 s while playing, at the end and when the app closes – `recordPlayback()` puts it into the sync outbox (only the latest per episode is kept, so frequent saves cost nothing on the server).
- At the end: played (and so out of the playlist). **Playback context** (24): an episode started on the playlist page ("Play playlist", a playlist row, "Now playing" while queued, the episode page opened from there) plays in *playlist mode* – the next playlist episode follows. Started anywhere else (start page, podcast page, "Continue listening", downloads) it is a *single episode*: at the end the setting `player.afterSingle` decides – `load` (default): the next playlist episode is shown paused in the player bar, one tap continues (in playlist mode); `play`: it starts; `stop`: nothing. The mode is stored (`player.queueMode`); "next" on the lock screen/cover to a playlist episode switches to playlist mode, to an episode in progress to single mode. Not done: continuing with "Continue listening" automatically – unexpected jumps between podcasts. Sleep timer from Sailwave, plus "end of episode" (stops in any mode).
- **Played = out of the playlist** (24): marking as played, "mark all as played", playing to the end and played states from the sync all remove the episode from the playlist (`AppCore::afterPlayed`, also after every sync and feed update).
- **Next** (app cover, lock screen, headset): next chapter; after the last chapter the next playlist episode, with an empty playlist the most recently played episode in progress (`AppCore::nextEpisode`) – like "next favourite" in Sailwave. **Previous** (lock screen): chapter start / previous chapter, without chapters −30 s. Lock screen/headset via Amber.Mpris in a Loader (like Sailwave); the package **requires** `qml(Amber.Mpris)` since 37 (was Recommends, only for Chum), so it is installed with the app – as Sailwave did after a user without amber-mpris had no lock screen controls.
- The last episode is restored on start with its image and position, but the media is only loaded on the first "play" – loading it paused in advance needed several presses and showed a wrong state (11).
- Settings: `player.rate`, `player.skipBack`, `player.skipForward` (30 s each), `player.last`, `player.sleepMinutes`.

**Listening time per podcast:** sum of the length of played episodes and the position of started ones. Episodes marked as played without listening count too – the database does not tell them apart (a played episode keeps no position).

**Sync password** in Sailfish Secrets (`SecretStore` from Sailwave, name `sync-password`), asked once; feeds and sync are updated on every start.

**Usability round (53):** smart resume (after more than 5 min of pause or a restart playback continues `player.smartResume` seconds earlier, default 10, 0 = off); time stamps in the show notes ("12:34", "1:02:03" – not inside links, not beyond the episode's length, not "18:30 Uhr") become links that jump there; "+5" in the player bar during the sleep timer's last minute (`SleepTimer.extend`, fading undone); per podcast "oldest first" (`podcast.<id>.oldestFirst`, podcast page only – `EpisodeListModel.oldestFirst`); search in a podcast's episode titles (pulley "Folgen durchsuchen", field in the list header, `EpisodeListModel.search` → `LIKE`, ASCII case-insensitive); "Ältere als gehört markieren" in the episode menu (`Database::markOlderPlayed`, sync like markAllPlayed); all mass "played" actions (podcast, folder, older ones) now with a countdown in the app window. Headphones unplugged: Sailfish's audio policy pauses the QML MediaPlayer by itself (checked on the device) – nothing to build.

**First-use hints** (52, as in Sailwave): `OnceHint` above the player bar shows a hint once (setting `hint.<key>`), 8 s or until tapped, one at a time (others wait): swipe hint with a moving circle on the first start (`RingHint`, from Sailwave; ends on a page change), the player bar the first time it appears, the first folder header (fold / press and hold), the first episode row with chapters ("› N Kapitel"), the playlist with 2+ episodes (handle / drag below to remove), the star on the episode page (bookmarks, 67); since 67 each hint belongs to its page and waits until that page is shown; the episode toolbar captions for the first 5 visits (50). The "Add podcast" page explains itself with examples while its field is empty (search terms, a feed or website address, the clipboard); the pulley entry is "Search / add podcast" (59, German "Podcast suchen / hinzufügen"). Double tap on the icon of the About page shows all hints again (`AppCore::resetHints` removes `hint.*`).

**Decisions of 21 Oct (51):** (1) **one "Add podcast"** in the Podcasts pulley: the search page also takes a feed address ("Check feed address: …" → Add podcast dialog) and offers an address in the clipboard; without a Podcast Index key it is a plain address field. "Import and export" moved into the settings (link at the top and in its pulley) – the Podcasts pulley has 5 entries. (2) **gpodder.net devices** (`core/gpodderdevices.*`): settings button "Gerät" (gpodder.net only) opens a dialog with the account's devices (`GET /api/2/devices/{user}.json`) and a new device name; an existing device is not registered again (`sync.deviceExisting`, otherwise its caption would become "Sailcast"); another device than before starts over with a first sync; optional "share subscriptions with all devices" (`POST /api/2/sync-devices/{user}.json`). (3) **Covers:** all cover addresses are fetched into the image cache once the start-up update is done (`AppCore::coverUrls`); the placeholder is a calm colour per podcast with up to two initials; covers in the podcast grid fade in briefly (not in recycling lists). YouTube: later (1.1+).

**Feedback round 21 Oct (50):** bookmarks get a name of their own (schema v7, `bookmarks.title`; dialog "Umbenennen / Notiz" with name and note; the list shows the name, else "Lesezeichen", and the note below); episodes with bookmarks show a small star on their image in every episode list (`EpisodeRow.bookmarkCount`); the episode toolbar shows **symbols only** (five captions overlapped) – short captions (Tempo, Laden, Playlist, Merken, Timer) only for the first 5 visits of an episode page (`hint.toolbar`), status texts (download %, sleep minutes, "Podcast" speed) always; the sleep moon is drawn by a `ShaderEffect` since 55 (Canvas drew nothing on the device, neither with `destination-out` nor with a clipped path). Podcast settings in the podcast row menu; "Stop updates (archived)" and "Archive podcast" moved from the podcast page pulley into the podcast settings. Folder menu: one "Ordner löschen …" with a dialog asking whether its podcasts are unsubscribed too. "Podcasts im Ordner" is a dialog with "Speichern" (tapping alone did not look like saving). Podcasts with new episodes since their page was last opened get a light blue glowing ball (68, Silica `GlassItem` like the page indicators; `NewDot`: top left on the tile, in the left margin of the list row; before: a glowing badge / "●", which looked like a glitch) – settings `podcast.<id>.newAt` (set when an update brings new episodes) and `.seenAt` (set when the podcast page opens), compared in the podcast query. "Add podcast" shows the 5 newest episodes of the checked feed (title, date, length) and, without the search, the feed's description (`FeedProbeResult::latest/description`).

**Bookmarks** (47, for 1.0): places in an episode to come back to (a tip, a recipe, a favourite passage) – unlike the listening position there can be several per episode, they stay where they are and also in played episodes. Schema v6, table `bookmarks` (episode, position in seconds, optional note, created; deleted with the episode). Episode page: 5th toolbar button (star, filled when the episode has bookmarks) sets one at the current position – for another episode at the shown position; section "Lesezeichen" above the chapters (time, note; tap plays from there; menu: add/edit note, delete with countdown). Page "Lesezeichen" (start page pulley, only when there are any): all bookmarks, newest first; tap plays, menu opens the episode / note / delete. Dialog and countdown in the app window (`editBookmarkNote`, `remorseDeleteBookmark`). Local only – neither the GPodder API nor Nextcloud have bookmarks; they are in the database backup and, for archived podcasts, in `episodes.json` (49).

**Folders** (45, modelled on the favourite groups of Sailwave, mockup variant A): schema v5 – table `folders` (id, name, sort_order, manual_update), `podcasts.folder_id` (NULL = none, `ON DELETE SET NULL`). **One folder per podcast** (as Sailwave's groups: no mapping table, no "move vs. copy"). Podcasts page in grid **and** list: one section per folder with a right-aligned header (chevron, name, ⏸ when the folder is "update only by hand", unplayed count), collapsible (`ui.folderCollapsed.<id>`); with at least one folder the podcasts without one are last under **"Other podcasts"** (id 0, no menu); without folders no headers (as before). The page is a flickable column with one Repeater per folder (`FolderListModel` + `PodcastListModel.folderId`) instead of a GridView/ListView: Qt 5.6 grids have no sections, and context menus must not sit in list headers. Folder context menu: rename, move up/down, update only by hand / include in "Update" (`podcastIdsForUpdate` skips folders with `manual_update`, ⏸ on header and its podcasts), mark all as played, delete folder (podcasts → Other podcasts) or delete folder and podcasts (unsubscribes them, also their feed secrets) – countdowns and dialogs in the app window (`appWindow.newFolder/renameFolder/movePodcastToFolder/remorseDeleteFolder`). A podcast gets its folder in "Add podcast" (also from the search), the podcast settings or "Move to folder …" in the list menu – always with "New folder …". **OPML:** folders as enclosing outlines (export), taken over on import (new podcasts and existing ones without a folder; deeper nesting → outermost folder); the import page chooses the folder for podcasts without one in the file (default Other podcasts), the export page which folders to export (none chosen = everything). **Not synchronized:** the GPodder API and Nextcloud GPodder Sync have no folders (gpodder.net's Settings API could hold one per podcast, but neither gpoddersync nor oPodSync implement it) – noted in the folder dialog and the sync settings; folders are in database backups and OPML. **Several podcasts at once** (46): folder menu "Choose podcasts …" (and right after "New folder") opens a page with all podcasts and a switch each – on = in this folder (taken out of its previous one, "now in …" shown), off = Other podcasts. Rejected: a selection mode on the Podcasts page (more code in grid and list for the same result). Rejected: folder tiles with 2×2 covers (mockup B), a folder filter on "New episodes".

**Per podcast "include in Update"** (44): podcast settings switch; uses the column `podcasts.paused` (since schema v1, skipped by `podcastIdsForUpdate()`); ⏸ in the grid (bottom right) and "⏸ manuell aktualisieren" in the list. Updating the podcast on its page still works; the sync is not per podcast and stays.

**Nextcloud sign-in with the browser** (44, Login Flow v2 as in Sailfeed, `core/nextcloudlogin.*`): POST `<server>/index.php/login/v2` → login URL (opened in the browser) + poll token; the app polls every 2 s (max. 20 min) until Nextcloud returns server, login name and an app password, then saves the account and synchronizes. Typing user + app password by hand stays possible. The Nextcloud-only parts (browser button, app password hint) are shown only with server type Nextcloud; gpodder.net shows its own hint (also for compatible servers such as oPodSync).

**Search details** (44): the "Add podcast" dialog opened from the search shows the Podcast Index entry: description, categories, language, episodes, last update, owner, explicit, website and funding buttons, "Source: Podcast Index"; the dialog scrolls.

**App start** (43): setting "Synchronize and update on start" (`start.update`, default on; off: only "Update" in the pulley – the feed passwords are still loaded at start) and "Start page" (`ui.startPage`: New episodes / Podcasts / Playlist, from the next start).

**Settings page** (since 18, pull-down menu of the start page and the podcasts page; layout as in Sailwave): synchronization (server type, server, user, password from/to Secrets, "Save and synchronize", "Remove account", last sync – another server or user resets the sync state, the next sync is a first sync), playback (skip back/forward 10–90 s), downloads (only on Wi-Fi, download new episodes, delete played downloads, "Clean up now"), archive (folder, cloud archive address), version. Settings are stored in the database (`settings` table), not in a separate configuration file, so a database backup contains them.

**Downloads page** (since 18; pull-down menu of the playlist since 73, before of the start page; "Downloads (N)" while some are running): running and waiting downloads on top (progress, context menu "Cancel download"), then all downloaded episodes as normal episode rows; header with number of files and space; pull-down "Cancel all downloads", "Delete played downloads".

**Import and export** (since 18, replaces the test import of 11): files are chosen with the Sailfish file picker (`Sailfish.Pickers`, allowed in the Jolla Store, as Sailwave's restore page): OPML, the gpodder-sailfish database (`Database`, copied to Downloads first – Sailjail cannot read gPodder's folder), a Sailcast database (replaces everything, with remorse). Exports go to **Downloads** with the date in the name (`sailcast-subscriptions-YYYY-MM-DD.opml`, `sailcast-YYYY-MM-DD.db` – after a WAL checkpoint), or the OPML file through the sharing menu (`Sailfish.Share`). Rejected: Documents as target – would need another Sailjail permission; Downloads is already granted for the file picker. Rejected: a free folder picker – shows folders the sandbox may not write to (same reason as in Sailwave).

**Lock screen and cover** (24): the lock screen (MPRIS, drawn by the system) does not scroll long titles (checked on the device). So the MPRIS title is short: "▸ chapter title" when the episode has chapters, else the episode title; the podcast is the artist line. The app cover: podcast name (one line, fixed), image, the same title in up to 3 lines, remaining time – no scrolling on the cover either (read at a glance; a marquee keeps redrawing it).

**Rejected:** a C++ `QMediaPlayer` – the QML `MediaPlayer` is what Sailwave uses successfully on the same devices, and the player logic needs QML bindings anyway. A side drawer like on Android – not a Silica pattern; the ring is known from Sailwave. A separate player page (11) – two pages for one episode, the episode page was only reachable via the pull-down menu.

## 9. Release 1.0 (40)

- Version 1.0 (spec, `AppCore::version`, CLI); build numbers continue (1.0 (build 40)).
- **About page** (settings pulley and button below the version): icon, version, short description, license, links to source code and PRIVACY.md (GitHub `t-sappl/harbour-sailcast`), thanks (Podcast Index, gPodder, Amber.Mpris).
- **Tokens hidden** (`core/urlmask.h`): values of query parameters that usually carry secrets (`auth`, `token`, `key`, `sig`, `secret`, `password`, …) and user info in addresses become "…" in every message of `AppCore` (`emitMessage`), in stored feed errors (`Database::setUpdateError`) and where the app shows an address (`core.maskUrl`). The full address stays in the database, the sync, OPML exports and the "Add podcast" field.
- Empty states point to the search ("Podcasts" pulley) and the sync settings.
- gpodder.net API stays visible.
- Public documents: README (features, Nextcloud setup, rclone, building with the key), PRIVACY.md, CHANGELOG.md; `.gitignore` keeps `podcastindex.pri`, DESIGN-INTERNAL.md, HANDOVER.md and TEST-PROTOCOL.md out of the repository.

## 9a. Landscape (54)

As in Sailwave: all orientations. In landscape the player is a column on the right (`PlayerPanel`, 38 % of the width, colour of the player bar) instead of the bar at the bottom – the bar would take too much of the short height. The panel shows image, podcast, title and chapter, position bar, large buttons (previous chapter *(with chapters)*, skip back, play, skip forward, next = next chapter/episode), the sleep timer (moon, "+5" in the last minute) and the chapter list; tap on image or title opens the episode page. It is hidden on the same pages as the player bar. The pages end left of it. Podcast grid: more columns at the same tile size. Episode page: two columns – player left, bookmarks, chapters and show notes right, each scrolling on its own. Hints and messages stay left of the panel; the player bar hint is only shown in portrait.

**Rejected:** a landscape-only page layout for the lists (wider rows are fine); hiding the panel to gain width (it is the player in landscape).

## 10. Origin and licenses

Sailcast is a new implementation. Behavior and formats of gpodder-sailfish, gpodder-core and podcastparser (ISC license, © Thomas Perl and contributors) served as reference – e.g. the "finished" rule (position within the last 10 seconds or 99 %) and the chapter format. If QML pages of gpodder-sailfish are taken over later, their ISC notice stays in the file header (ISC is compatible with GPL-3.0).

`SecretStore`, the network access helpers, the ring navigation, the sleep timer and the MPRIS integration are taken over from Sailwave (same author).

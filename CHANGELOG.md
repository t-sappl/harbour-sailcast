# Changelog

## 1.2 (2026-11-01)

Released on GitHub (RPMs and source) on 1 Nov 2026 – the first version for
OpenRepos and the Jolla Store (1.1 was only on GitHub).

### Discover (new)

- "Search / add podcast" became "Discover": with an empty search field it
  shows suggestions, typing searches Podcast Index as before; a feed
  address is checked right away
- Three tabs:
  - "For you" – personal suggestions from what you really listen to (mass
    markings do not count): your languages, categories and keywords, and
    optionally podcasts similar to your most listened ones; every
    suggestion says why ("Because you listen to History", "Similar to …");
    spam is filtered, "Not interested" hides a podcast for good
  - "Trending" – Podcast Index and fyyd together; each podcast with its
    main category, "never" categories left out
  - "New" – first episodes of German-language podcasts new on fyyd
    (Hörsuppe's list "Nullnummern"); in English the trailers of new and
    noteworthy shows from Podnews ("New Podcasts") – ▶ plays the trailer,
    then the show's first episode; marked when they fit your interests;
    shows in other languages than yours left out; can be switched off
  - a language line on "Trending" and "New": all your languages side by
    side, "change ›" leads to "My interests"
  - "Up next" in the player bar shows the episode that follows the trailer
  - long lists (20 more while scrolling), compact or detailed with the
    description (pull-down menu)
- "My interests": languages, category tiles (more, less, never, normal) and
  all sub-categories, keywords "More of this" / "Never" (with a built-in
  German/English word list, e.g. Klassik = classical), switch
  "Recommendations from my subscriptions"; a box on the Discover page shows
  what "For you" depends on; on the very first start "What are you
  interested in?" with the 12 most popular categories
- "Browse categories": category tiles → a page with the active podcasts of
  that category in your languages (Podcast Index and fyyd), sub-categories
  as chips; podcasts named by both sources and those fitting you first,
  subscribed ones last; detailed by default
- "Similar podcasts" on the podcast page (next to the cover) and in "Add
  podcast"; when fyyd does not know a podcast or knows nothing similar, active
  podcasts of its category instead; ones you already subscribed come last
  ("Add podcast" shows only new ones); compact or detailed
- Listen without subscribing: ▶ on the latest episodes in "Add podcast" and
  in "New", "All episodes" opens the podcast page as a preview; the podcast
  stays hidden (not in lists, sync or updates), its episodes say "not
  subscribed" and offer "Subscribe ›" right on the page (what you heard is
  kept; with folders you choose one first); removed after 30 days without
  use
- Shared episode links (an episode's web page, also Sailcast's own shares
  with "#t=" for the time): "Add podcast" shows the shared episode first
  and plays it from that time
- Discover as a ring page between New episodes and Podcasts (on by default,
  can be switched off in Settings → General)
- Privacy: only languages, categories and search terms leave the phone;
  with "Recommendations from my subscriptions" the addresses of your 10
  most listened podcasts are sent to fyyd (see PRIVACY.md)

### Podcasts

- Podcast settings: an own name for the podcast (empty = the name from the
  feed; only on this phone, not synchronized)
- Podcast settings: download new episodes automatically – as set globally,
  always or never for this podcast
- Podcast settings: delete played downloads – as set globally, never (kept
  also by "Delete played downloads"), right after listening or after N days
- Podcast settings: "Download episodes …" – all or only the unplayed ones,
  grouped by season or month/year; select single episodes or whole groups,
  with number and size
- Podcast page: opening the description also shows statistics (hours
  listened, episodes started, space used by downloads, publishing rhythm
  such as "weekly, Monday", estimated next episode – rhythm and next
  episode also in "Add podcast") and "About this
  podcast" – categories (German names), language, episodes, newest
  episode, feed address, website and support
- Long episode lists (more than 50): sections by season (when the feed has
  seasons) or by month and year, a grab on the right edge to jump, the
  first 50 and "Show older episodes"; tap a group header to download it,
  add its unplayed episodes to the playlist or mark it as played
- "Mark as played / unplayed …" on the podcast page: choose the episodes –
  all, all but the newest, single ones, or one and all older ones (press and
  hold); also played ones back to unplayed
- "Add unplayed to the playlist" on the podcast page: a whole (short,
  finished) podcast at once; podcast and groups go in oldest first
- Search on the phone: switch "Also in descriptions" – episodes found in
  their show notes, with the matching passage
- Podcasts page: "Search" (on the phone) in the pull-down menu; Discover
  is a swipe page now (in the menu only when taken out of the ring)
- Podcast page: "More: statistics and info" under the description; Add
  podcast: the description at the top, four lines, "More" for all of it
- Shorter pull-down menu on the podcast page: unsubscribe, login, support
  and "Save downloads in Music" moved to the podcast settings
- Language and categories of every podcast are read from its feed (every
  feed is loaded in full once after the update)

### Player and playlist

- Player bar: a "next" button (chapter, then episode) instead of the moon;
  a long chapter or podcast name in the second line scrolls
- Expanded player bar: a compact position bar to seek, skip, speed,
  bookmark (name and note in the panel from below) and sleep timer, then
  tabs for the chapters, the bookmarks (with notes and their menu) and
  "Up next" – the next playlist episodes, with "Play next", "Remove from
  playlist" and "Full playlist"
- Playlist: what you see is the order – the episode playing is always on
  top, the first one below it plays next ("Play next" puts an episode
  there)
- "Next" skips an episode: it counts as played and leaves the playlist;
  "Recently heard" at the bottom of the playlist page brings it back,
  from where you left it
- Sleep timer: "Other …" with an own duration (hours and minutes)
- A new speed takes effect at once while playing (before: only after the
  next jump)
- Streams whose length the player estimates far too long keep the feed's
  length for the seek bar; a jump that fails returns to where it was
- A playback that hangs on a weak network shows ▶ instead of pause, and
  "play" loads it again at the same position (once also by itself); while
  a stream loads, the seek bar shows where it will continue
- A message when marking as played deletes downloads ("right after
  listening")
- Playlist and downloads pages with tiles: time left, episodes,
  downloaded; size, played and free space

### Episodes and downloads

- Single episodes: "Share file" (the downloaded file, e.g. by mail or
  Bluetooth) and "Save in Music" (moved into the podcast's archive folder
  in Music/Podcasts, never cleaned up)
- Podcast settings: "Save downloads in Music" for all its downloads
- Downloads in the Music folder (off by default – the music player would
  list them): for all podcasts (Settings → Downloads and archive) or per
  podcast ("Download location" in the podcast settings, e.g. audiobooks in
  Music, news on the phone); there they are visible to other apps and are
  cleaned up like any download – "Save in Music" or "Delete played
  downloads: Never" keeps them
- Settings → Downloads and archive: choose the podcasts in the Music folder
  and those whose downloads are never deleted from a list of all podcasts
  (the second one grouped by download location)
- Changing the download location offers to move the existing downloads –
  all podcasts or chosen ones, with number and size (also from the podcast
  settings); the downloads page shows how much is in the Music folder
- Half-finished downloads of failed attempts are removed after a week; in
  Music only Sailcast's own files are touched
- Downloads page: "Add unplayed to the playlist" (oldest first), e.g.
  before a trip without network

### Other

- OPML import: switch "Only the newest episode unplayed" (off by default) –
  after loading, the older episodes of the imported podcasts count as
  played, handy when moving from another app
- Links like podcast://, pcast:// and itpc:// open Sailcast (also when it
  is closed) and show the podcast to subscribe
- gpodder.net: feed addresses rewritten by the server ("update_urls") are
  taken over
- A smoother first start: buttons on the empty start and Podcasts pages
  to discover podcasts, set up the sync or import a list (with a line
  where to find them later), "What are you interested in?" in Discover
  updating the suggestions live, and short one-time hints where they help
  (player bar, podcast menu, folders, bookmarks, skipping, "My
  interests"), on a readable backing and shown only once
- Design: playlist symbol and "↓ N" instead of words in lists, smaller ▶
  in rows, plain group headers, updated first-use hints; the shadow above
  the player bar fits light ambiences
- Database schema 11 – after updating, builds before 1.2 cannot open the
  database any more (make a backup first if you might go back)

## 1.1 (2026-10-24)

First packaged release (tag v1.1 on GitHub; submitted to the Jolla Store, not published there – the first published release is 1.2).

- Sailcast in the sharing menu ("Add podcast"): a link or text shared from
  the browser or another app opens "Search / add podcast" with it (also
  when Sailcast is closed)
- Search on the device: "Search" in the pull-down menus of New episodes and
  Playlist finds your podcasts, episode titles and bookmarks (also with
  umlauts in any case), and leads to "Search / add podcast" for new ones
- An address (also just a domain such as "cre.fm") is checked right away:
  a website is searched for the podcast feed it names with the most
  episodes; if there is none, the page's title or the site's name is
  searched in Podcast Index (links from Spotify, YouTube and other
  platforms search for the podcast's name, Apple Podcasts links are looked
  up by their id); a podcast found is shown as a card with its cover
- Share an episode link from its menu or the episode page, optionally from
  the current position
- Section "Lock screen and cover" in the settings: each of the two lock
  screen and headset buttons (chapter, episode or skip)
  and the second button on the app cover (next, skip back, skip forward,
  the sleep timer or a bookmark)
- Automatic database backup (off, daily, weekly) into
  Downloads/Sailcast-Backups and, with a Nextcloud sync account, into the
  Nextcloud folder "Sailcast-Backups" (newest 5 kept); "Back up now";
  restore a backup from the phone or the Nextcloud (the state before is
  kept and can be restored again)
- Season and episode numbers from the feed in the episode lists and on the
  episode page; "Support the podcast" on the podcast page (podcast:funding)
- gpodder.net: choose which devices share their subscriptions (check box
  per device, current sync group preset)
- Setting a bookmark opens a small panel for its name and note (tap beside
  it to skip)
- Settings in sub-pages (General, Synchronization, Playback, Downloads and
  archive, Backup, Import and export), each with a summary
- Setting "Sync and update together" (off: separate "Synchronize" and
  "Update" in the pull-down menus); "Synchronize now" in the settings

## 1.0 (2026-10-22)

First public release (source code on GitHub).

- New episodes, Podcasts (grid or list) and Playlist as swipeable pages;
  start page and sync/update on start selectable
- Folders for podcasts: collapsible sections in grid and list, "Other
  podcasts", folder menu (rename, order, update by hand, mark played,
  delete), choose several podcasts at once, folders in OPML import/export
- Podcast search through Podcast Index with details; adding a feed address
  with a check before subscribing (also from the clipboard; a website: the
  feed it names)
- Playlist with drag sorting, "Now playing", "Continue listening"; playback
  context (playlist or single episode) with a setting for what follows
- Player on the episode page with a labelled toolbar (speed, download,
  playlist, bookmark, sleep timer), chapters, skip buttons; lock screen, app
  cover and headset controls (next/previous chapter)
- Bookmarks with notes, page with all bookmarks
- Per-podcast settings: folder, include in "Update", speed, skip intro and
  outro
- Downloads with resume, Wi-Fi only, automatic downloads, clean-up
- Archive of whole podcasts into the Music folder, cloud archive address
  (rclone), import of archive folders
- Sync with Nextcloud GPodder Sync (sign-in in the browser, Login Flow v2)
  and the gpodder.net API
- Import/export: OPML, gpodder-sailfish database, database backup
- Feeds with login (password in Sailfish Secrets), repair of broken feeds,
  private feed tokens hidden in messages
- Landscape: player as a column on the right, two-column episode page
- German translation

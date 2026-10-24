# Changelog

## 1.1 (2026-10-24)

First packaged release (Jolla Store, OpenRepos).

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

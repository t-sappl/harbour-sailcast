# Privacy

Sailcast has no account of its own, no tracking, no analytics and no
advertising. It contacts only the following servers:

- **The podcast feeds and media files you subscribe to** – to update feeds,
  play and download episodes, load chapters and cover images. These servers
  (and tracking services some podcasts route their media links through) see
  your IP address and the app's user agent, as with any podcast app.
- **Your sync server, if you set one up** (your Nextcloud with GPodder Sync,
  or a gpodder.net compatible server) – subscriptions and the listening state
  (which episode, position, played) are exchanged with it. With "Sign in with
  the browser" the Nextcloud login page opens in the browser, and your
  Nextcloud hands Sailcast an app password (Login Flow v2). Folders,
  bookmarks, the playlist and settings are not part of the sync.
- **Your Nextcloud, for the database backup** – only with a Nextcloud sync
  account and the switch "Back up to Nextcloud" (on by default): a copy of
  the app's database (subscriptions, listening state, playlist, folders,
  bookmarks, settings – no passwords) is stored in the folder
  "Sailcast-Backups" of that account, the newest 5 are kept. "Restore" reads
  them from there. Switch it off in Settings → Backup to keep the copies on
  the phone only.
- **Podcast Index (api.podcastindex.org)** – when you search for podcasts:
  the search term is sent. On the Discover page: the language and the
  category of trending podcasts, and your "More of this" keywords (as
  search terms).
- **fyyd (api.fyyd.de)** – on the Discover page: the language (most active
  podcasts), the category tree, and your "More of this" keywords (as search
  terms). With "Recommendations from my subscriptions" (on by default,
  switch in "My interests"): the feed addresses of your 10 most listened
  podcasts, to find similar ones and the public collections they are in.
  Your listening history itself, the categories you weight and your "Never"
  keywords stay on the phone. Answers are kept for a day on the phone.
  "Similar podcasts" (podcast page, Add podcast) sends the address of that
  one podcast. The "New" tab asks for Hörsuppe's public list of first
  episodes and then for each listed podcast by its fyyd number – nothing
  about you.
- **Podnews (podnews.net)** – the "New" tab in English: Podnews' public list
  "New Podcasts" (trailers of new shows) is loaded, kept for 6 hours on the
  phone; for each show listed, Podcast Index is asked by the show's feed
  address (its categories and language) – nothing about you.
- **Your cloud archive address, if you set one** (e.g. a local rclone server).

Sharing: "Share" sends an episode's web address (optionally with the
position) only to the app you choose in the Sailfish sharing menu – never
the address of a private feed. A link or text shared *to* Sailcast stays on
the phone until you search for it or check it as a feed address.

Data stored on the phone: the database with subscriptions, episodes,
listening state, playlist, folders, bookmarks and settings (in the app's data
folder), its backups in Downloads/Sailcast-Backups, downloads and archive
folders. Passwords (sync account, feed logins) are kept in
Sailfish Secrets, never in the database, its backups or exports. OPML exports contain
the full feed addresses, including private tokens of premium feeds – keep
them private.

Nothing is sent to the developer.

Contact: Thomas Sappl, thomas.sappl@gmail.com

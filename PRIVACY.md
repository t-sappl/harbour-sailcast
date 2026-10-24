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
- **Podcast Index (api.podcastindex.org)** – only when you use the podcast
  search: the search term is sent.
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

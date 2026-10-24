// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef DATABASE_H
#define DATABASE_H

#include <QList>
#include <QSqlDatabase>
#include <QString>
#include <QStringList>

#include "episodeaction.h"
#include "feedtypes.h"

// One subscription as shown in lists. The counts are computed by the
// query (see podcasts()), not stored.
struct PodcastRow
{
    qint64 id = 0;
    QString url;
    QString title;          // custom title if set, else the feed title
    QString link;
    QString description;
    QString author;
    QString coverUrl;
    QString authUser;       // user name for feeds with login, empty = none
    qint64 lastUpdate = 0;  // seconds since the epoch, 0 = never
    QString lastError;      // empty if the last update worked
    int episodeCount = 0;
    int unplayedCount = 0;
    int newCount = 0;
    bool archived = false;      // no more feed updates, downloads are kept
    bool manualUpdate = false;  // left out of "update all" (column "paused", 44)
    qint64 folderId = 0;        // 0 = no folder (45)
    bool freshEpisodes = false; // new episodes since the podcast page was last opened (50)
    QString funding;            // podcast:funding as JSON [{url, title}] (83), may be empty
    QString archiveFolder;      // folder name inside the archive root, empty = never archived
    int downloadedCount = 0;
    // Listening time in seconds: full length of played episodes plus the
    // position of started ones ("mark as played" counts as listened)
    qint64 listenedSeconds = 0;
};

// A folder of podcasts (45); id 0 stands for "Other podcasts" (no folder)
struct FolderRow
{
    qint64 id = 0;
    QString name;
    int sortOrder = 0;
    bool manualUpdate = false;  // its podcasts are left out of "update all"
    int podcastCount = 0;
    int unplayedCount = 0;
};

// A bookmark in an episode (47), with what lists need to show it
struct BookmarkRow
{
    qint64 id = 0;
    qint64 episodeId = 0;
    int position = 0;       // seconds
    QString note;
    qint64 created = 0;     // Unix time
    QString episodeTitle;
    QString podcastTitle;
    QString imageUrl;       // episode image, else the cover
    QString title;          // own name (50), empty = "Bookmark"
};

// One episode as shown in lists
struct EpisodeRow
{
    qint64 id = 0;
    qint64 podcastId = 0;
    QString podcastTitle;
    QString guid;
    QString url;
    QString title;
    qint64 published = 0;
    int duration = 0;        // seconds, 0 = unknown
    int position = 0;        // seconds
    bool played = false;
    bool isNew = false;
    QString imageUrl;        // the episode's own image, may be empty
    QString displayImageUrl; // episode image, else the podcast cover
    int chapterCount = 0;
    qint64 lastPlayed = 0;
    QString mimeType;
    QString link;
    QString chaptersUrl;     // podcast:chapters (JSON), may be empty
    qint64 fileSize = 0;     // as declared in the feed
    qint64 positionUpdated = 0;
    bool downloaded = false;
    QString downloadPath;    // absolute path of the downloaded file
    QString archiveFile;     // file name inside the podcast's archive folder
    int bookmarkCount = 0;   // (50) star on the image in lists
    int season = 0;          // (83) from the feed, 0 = none
    int episodeNumber = 0;   // (83) from the feed, 0 = none

    // Seconds still to listen to; -1 if the duration is unknown
    int remaining() const
    {
        if (played) {
            return 0;
        }
        if (duration <= 0) {
            return -1;
        }
        return position < duration ? duration - position : 0;
    }
    bool inProgress() const { return !played && position > 0; }
};

// Result of applying actions received from the sync server
struct ApplyResult
{
    int applied = 0;    // changed an episode
    int older = 0;      // the episode's own state was newer - ignored
    int pending = 0;    // no matching episode (yet) - kept for later
};

struct SyncCounts
{
    int outboxSubscriptions = 0;
    int outboxActions = 0;
    int pendingActions = 0;
};

struct DatabaseStats
{
    int podcasts = 0;
    int episodes = 0;
    int played = 0;
    int unplayed = 0;
    int inProgress = 0;
};

// The app's SQLite database (QtSql). Owns one named connection; all access
// happens in the thread that opened it (the main thread).
//
// Schema changes go through migrate(): PRAGMA user_version holds the
// schema version, every step upgrades by one version.
class Database
{
public:
    Database();
    ~Database();

    bool open(const QString &path);
    void close();
    bool isOpen() const;
    QString lastError() const { return m_lastError; }
    QSqlDatabase handle() const;

    // --- Subscriptions ---
    // Finds a podcast by feed URL; http and https count as the same feed.
    // Returns -1 if there is none.
    qint64 findPodcast(const QString &url) const;
    // Adds a podcast (returns the existing id if already subscribed), -1 on
    // error. With queueForSync and a configured sync account, the change is
    // also put into the sync outbox (not when the sync itself adds it).
    qint64 addPodcast(const QString &url, const QString &title = QString(),
                      bool queueForSync = true);
    bool removePodcast(qint64 id, bool queueForSync = true);
    QStringList podcastUrls() const;
    QList<PodcastRow> podcasts() const;
    PodcastRow podcast(qint64 id) const;    // id 0 if not found
    QList<qint64> podcastIdsForUpdate() const;
    bool changeUrl(qint64 id, const QString &newUrl);
    // User name for a feed with login (HTTP Basic Auth). The password is
    // never stored in this database - see FeedUpdater::setPassword().
    bool setAuthUser(qint64 id, const QString &user);

    // --- Episodes ---
    enum EpisodeFilter { AllEpisodes, Unplayed, InProgress, Downloaded, NewEpisodes, Queue };
    // podcastId < 0: all podcasts. limit < 0: no limit. Newest first
    // (InProgress: most recently played first).
    // oldestFirst (53): oldest first for serials; search (53): words in the title
    QList<EpisodeRow> episodes(qint64 podcastId, int limit = -1, EpisodeFilter filter = AllEpisodes,
                               bool oldestFirst = false, const QString &search = QString()) const;
    EpisodeRow episode(qint64 id) const;    // id 0 if not found
    QList<ParsedChapter> chapters(qint64 episodeId) const;
    DatabaseStats stats() const;

    // --- Local playback state (each change is queued for the sync) ---
    // Position reached while playing, in seconds. An episode counts as
    // played when the position is within the last 10 seconds or 99 %.
    bool recordPlayback(qint64 episodeId, int started, int position, int total);
    bool markPlayed(qint64 episodeId);
    bool markUnplayed(qint64 episodeId);

    // --- Downloads and archive (stage 3) ---
    bool setArchived(qint64 podcastId, bool archived);
    // Left out of the automatic update ("update all"); updating the podcast
    // itself still works (column "paused", since schema v1, UI since 44)
    bool setManualUpdate(qint64 podcastId, bool manual);

    // Received sync actions that never found their episode (e.g. episodes
    // no longer in the feed) are dropped after maxAgeDays (49); returns the
    // number removed
    int prunePendingActions(int maxAgeDays = 90);

    // --- Bookmarks (47), local only ---
    // Of one episode (by position), or with episodeId 0 all (newest first)
    QList<BookmarkRow> bookmarks(qint64 episodeId = 0) const;
    int bookmarkCount() const;
    // created: Unix time (0 = now; an archive import keeps the original)
    qint64 addBookmark(qint64 episodeId, int position, qint64 created = 0);  // id, 0 on error
    bool setBookmarkNote(qint64 bookmarkId, const QString &note);
    bool setBookmarkTitle(qint64 bookmarkId, const QString &title);
    qint64 removeBookmark(qint64 bookmarkId);           // episode id of it, 0 if none

    // --- Folders (45): one folder per podcast, local only ---
    QList<FolderRow> folders() const;           // by sort order
    FolderRow unfiledFolder() const;            // counts of the podcasts without a folder
    qint64 folderByName(const QString &name) const;     // 0 if none (case-insensitive)
    qint64 createFolder(const QString &name);   // existing id if the name exists, 0 on error
    bool renameFolder(qint64 folderId, const QString &name);
    bool moveFolder(qint64 folderId, int step); // -1 up, +1 down
    bool setFolderManualUpdate(qint64 folderId, bool manual);
    bool deleteFolder(qint64 folderId);         // its podcasts get no folder
    bool setPodcastFolder(qint64 podcastId, qint64 folderId);   // 0 = none
    QList<qint64> podcastIdsInFolder(qint64 folderId) const;
    bool setArchiveFolder(qint64 podcastId, const QString &folder);
    bool isArchiveFolderUsed(const QString &folder, qint64 exceptPodcastId) const;
    bool setDownloaded(qint64 episodeId, const QString &path);
    bool clearDownload(qint64 episodeId);
    bool setArchiveFile(qint64 episodeId, const QString &fileName);
    QString episodeDescription(qint64 episodeId, bool html) const;
    QString podcastCoverUrl(qint64 podcastId) const;
    // Replaces the chapters of an episode (e.g. loaded from podcast:chapters)
    bool setChapters(qint64 episodeId, const QList<ParsedChapter> &chapters);
    // Episodes with a podcast:chapters URL whose chapters are not loaded yet
    QList<qint64> episodesMissingChapters(qint64 podcastId = -1) const;
    // Takes over a listening state if it is newer than the stored one
    // (archive import); returns true if it changed the episode
    bool mergeListeningState(qint64 episodeId, bool played, int position, qint64 timestamp,
                             qint64 lastPlayed);
    qint64 findEpisodeByGuid(qint64 podcastId, const QString &guid) const;
    bool setPodcastMetadata(qint64 podcastId, const QString &title, const QString &link,
                            const QString &description, const QString &author,
                            const QString &coverUrl);

    // --- Playlist (manual queue, stage 4) ---
    bool queueAdd(qint64 episodeId, bool atFront);   // also moves an episode already in it
    bool queueRemove(qint64 episodeId);
    bool queueMove(qint64 episodeId, int newIndex);
    // New order: the given ids first (ids not in the playlist are ignored),
    // then the remaining playlist episodes in their previous order
    bool queueSetOrder(const QList<qint64> &episodeIds);
    void queueClear();
    QList<qint64> queueIds() const;
    // Marks all unplayed episodes of a podcast as played; returns the number
    int markAllPlayed(qint64 podcastId);
    // The unplayed episodes of the same podcast older than this one (53)
    int markOlderPlayed(qint64 episodeId);

    // --- Settings (key/value, e.g. the sync account) ---
    QString setting(const QString &key, const QString &defaultValue = QString()) const;
    bool setSetting(const QString &key, const QString &value);
    // Removes all settings whose key starts with prefix (per-podcast settings)
    bool removeSettings(const QString &prefix);
    bool isSyncConfigured() const;

    // --- Sync (used by SyncEngine) ---
    // Outbox: local changes not yet uploaded
    QStringList outboxSubscriptions(const QString &kind) const;   // "add" / "remove"
    void removeOutboxSubscriptions(const QStringList &urls);
    QList<EpisodeAction> outboxActions(int limit) const;
    void removeOutboxActions(const QList<qint64> &ids);
    // Actions received from the server: stored first, then applied; the
    // ones without matching episode stay pending and are tried again after
    // the next sync or feed update.
    void addPendingActions(const QList<EpisodeAction> &actions);
    ApplyResult applyPendingActions();
    // Forget everything sync-related except the account (new account/server)
    void resetSyncState();
    SyncCounts syncCounts() const;

    // --- Feed updates (used by FeedUpdater) ---
    struct FeedState
    {
        QString url;
        QString etag;
        QString lastModified;
        QString authUser;
        bool neverUpdated = true;
    };
    FeedState feedState(qint64 podcastId) const;
    // Stores a parsed feed; returns the number of new episodes, -1 on error
    int applyFeed(qint64 podcastId, const ParsedFeed &feed,
                  const QString &etag, const QString &lastModified);
    void markNotModified(qint64 podcastId);
    void setUpdateError(qint64 podcastId, const QString &error);
    // Removes stored connection errors ("Network session error" etc., see
    // FeedUpdater) - they were no problems of the feeds
    void clearConnectionErrors();

    static qint64 now();

private:
    bool exec(const QString &sql);
    bool queueAction(qint64 episodeId, const QString &action, int started, int position,
                     int total, qint64 timestamp);
    void queueSubscription(const QString &kind, const QString &url);
    qint64 findEpisodeForAction(const EpisodeAction &action) const;
    bool migrate();
    bool fail(const QString &context, const QString &error) const;

    QString m_connection;
    mutable QString m_lastError;
};

#endif // DATABASE_H

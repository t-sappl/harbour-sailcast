// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef APPCORE_H
#define APPCORE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

class FeedProbe;
class PodcastIndex;
class NextcloudLogin;
class GpodderDevices;
struct PodcastSearchResult;
struct FeedProbeResult;
class QNetworkConfigurationManager;
class QTimer;
struct ArchiveResult;
class Archiver;
class ChaptersLoader;
class Database;
class DownloadManager;
class FeedUpdater;
class SyncEngine;

// The core of the app as seen from QML (context property "core"). Owns the
// database and the workers of the core library and offers what the pages
// need: updates, sync, downloads, listening state, playlist, settings.
//
// QML uses int for ids (episode and podcast ids stay far below 2^31).
//
// Change notification: dataChanged() after anything that can change lists
// (update, sync, marking, import); episodeChanged() for a single episode
// (listening position, download finished); queueChanged() for the playlist.
// The list models (PodcastListModel, EpisodeListModel) listen to these.
class AppCore : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool updating READ updating NOTIFY updatingChanged)
    Q_PROPERTY(qreal updateProgress READ updateProgress NOTIFY updatingChanged)
    Q_PROPERTY(bool syncing READ syncing NOTIFY syncingChanged)
    // Number of bookmarks in all episodes (47)
    Q_PROPERTY(int bookmarkCount READ bookmarkCount NOTIFY bookmarksChanged)
    // Nextcloud login in the browser (Login Flow v2) running (44)
    Q_PROPERTY(bool browserLoginActive READ browserLoginActive NOTIFY browserLoginActiveChanged)
    Q_PROPERTY(bool syncConfigured READ syncConfigured NOTIFY settingsChanged)
    Q_PROPERTY(QString syncAccount READ syncAccount NOTIFY settingsChanged)
    Q_PROPERTY(int podcastCount READ podcastCount NOTIFY dataChanged)
    Q_PROPERTY(int queueCount READ queueCount NOTIFY queueChanged)
    Q_PROPERTY(int downloadCount READ downloadCount NOTIFY downloadsChanged)
    Q_PROPERTY(int archivingPodcastId READ archivingPodcastId NOTIFY archivingChanged)
    Q_PROPERTY(bool searchAvailable READ searchAvailable CONSTANT)
    Q_PROPERTY(QString databaseError READ databaseError CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)

public:
    explicit AppCore(QObject *parent = nullptr);
    ~AppCore();

    // The one instance (created in main); the list models use it
    static AppCore *instance();
    Database *database() const { return m_database; }

    bool updating() const;
    qreal updateProgress() const;
    bool syncing() const;
    bool syncConfigured() const;
    QString syncAccount() const;
    int podcastCount() const;
    int queueCount() const;
    int downloadCount() const;
    int archivingPodcastId() const { return m_archivingId; }
    QString databaseError() const { return m_databaseError; }
    QString version() const;

    // --- Feeds ---
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void updatePodcast(int podcastId);
    // folderId (45): put the new podcast into this folder (0 = none)
    Q_INVOKABLE void subscribe(const QString &url, int folderId = 0);
    Q_INVOKABLE void unsubscribe(int podcastId);
    // Checks an address before subscribing (no subscription, no database);
    // feedChecked() follows with {requested, ok, needsLogin, url, title,
    // author, coverUrl, episodes, subscribed, error}
    Q_INVOKABLE void checkFeed(const QString &url);
    Q_INVOKABLE bool isSubscribed(const QString &url) const;
    // Podcast search in the Podcast Index directory (key built in, see
    // podcastindex.pri.example); searchFinished() with [{title, author, url,
    // imageUrl, description, language, episodes, subscribed}] or searchFailed()
    bool searchAvailable() const;
    Q_INVOKABLE void searchPodcasts(const QString &term);
    // Feed with login (HTTP Basic Auth): the user name goes into the
    // database, the password only into memory - QML keeps it in Sailfish
    // Secrets ("feed-<id>") and hands it over on every start with
    // setFeedPassword(). An empty user removes the login.
    Q_INVOKABLE void setFeedLogin(int podcastId, const QString &user, const QString &password);
    Q_INVOKABLE void setFeedPassword(int podcastId, const QString &password);
    // Ids of the podcasts with a login (their passwords are loaded at start)
    Q_INVOKABLE QVariantList feedsWithLogin() const;

    // --- Sync (password from Sailfish Secrets, see harbour-sailcast.qml) ---
    Q_INVOKABLE void sync(const QString &password);
    // Nextcloud Login Flow v2: opens the browser, waits for the app password
    // (browserLoginFinished / browserLoginFailed)
    Q_INVOKABLE void startBrowserLogin(const QString &server);
    Q_INVOKABLE void cancelBrowserLogin();
    // gpodder.net API devices (51): list the account's devices
    // (devicesListed [{id, caption, type, subscriptions}] / devicesFailed),
    // choose one (existing = do not register it again; another device
    // starts over with a first sync) and put devices into one sync group
    Q_INVOKABLE void listDevices(const QString &server, const QString &user, const QString &password);
    Q_INVOKABLE void chooseDevice(const QString &deviceId, bool existing);
    // newDevice: a device that is not on the server yet - registered first (77)
    Q_INVOKABLE void synchronizeDevices(const QStringList &deviceIds, const QString &newDevice = QString());
    bool browserLoginActive() const;
    // Sets the account ("nextcloud" / "gpodder"); another account than
    // before starts over with a first sync. Returns false if the values
    // are not usable.
    Q_INVOKABLE bool setSyncAccount(const QString &backend, const QString &server, const QString &user);
    Q_INVOKABLE void removeSyncAccount();

    // --- Episodes ---
    // Everything the episode page and the player need, incl. "sources"
    // (URLs in the order to try) and "chapters" ([{start: ms, title}]).
    // Images: "imageUrl" (episode image, else the podcast cover),
    // "episodeImageUrl" (own image only) and "coverUrl" (podcast cover).
    Q_INVOKABLE QVariantMap episode(int episodeId) const;
    Q_INVOKABLE QVariantMap podcast(int podcastId) const;
    Q_INVOKABLE void markPlayed(int episodeId);
    Q_INVOKABLE void markUnplayed(int episodeId);
    Q_INVOKABLE void markAllPlayed(int podcastId);
    // Called by the player on pause, stop, seek and every 30 s
    Q_INVOKABLE void recordPlayback(int episodeId, int started, int position, int total);
    // The episode in the player (its file is never cleaned up)
    Q_INVOKABLE void setCurrentEpisode(int episodeId) { m_currentEpisode = episodeId; }
    Q_INVOKABLE void loadChapters(int episodeId);
    // Playback of a remote source failed: update the podcast's feed (signed
    // or rotating media links expire); podcastRefreshed() follows
    Q_INVOKABLE void refreshPodcast(int podcastId);

    // --- Downloads ---
    // Starts the download without asking - QML asks first when
    // downloadAllowed() is false (setting "only on Wi-Fi")
    Q_INVOKABLE void download(int episodeId);
    Q_INVOKABLE void cancelDownload(int episodeId);
    Q_INVOKABLE void cancelAllDownloads();
    // Files inside the archive folder are not deleted here
    Q_INVOKABLE void deleteDownload(int episodeId);
    Q_INVOKABLE bool isDownloading(int episodeId) const;
    Q_INVOKABLE qreal downloadProgress(int episodeId) const;
    // Running and waiting downloads: [{episodeId, title, podcastTitle,
    // imageUrl, progress, running}]
    Q_INVOKABLE QVariantList activeDownloads() const;
    // {count, bytes} of the downloaded files
    Q_INVOKABLE QVariantMap downloadStats() const;
    // Deletes the downloads of played episodes (days: last played more than
    // N days ago, -1: the setting "cleanup.days"); returns the number
    Q_INVOKABLE int cleanupDownloads(int days);
    // Connected through Wi-Fi (or Ethernet)? Unknown counts as "no".
    Q_INVOKABLE bool onWifi() const;
    // False if the setting "downloads.wifiOnly" is on and there is no Wi-Fi
    Q_INVOKABLE bool downloadAllowed() const;

    // --- Playlist ---
    Q_INVOKABLE void queueAdd(int episodeId, bool next);
    Q_INVOKABLE void queueRemove(int episodeId);
    Q_INVOKABLE void queueMove(int episodeId, int newIndex);
    // Drag and drop: the shown order of the playlist (ids); episodes left out
    // (the one playing) stay in front
    Q_INVOKABLE void queueSetOrder(const QVariantList &episodeIds);
    Q_INVOKABLE void queueClear();
    Q_INVOKABLE bool isQueued(int episodeId) const;
    // Removes the finished episode from the playlist and returns the next
    // one, -1 if the playlist is empty
    Q_INVOKABLE int queueNext(int finishedEpisodeId);
    // "Next" on the cover and the lock screen after the last chapter: the
    // first playlist episode other than the current one, else the most
    // recently played episode in progress; -1 if there is none
    Q_INVOKABLE int nextEpisode(int currentEpisodeId) const;
    // Number of episodes in progress, without the given one (the current)
    Q_INVOKABLE int inProgressCount(int excludeEpisodeId) const;

    // --- Archive ---
    // Archives all episodes of the podcast (see Archiver); one at a time
    Q_INVOKABLE void archivePodcast(int podcastId);
    Q_INVOKABLE void cancelArchive();
    // "Archived": no more feed updates
    Q_INVOKABLE void setArchived(int podcastId, bool archived);
    // Leave a podcast out of "update all" (manual update only, 44)
    Q_INVOKABLE void setManualUpdate(int podcastId, bool manual);
    // The podcast page was opened: its "new episodes since you last looked"
    // marker goes away (50)
    Q_INVOKABLE void markPodcastSeen(int podcastId);
    // Cover addresses of all podcasts - fetched into the image cache in the
    // background after the start-up update (51)
    Q_INVOKABLE QStringList coverUrls() const;
    // Forgets which first-use hints were shown (settings "hint.*", 52)
    Q_INVOKABLE void resetHints();

    // --- Bookmarks (47): places in an episode, optional note, local only ---
    // [{bookmarkId, episodeId, position, note, created, episodeTitle,
    // podcastTitle, imageUrl}] - of one episode by position, with 0 all
    // (newest first)
    Q_INVOKABLE QVariantList bookmarks(int episodeId = 0) const;
    Q_INVOKABLE int addBookmark(int episodeId, int seconds);
    Q_INVOKABLE void setBookmarkNote(int bookmarkId, const QString &note);
    // Name and note at once (50)
    Q_INVOKABLE void editBookmark(int bookmarkId, const QString &title, const QString &note);
    Q_INVOKABLE void removeBookmark(int bookmarkId);
    int bookmarkCount() const;

    // --- Folders (45): one folder per podcast, local only (the sync has no
    // folders) ---
    // [{folderId, name, manualUpdate, podcastCount}] by order
    Q_INVOKABLE QVariantList folders() const;
    Q_INVOKABLE int createFolder(const QString &name);    // existing id for an existing name
    Q_INVOKABLE void renameFolder(int folderId, const QString &name);
    Q_INVOKABLE void moveFolder(int folderId, int step);
    Q_INVOKABLE void setFolderManualUpdate(int folderId, bool manual);
    Q_INVOKABLE bool folderManualUpdate(int folderId) const;
    // Deletes the folder; its podcasts go to "Other podcasts". To delete
    // them too, QML unsubscribes podcastIdsInFolder() first (feed secrets)
    Q_INVOKABLE void deleteFolder(int folderId);
    Q_INVOKABLE QVariantList podcastIdsInFolder(int folderId) const;
    Q_INVOKABLE void setPodcastFolder(int podcastId, int folderId);
    Q_INVOKABLE void markFolderPlayed(int folderId);
    // The older unplayed episodes of the episode's podcast (53)
    Q_INVOKABLE void markOlderPlayed(int episodeId);
    QString otherFolderName() const;
    // The archive folder in use and the default (~/Music/Podcasts)
    Q_INVOKABLE QString archiveDirectory() const;
    Q_INVOKABLE QString defaultArchiveDirectory() const;
    // Empty: back to the default. Returns false (and shows a message) if
    // the folder cannot be created or written.
    Q_INVOKABLE bool setArchiveDirectory(const QString &path);
    // Podcast folders in the archive folder (with episodes.json):
    // [{path, title, episodes, subscribed}]
    Q_INVOKABLE QVariantList archiveFolders() const;
    Q_INVOKABLE void importArchiveFolder(const QString &path);

    // --- Settings ---
    Q_INVOKABLE QString setting(const QString &key, const QString &defaultValue = QString()) const;
    Q_INVOKABLE void setSetting(const QString &key, const QString &value);
    // Address or message with tokens hidden (UrlMask) - for display
    Q_INVOKABLE QString maskUrl(const QString &text) const;

    // --- Import and export ---
    // folderId (45): folder for the podcasts that the file puts in no
    // folder (0 = "Other podcasts"); folders named in the file are used
    Q_INVOKABLE void importOpml(const QString &path, int folderId = 0);
    // Database of gpodder-sailfish (copied to a readable place first)
    Q_INVOKABLE void importGpodder(const QString &path);
    // Replaces the app database with a copy (e.g. a backup, or the
    // desktop tool's database)
    Q_INVOKABLE void importDatabase(const QString &path);
    // Write to Downloads; return the path, empty on error (with a message)
    // folderIds (45): only these folders (0 = "Other podcasts"); empty = all
    Q_INVOKABLE QString exportOpml(const QVariantList &folderIds = QVariantList());
    Q_INVOKABLE QString exportDatabase();

signals:
    void updatingChanged();
    void syncingChanged();
    void settingsChanged();
    void dataChanged();
    void episodeChanged(int episodeId);
    void queueChanged();
    // The running/waiting downloads changed (started, finished, cancelled)
    void downloadsChanged();
    void archivingChanged();
    void downloadProgressChanged(int episodeId, qreal progress);
    void downloadFinished(int episodeId, bool ok);
    // A feed update of this podcast is done (ok = false: it failed)
    void podcastRefreshed(int podcastId, bool ok);
    void searchFinished(const QString &term, const QVariantList &results);
    void searchFailed(const QString &term, const QString &error);
    // Result of checkFeed()
    void feedChecked(const QVariantMap &result);
    // Short text for the message banner
    void message(const QString &text);
    void browserLoginActiveChanged();
    void devicesListed(const QVariantList &devices);
    void devicesFailed(const QString &error);
    void devicesSynchronized();
    // Bookmarks of an episode changed (47); 0 = several
    void bookmarksChanged(int episodeId);
    void browserLoginUrl(const QString &url);
    void browserLoginFinished(const QString &server, const QString &user, const QString &appPassword);
    void browserLoginFailed(const QString &error);

private:
    void emitMessage(const QString &text);
    void bookmarkArchiveChanged(qint64 episodeId);
    void openDatabase();
    QString databasePath() const;
    QString archiveRoot() const;
    void reloadQueue();
    void autoDownload(qint64 podcastId, int newEpisodes);
    void onArchiveFinished(const ArchiveResult &result);
    // episodes.json of archived podcasts is written again some time after
    // their listening state changed (and when the app closes)
    void archiveStateChanged(int episodeId);
    void writeArchiveMetadata();
    void cleanupOnStart();
    void onFeedChecked(const FeedProbeResult &result);
    // After episodes became played: out of the playlist (24) and, with
    // "cleanup.days" = 0, their downloads deleted right away
    void afterPlayed();

    Database *m_database;
    FeedUpdater *m_updater;
    SyncEngine *m_sync;
    DownloadManager *m_downloads;
    Archiver *m_archiver;
    ChaptersLoader *m_chapters;
    FeedProbe *m_probe;
    PodcastIndex *m_index;
    NextcloudLogin *m_browserLogin;
    GpodderDevices *m_devices;
    QNetworkConfigurationManager *m_networkConfig = nullptr;
    QTimer *m_metadataTimer;
    QSet<qint64> m_metadataDirty;   // podcasts whose episodes.json is outdated
    QSet<int> m_cancelled;          // downloads cancelled here: no error message
    int m_archivingId = 0;
    int m_currentEpisode = 0;
    bool m_archiveCancelled = false;
    QString m_databaseError;
    int m_updateDone = 0;
    int m_updateTotal = 0;
    int m_updateNew = 0;
    int m_updateFailed = 0;
    QHash<int, qreal> m_downloadProgress;
    QSet<int> m_queue;      // cached ids of the playlist
};

#endif // APPCORE_H

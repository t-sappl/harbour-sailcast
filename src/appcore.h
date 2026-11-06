// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef APPCORE_H
#define APPCORE_H

#include <QHash>
#include <QObject>
#include <QSet>
#include <QSharedPointer>
#include <QString>
#include <QStringList>
#include <QVariantList>
#include <QVariantMap>

#include "fyyd.h"
#include "podnews.h"
#include "feedtypes.h"

struct ForYouProfile;
struct EpisodeRow;
class FeedProbe;
class PodcastIndex;
class ForYou;
class Fyyd;
struct ForYouItem;
struct FyydPodcast;
class NextcloudLogin;
class GpodderDevices;
class WebDavBackup;
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
    // Counts the changes of "Meine Interessen" (140): Discover computes
    // "Für dich" again when it is shown and the number changed
    Q_PROPERTY(int interestsRevision READ interestsRevision NOTIFY interestsChanged)
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
    // (190) Tiles on the playlist page: count, seconds left, downloaded
    // (246) Without the episode `excludeId` (the one playing, counted live by the page)
    Q_INVOKABLE QVariantMap queueStats(int excludeId = -1) const;
    int downloadCount() const;
    int archivingPodcastId() const { return m_archivingId; }
    QString databaseError() const { return m_databaseError; }
    QString version() const;

    // --- Feeds ---
    Q_INVOKABLE void updateAll();
    Q_INVOKABLE void updatePodcast(int podcastId);
    // folderId (45): put the new podcast into this folder (0 = none)
    Q_INVOKABLE void subscribe(const QString &url, int folderId = 0);
    // (156) Subscribes a podcast listened to without subscribing (147)
    Q_INVOKABLE void subscribePodcast(int podcastId);
    // (158) Category names as shown: "Society & Culture/Documentary" ->
    // "Gesellschaft & Kultur / Dokumentation" (fyyd's German names), a top
    // category left out when one of its sub-categories is there
    Q_INVOKABLE QStringList categoryLabels(const QStringList &categories) const;
    // (158) "de" -> "Deutsch" (the own names of "Meine Interessen")
    Q_INVOKABLE QString languageName(const QString &code) const;
    // "Anhören ohne Abo" (147): the podcast is stored provisionally (hidden,
    // see Database) and updated; previewReady(podcastId, episodeId) follows -
    // episodeId of the episode with this guid, -1 without guid (the podcast
    // page) or if it is not found; previewFailed(error) if the feed fails.
    // A subscribed podcast is answered at once.
    Q_INVOKABLE void listenWithoutSubscribing(const QString &url, const QString &title, const QString &guid);
    Q_INVOKABLE void unsubscribe(int podcastId);
    // Checks an address before subscribing (no subscription, no database);
    // feedChecked() follows with {requested, ok, needsLogin, url, title,
    // author, coverUrl, episodes, subscribed, error}
    Q_INVOKABLE void checkFeed(const QString &url);
    Q_INVOKABLE bool isSubscribed(const QString &url) const;
    // Id of the podcast with this feed address (http = https), -1 = none (99)
    Q_INVOKABLE int podcastIdForUrl(const QString &url) const;
    // Podcast search in the Podcast Index directory (key built in, see
    // podcastindex.pri.example); searchFinished() with [{title, author, url,
    // imageUrl, description, language, episodes, subscribed}] or searchFailed()
    bool searchAvailable() const;
    Q_INVOKABLE void searchPodcasts(const QString &term);
    // Discover (137, the search page with an empty field): "Für dich"
    // (forYouReady), Podcast Index trending (trendingReady) and fyyd's most
    // active podcasts (activeReady) in the first language of the profile;
    // rows as searchFinished plus reason (Für dich), all without spam and
    // without podcasts with a "Nie" keyword
    Q_INVOKABLE void discover();
    // "Kein Interesse": never suggested again (suggestionHidden)
    Q_INVOKABLE void hideSuggestion(const QString &url, const QString &title, const QString &author);
    // "Im Trend" (139: Podcast Index trending and fyyd's most active
    // podcasts in one list) for another language
    Q_INVOKABLE void discoverTrending(const QString &language);
    // A category to browse (150): "History" or "Leisure/Automotive" (Apple
    // names as in interests()). Podcast Index trending of that category per
    // language of the user, and fyyd's active podcasts of it, taking turns,
    // each podcast once; spam, "Nie", "Kein Interesse" and podcasts without an
    // episode for 24 months left out -> categoryReady(category, items)
    Q_INVOKABLE void discoverCategory(const QString &category);
    // "Für dich" again (after a change in "Meine Interessen")
    Q_INVOKABLE void refreshForYou();
    // "Meine Interessen" (139): {languages: [code], languagesChosen, languageNames: {code: name},
    //  available: [{code, name}], categories: [{name, label, parent, share, listened, level}] (fyyd's
    //  tree, top categories followed by their sub-categories; share relative to the strongest, 0..1),
    //  more / never: [{word, forms: [...]}], fromSubscriptions}
    Q_INVOKABLE QVariantMap interests() const;
    Q_INVOKABLE void setInterest(const QString &category, int level);  // ForYou::Interest
    Q_INVOKABLE void setDiscoverLanguages(const QStringList &languages); // empty = from the subscriptions
    Q_INVOKABLE QStringList translateKeyword(const QString &word) const;
    // kind: 1 = "Mehr davon", 3 = "Nie"; oldWord: replaced (editing), may be empty
    Q_INVOKABLE void setKeyword(int kind, const QString &word, const QStringList &forms, const QString &oldWord);
    Q_INVOKABLE void removeKeyword(int kind, const QString &word);
    Q_INVOKABLE void setRecommendFromSubscriptions(bool on);
    // "Ähnliche Podcasts" (141): fyyd's similar podcasts of one podcast (only
    // its address is sent; title and author find it when fyyd has another
    // address) - similarReady(feedUrl, items, known) / similarFailed
    // website (157): the podcast's web page - fyyd's entry is also found by it
    Q_INVOKABLE void similarPodcasts(const QString &feedUrl, const QString &title, const QString &author,
                                     const QString &website = QString());
    // "Neu" (149): the first episodes of podcasts new on fyyd (Hörsuppe's
    // curation "Nullnummern") - newReady(language, items) with {key (the
    // fyyd id), episodeTitle, guid, imageUrl, published, duration, added,
    // description}, newest added first, episodes with a "Nie" keyword left
    // out; newFailed(language, error).
    // language "en" (193): Podnews' "New Podcasts" instead - the trailers of
    // new shows, {key (the show's feed), title, author, imageUrl, published,
    // duration (of the trailer), added, description, trailer: true}
    Q_INVOKABLE void discoverNew(const QString &language = QString());
    // The podcast of such an episode (the curation names only its id;
    // Podnews only the feed, looked up in Podcast Index):
    // newPodcastReady(key, {title, author, url, imageUrl, wanted, match,
    // subscribed}) - wanted false for hidden, "Nie" or spam, match the
    // category of the user's interests it fits ("" = none); {} = unknown
    // (fyyd only - a Podnews show not in Podcast Index keeps Podnews' data)
    Q_INVOKABLE void newPodcastInfo(const QString &key);
    // (193) After "Anhören ohne Abo" of a Podnews show: its trailer (type
    // "trailer", else a title with "Trailer", else the length Podnews names)
    // and its first full episode - {trailer, first}, -1 = none
    Q_INVOKABLE QVariantMap trailerAndFirst(int podcastId, int trailerSeconds) const;
    int interestsRevision() const { return m_interestsRevision; }
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
    // (devicesListed [{id, caption, type, subscriptions, group}] / devicesFailed;
    // group: index of the sync group on the server, -1 = none - 83),
    // choose one (existing = do not register it again; another device
    // starts over with a first sync) and put devices into one sync group
    Q_INVOKABLE void listDevices(const QString &server, const QString &user, const QString &password);
    Q_INVOKABLE void chooseDevice(const QString &deviceId, bool existing);
    // newDevice: a device that is not on the server yet - registered first (77)
    // stopIds (83): devices taken out of their sync group
    Q_INVOKABLE void synchronizeDevices(const QStringList &deviceIds, const QString &newDevice = QString(),
                                        const QStringList &stopIds = QStringList());
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
    // (162) Statistics of a podcast for "Über diesen Podcast": listened
    // (seconds), started (episodes played or begun), downloads (count, bytes
    // on the phone), rhythm (text, "" = too few episodes), next (estimated
    // next episode, ms, 0 = none - only with a regular rhythm and not overdue)
    Q_INVOKABLE QVariantMap podcastStats(int podcastId) const;
    // (163) Publishing rhythm from episode dates (Unix seconds, any order):
    // {rhythm, next (ms)} - see podcastStats; also for "Add podcast"
    static QVariantMap rhythmOf(QList<qint64> dates);
    Q_INVOKABLE void markPlayed(int episodeId);
    Q_INVOKABLE void markUnplayed(int episodeId);
    // (253) Out of "New episodes", stays unplayed
    Q_INVOKABLE void removeFromNew(int episodeId);
    Q_INVOKABLE void markAllPlayed(int podcastId);
    // (246) Settings: every podcast (archived ones too, not provisional),
    // optionally all but the newest episode of each; one message
    Q_INVOKABLE void markEverythingPlayed(bool keepNewest, const QVariantList &podcastIds = QVariantList());
    // (248) For the dialog: podcasts with something to mark - podcastId,
    // title, count (what markEverythingPlayed would mark), archived
    Q_INVOKABLE QVariantList markablePodcasts(bool keepNewest) const;
    // Called by the player on pause, stop, seek and every 30 s
    Q_INVOKABLE void recordPlayback(int episodeId, int started, int position, int total);
    // (244) Listening log: seconds really listened (wall clock) since the
    // last call - from the player while it plays
    Q_INVOKABLE void logListening(int episodeId, int seconds, int mediaSeconds);
    // (244) The podcast's feed offers a video version (podcast:alternateEnclosure)
    Q_INVOKABLE bool podcastHasVideoVersion(int podcastId) const;
    // The episode in the player (its file is never cleaned up)
    Q_INVOKABLE void setCurrentEpisode(int episodeId);
    // (188) An episode heard to the end or skipped in the player: first in
    // "Recently heard" (playlist page; at most 30 kept)
    Q_INVOKABLE void noteHeard(int episodeId);
    // (189) Position of an episode skipped with "next" (it is marked played):
    // offered again when it is played or marked unplayed
    Q_INVOKABLE void noteSkipped(int episodeId, int seconds);
    int skippedAt(int episodeId) const;
    void clearSkipped(int episodeId);
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
    // Moves a downloaded episode into the archive folder in Music (120)
    Q_INVOKABLE void keepEpisode(int episodeId);
    // All downloads of a podcast into its archive folder (121); returns the number
    Q_INVOKABLE int keepPodcastDownloads(int podcastId);
    // (217) Downloads not in the place their podcast's download location
    // asks for (after changing it): per podcast {podcastId, title, count,
    // bytes, toMusic}; podcastId > 0 = only that podcast. Kept files,
    // archived podcasts, running downloads and the episode in the player
    // are left out
    Q_INVOKABLE QVariantList misplacedDownloads(int podcastId = 0) const;
    // Moves them for these podcasts; returns the number of files moved
    Q_INVOKABLE int moveDownloads(const QVariantList &podcastIds);
    // (143) "Alle Folgen herunterladen": what is not downloaded yet -
    // {all, allBytes, allUnknown, unplayed, unplayedBytes, unplayedUnknown}
    // (bytes as declared in the feed; *Unknown = episodes without a size)
    Q_INVOKABLE QVariantMap downloadAllInfo(int podcastId) const;
    // Starts those downloads, newest first; returns how many
    Q_INVOKABLE int downloadAll(int podcastId, bool unplayedOnly);
    // (159) Episodes to choose from on "Folgen herunterladen": not downloaded,
    // not loading, in the podcast's order - {id, title, published (ms),
    // duration, size, played, section (season, else month/year)}
    Q_INVOKABLE QVariantList downloadCandidates(int podcastId) const;
    // Starts the downloads of these episodes; returns how many
    Q_INVOKABLE int downloadEpisodes(const QVariantList &episodeIds);
    // (167) "Mark as played …": the unplayed episodes of a podcast with
    // their group (as downloadCandidates), and marking a choice of them
    // (170) played: the played ones instead, for "mark as unplayed"
    Q_INVOKABLE QVariantList unplayedCandidates(int podcastId, bool played = false) const;
    Q_INVOKABLE int markEpisodesPlayed(const QVariantList &episodeIds);
    Q_INVOKABLE int markEpisodesUnplayed(const QVariantList &episodeIds);
    // (159) A group of a long podcast list (section as EpisodeListModel shows
    // it - season or month/year), also the episodes not loaded yet:
    // download what is not downloaded, add the unplayed to the end of the
    // playlist (in the podcast's order), mark all as played; how many
    Q_INVOKABLE int downloadSection(int podcastId, const QString &section);
    Q_INVOKABLE int queueSection(int podcastId, const QString &section);
    // (165) All unplayed episodes of a podcast to the end of the playlist,
    // oldest first (a short, finished series heard at once)
    Q_INVOKABLE int queuePodcast(int podcastId);
    // (248) How many queuePodcast / queueDownloaded would add (pulley text)
    Q_INVOKABLE int queuePodcastCount(int podcastId) const;
    Q_INVOKABLE int queueDownloadedCount() const;
    Q_INVOKABLE int markSectionPlayed(int podcastId, const QString &section);
    // Unplayed downloaded episodes not in the playlist to its end, oldest
    // first (121); returns the number
    Q_INVOKABLE int queueDownloaded();
    // Path of the downloaded file, empty if none (120: share the file)
    Q_INVOKABLE QString downloadPath(int episodeId) const;
    Q_INVOKABLE QString mimeType(int episodeId) const;
    Q_INVOKABLE bool isDownloading(int episodeId) const;
    Q_INVOKABLE qreal downloadProgress(int episodeId) const;
    // Running and waiting downloads: [{episodeId, title, podcastTitle,
    // imageUrl, progress, running}]
    Q_INVOKABLE QVariantList activeDownloads() const;
    // {count, bytes} of the downloaded files
    Q_INVOKABLE QVariantMap downloadStats() const;
    // (218) Number of downloaded episodes (playlist pulley)
    Q_INVOKABLE int downloadedCount() const;
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
    // (167) The next playlist episodes after the current one (at most
    // count) - "Up next" in the expanded player bar
    Q_INVOKABLE QVariantList upNext(int currentEpisodeId, int count) const;
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
    // Own name of a podcast, empty = the feed's title (112)
    Q_INVOKABLE void setPodcastTitle(int podcastId, const QString &title);
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
    // Global search (85) on "Search / add podcast", in the own data:
    // {podcasts: [{podcastId, title, author, coverUrl}] (title/author),
    //  episodes: [{episodeId, title, podcastTitle, imageUrl, published,
    //  duration, played, position}] (titles, newest first),
    //  bookmarks: [as bookmarks()] (name, note, episode title)} -
    // at most 5 / 20 / 5, show notes are not searched
    // (236) inDescriptions: also episodes whose description contains the
    // term ("described", each with an "excerpt" around it)
    Q_INVOKABLE QVariantMap searchLocal(const QString &term, bool inDescriptions = false) const;
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
    // (212) Per-podcast choice "podcast.<id>.<name>" on the settings page:
    // {on, total} - subscribed podcasts whose own value is onValue, or
    // empty while the global choice is on
    Q_INVOKABLE QVariantMap podcastChoice(const QString &name, const QString &onValue, bool globalOn) const;
    // (213) The ids of those podcasts (e.g. the ones downloading into Music)
    Q_INVOKABLE QVariantList podcastIdsWithChoice(const QString &name, const QString &onValue, bool globalOn) const;
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
    // Link to share an episode (83): {url, title} - the episode's web page,
    // else the podcast's website, else the media address if it carries no
    // token; empty url if there is nothing that can be shared. seconds > 0
    // adds the position as "#t=h:mm:ss" (Podlove/media fragment) and to
    // the title.
    Q_INVOKABLE QVariantMap shareLink(int episodeId, int seconds = 0) const;

    // --- Import and export ---
    // folderId (45): folder for the podcasts that the file puts in no
    // folder (0 = "Other podcasts"); folders named in the file are used
    // (168) markOlder: after the first update of the added podcasts all but
    // the newest episode of each is marked as played (setting
    // "import.markOlder" holds the podcasts still waiting for their episodes)
    Q_INVOKABLE void importOpml(const QString &path, int folderId = 0, bool markOlder = false);
    // Database of gpodder-sailfish (copied to a readable place first)
    Q_INVOKABLE void importGpodder(const QString &path);
    // Replaces the app database with a copy (e.g. a backup, or the
    // desktop tool's database)
    // keepFolders (93): a backup of this phone - its archive and download
    // folders stay valid (a database from another device resets them)
    Q_INVOKABLE void importDatabase(const QString &path, bool keepFolders = false);
    // Write to Downloads; return the path, empty on error (with a message)
    // folderIds (45): only these folders (0 = "Other podcasts"); empty = all
    Q_INVOKABLE QString exportOpml(const QVariantList &folderIds = QVariantList());
    Q_INVOKABLE QString exportDatabase();

    // --- Automatic database backup (83) ---
    // Setting "backup.days": 0 = off, 1 = daily, 7 = weekly (default).
    // Copies go to Downloads/Sailcast-Backups (Sailjail allows writing
    // there), named sailcast-YYYY-MM-DD.db; the newest 5 are kept.
    Q_INVOKABLE QString backupDirectory() const;
    // Backs up now; returns the path, empty on error (with a message).
    // With a Nextcloud sync account and "backup.nextcloud" (default on) the
    // copy also goes to the Nextcloud folder "Sailcast-Backups" (WebDAV,
    // newest 5 kept); password: the sync password (else the one of the
    // last sync). Manual backups report the upload with a message.
    Q_INVOKABLE QString backupNow(const QString &password = QString());
    // Time of the last local / Nextcloud backup (ms, 0 = never)
    Q_INVOKABLE double lastBackup() const;
    Q_INVOKABLE double lastNextcloudBackup() const;
    Q_INVOKABLE bool nextcloudBackupPossible() const;

    // --- Restore (93) ---
    // Local backups, newest first: [{path, name, modified (ms), size}];
    // the copy made before the last restore is {undo: true}
    Q_INVOKABLE QVariantList localBackups() const;
    // Nextcloud backups -> nextcloudBackupsListed(ok, [{name, modified, size}], error)
    Q_INVOKABLE void listNextcloudBackups(const QString &password = QString());
    // Downloads one into the backup folder -> nextcloudBackupFetched(ok, path, error)
    Q_INVOKABLE void fetchNextcloudBackup(const QString &name, const QString &password = QString());
    // Replaces the database with a backup; the current state is copied to
    // "before-restore.db" first (offered as "before the last restore")
    Q_INVOKABLE bool restoreBackup(const QString &path);

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
    void forYouReady(const QVariantList &items);
    void forYouStarted();
    void trendingReady(const QString &language, const QVariantList &items);
    void categoryReady(const QString &category, const QVariantList &items);
    void interestsChanged();
    void suggestionHidden(const QString &url);
    // known: fyyd knows the podcast (else items is empty for that reason)
    void similarReady(const QString &feedUrl, const QVariantList &items, bool known);
    void similarFailed(const QString &error);
    // (147) see listenWithoutSubscribing
    void previewReady(int podcastId, int episodeId);
    void newReady(const QString &language, const QVariantList &items);
    void newFailed(const QString &language, const QString &error);
    void newPodcastReady(const QString &key, const QVariantMap &podcast);
    void previewFailed(const QString &error);
    // Result of checkFeed()
    void feedChecked(const QVariantMap &result);
    // Short text for the message banner
    void message(const QString &text);
    void browserLoginActiveChanged();
    void devicesListed(const QVariantList &devices);
    void devicesFailed(const QString &error);
    void devicesSynchronized();
    void nextcloudBackupsListed(bool ok, const QVariantList &backups, const QString &error);
    void nextcloudBackupFetched(bool ok, const QString &path, const QString &error);
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
    // (245) ~/Videos/Podcasts: video files of "Downloads in Music" / "Save in Music"
    QString videoRoot() const;
    QString mediaRootFor(const EpisodeRow &e) const;
    // (248) Duplicate subscriptions
    void mergeDuplicate(qint64 from, qint64 into);
    void mergeCaseDuplicates();
    bool ensureMergeBackup();
    bool isKept(const EpisodeRow &e) const;
    // (244) The video version of an episode to play: its podcast prefers
    // video ("podcast.<id>.version" = "video") and the feed offers one
    ParsedAlternate videoAlternate(const EpisodeRow &e) const;
    bool podcastInMusic(qint64 podcastId) const;
    QList<EpisodeRow> misplacedEpisodes(qint64 podcastId) const;
    void reloadQueue();
    void autoDownload(qint64 podcastId, int newEpisodes);
    void onArchiveFinished(const ArchiveResult &result);
    // episodes.json of archived podcasts is written again some time after
    // their listening state changed (and when the app closes)
    void archiveStateChanged(int episodeId);
    void writeArchiveMetadata();
    void cleanupOnStart();
    // Per-podcast "delete played downloads" (117): id -> days (-1 never)
    QHash<qint64, int> podcastCleanupDays() const;
    // A backup when the last one is older than "backup.days" (83); the
    // Nextcloud copy separately (it needs the sync password - also checked
    // after every sync)
    void backupIfDue();
    QString makeBackup();
    void uploadBackup(const QString &path, const QString &password, bool manual);
    // Database (after a WAL checkpoint) copied to path
    bool copyDatabaseTo(const QString &path);
    void onFeedChecked(const FeedProbeResult &result);
    // After episodes became played: out of the playlist (24) and, with
    // "cleanup.days" = 0, their downloads deleted right away
    void afterPlayed();
    void cleanupRightAfterListening();
    void markImportedOlder();

    Database *m_database;
    FeedUpdater *m_updater;
    SyncEngine *m_sync;
    DownloadManager *m_downloads;
    Archiver *m_archiver;
    ChaptersLoader *m_chapters;
    FeedProbe *m_probe;
    PodcastIndex *m_index;
    // Discover (137): own instances - ForYou counts the answers of its own
    ForYou *m_forYou;
    Fyyd *m_discoverFyyd;
    PodcastIndex *m_discoverIndex;
    // (150) Browsing a category: its sources per language ("pi:de", "fy:de")
    PodcastIndex *m_catIndex;
    Fyyd *m_catFyyd;
    QString m_catName;
    QStringList m_catParts;
    QHash<QString, QVariantList> m_catResults;
    void categoryPartReady(const QString &part, const QVariantList &items);
    bool categoryWanted(const QVariantMap &map, qint64 newest) const;
    // Podcast Index's name for an Apple category ("Society & Culture" ->
    // "Society,Culture", "Leisure/Automotive" -> "Automotive")
    static QString indexCategory(const QString &category);
    Fyyd *m_similarFyyd;
    Fyyd *m_newFyyd;        // (149) the curation
    Fyyd *m_newInfoFyyd;    // (149) its podcasts, cached 7 days
    Podnews *m_podnews;     // (193) "Neu" in English
    PodcastIndex *m_newIndex;   // (193) its shows by feed address
    QHash<QString, PodnewsTrailer> m_newTrailers;   // feed address -> show
    QVariantMap newPodcastMap(const FyydPodcast &p) const;
    QVariantMap newPodcastMap(const PodnewsTrailer &t, const PodcastSearchResult &r) const;
    void markNew(QVariantMap &map, const QStringList &names, const QStringList &labels, const QString &title,
                 const QString &description, const QString &language) const;
    // (197) Apple names ("History", "Leisure/Automotive") and labels (German
    // with a German phone) of a podcast's categories - from fyyd ids, or
    // from Podcast Index's words ("Society", "Culture"); the first one is
    // the main category (a top category when there is one)
    void categoriesOf(const QList<int> &fyydIds, QStringList *names, QStringList *labels) const;
    void categoriesOf(const QStringList &words, QStringList *names, QStringList *labels) const;
    // A category of the list set to "never" (or its top category)
    static bool neverCategory(const QStringList &names, const QStringList &never);
    // fyyd's category tree, read once from its cache (197: the rows of Im
    // Trend ask it per podcast); cleared when a new tree arrives
    void loadCategoryTree() const;
    mutable QList<FyydCategory> m_tree;
    mutable QHash<int, FyydCategory> m_treeById;
    // (197) Im Trend: without "never" categories, with the main category
    // ("category" label) - false = left out
    bool trendingRow(QVariantMap &map, const QStringList &names, const QStringList &labels,
                     const QStringList &never) const;
    // The profile for those rows, computed once per list (not per row)
    mutable QSharedPointer<ForYouProfile> m_newProfile;
    QVariantMap podcastMap(const PodcastSearchResult &r) const;
    QVariantMap podcastMap(const FyydPodcast &p) const;
    QVariantMap suggestionMap(const ForYouItem &item) const;
    void trendingPartReady(const QString &language, bool fromFyyd, const QVariantList &items);
    // "Im Trend": both parts of a language, merged once both are there
    QHash<QString, QVariantList> m_trendIndex;
    QHash<QString, QVariantList> m_trendFyyd;
    QSet<QString> m_trendIndexDone;
    QSet<QString> m_trendFyydDone;
    bool m_forYouAgain = false;
    int m_interestsRevision = 0;
    void interestsEdited() { ++m_interestsRevision; m_newProfile.reset(); emit interestsChanged(); }
    NextcloudLogin *m_browserLogin;
    GpodderDevices *m_devices;
    WebDavBackup *m_webdav;
    bool m_webdavManual = false;
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
    // (147) Podcasts loaded for "Anhören ohne Abo": id -> guid of the
    // episode to play ("" = the podcast page)
    QHash<qint64, QString> m_previews;
    // An update of a subscription took part (the summary message is only
    // for those, not for a preview alone)
    bool m_updateRegular = false;
    void removeProvisional(qint64 podcastId);
    QList<EpisodeRow> sectionEpisodes(int podcastId, const QString &section) const;
    // Seconds of a time in a shared link ("#t=1:02:03", "t=754", "t=12m30s"), 0 if none (148)
    static int linkSeconds(const QString &url);
    QHash<int, qreal> m_downloadProgress;
    QSet<int> m_queue;      // cached ids of the playlist
};

#endif // APPCORE_H

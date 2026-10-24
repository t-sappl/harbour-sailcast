// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Share 1.0
import Nemo.DBus 2.0
import "TimeFormat.js" as TimeFormat
import harbour.sailcast 1.0
import "pages"
import "cover"

ApplicationWindow {
    id: appWindow

    property alias player: playerController
    property alias sleepTimer: sleepTimerInstance

    // Height of the mini player while it is shown - the lists end above it
    // (0 in landscape: the player is a column on the right there)
    readonly property real playerBarHeight: playerBar.shown ? playerBar.height : 0

    // Landscape (54, as in Sailwave): the overlay layer follows the current
    // page's size and rotation; with an episode the player becomes a column
    // on the right (PlayerPanel) and the pages end left of it.
    readonly property bool landscapeLayout: uiLayer.width > uiLayer.height
    readonly property real playerPanelFullWidth: Math.round(uiLayer.width * 0.38)
    readonly property real playerPanelWidth: landscapeLayout && playerBar.wanted ? playerPanelFullWidth : 0

    cover: CoverPage {}
    // Portrait and landscape (54). allowedOrientations alone is not enough:
    // the pages take theirs from _defaultPageOrientations (portrait only on
    // phones) - both are needed, as in Sailwave and Sailfeed (55)
    allowedOrientations: Orientation.All
    _defaultPageOrientations: Orientation.All

    PlayerController {
        id: playerController
        onError: appWindow.showMessage(message)
    }

    SleepTimer {
        id: sleepTimerInstance
        mediaPlayer: playerController.mediaPlayer
        fadeOut: true
        onExpired: playerController.pause()
    }

    // Lock screen and headset controls; a Loader so that a missing
    // Amber.Mpris plugin only disables them (as in Sailwave)
    Loader {
        id: mprisLoader
        source: "MprisIntegration.qml"
        onStatusChanged: {
            if (status === Loader.Error) {
                console.warn("Amber.Mpris not available - no lock screen controls")
            }
        }
    }

    // Image for MPRIS / the lock screen: episode image with the podcast
    // cover as a badge, composed in C++ (works in the background too, see
    // src/artcomposer.h). Until it is ready, the plain image.
    readonly property string artKey: player.hasEpisode ? player.episodeImageUrl + "|" + player.coverUrl : ""
    property string _composedKey: ""
    property string _composedPath: ""
    readonly property string mprisArtUrl: _composedKey === artKey && _composedPath.length > 0
                                          ? "file://" + _composedPath : covers.url(player.imageUrl)

    function _cached(url) {
        var local = covers.url(url)
        return local.length > 0 ? local : url
    }

    onArtKeyChanged: artTimer.restart()

    // Bundles the changes of one episode switch into one job
    Timer {
        id: artTimer
        interval: 150
        onTriggered: {
            if (appWindow.artKey.length === 0) {
                return
            }
            var episodeImage = appWindow.player.episodeImageUrl
            var cover = appWindow.player.coverUrl
            var album = episodeImage.length > 0 && episodeImage !== cover ? appWindow._cached(episodeImage) : ""
            var logos = cover.length > 0 ? [appWindow._cached(cover)] : []
            if (album.length === 0 && logos.length === 0) {
                return
            }
            artComposer.compose(album, logos, appWindow.artKey)
        }
    }

    ArtComposer {
        id: artComposer
        userAgent: "Sailcast"
        imageSize: 512
        onComposed: {
            appWindow._composedKey = key
            appWindow._composedPath = path
        }
    }

    function showMessage(text) {
        messageBanner.show(text)
    }

    Connections {
        target: core
        onMessage: appWindow.showMessage(text)
    }

    // --- Countdowns started from context menus (25) ---
    // A function created inside a ContextMenu loses its QML context when the
    // menu is destroyed after closing; when the countdown ended, "core" could
    // not be reached any more and nothing happened (unsubscribing and
    // deleting downloads from lists, 11-24). These functions and their
    // callbacks belong to the window, which lives as long as the app.
    RemorsePopup {
        id: windowRemorse
        z: 200
    }

    function remorseDeleteDownload(episodeId) {
        windowRemorse.execute(qsTr("Deleting download"), function() { core.deleteDownload(episodeId) })
    }

    // Sharing an episode (83): link to its web page (AppCore::shareLink) in
    // the format of the browser's link sharing, so mail, messages and the
    // clipboard take it as a link. Here and not in a context menu (25).
    ShareAction {
        id: episodeShare
        mimeType: "text/x-url"
    }

    function shareEpisode(episodeId, seconds) {
        var link = core.shareLink(episodeId, seconds || 0)
        if (!link.url) {
            showMessage(qsTr("This episode has no address that can be shared"))
            return
        }
        episodeShare.title = qsTr("Share episode")
        episodeShare.resources = [{ "type": "text/x-url", "linkTitle": link.title, "status": link.url }]
        episodeShare.trigger()
    }

    // --- Sailcast as a share target (84) ---
    // .desktop: one share method "podcast" for links (text/x-url, from the
    // browser) and text (text/plain), ExecDBus so a share also starts the app; the
    // D-Bus name (OrganizationName.ApplicationName) is registered in main()
    // once this QML is loaded. Own adaptors instead of ShareProvider: the
    // browser shares a link as {type, linkTitle, status}, which
    // ShareProvider rejects (no "name"/"data"). Whatever arrives goes into
    // the field of "Search / add podcast": an address is checked, other
    // text searched.
    readonly property string dbusName: "harbour-sailcast.harbour-sailcast"

    // One entry in the sharing menu (85) for links and text: the path is
    // /share/<method of the .desktop file>
    DBusAdaptor {
        service: appWindow.dbusName
        path: "/share/podcast"
        iface: "org.sailfishos.share"
        function share(shareConfiguration) { appWindow.receiveShare(shareConfiguration) }
    }

    // Only used to read a share configuration
    ShareAction {
        id: shareReader
    }

    property string _sharedText: ""

    // The text of the first resource: a link ({status}), raw data ({data})
    // or plain text; from longer text the first web address
    function sharedTextOf(configuration) {
        var resources = []
        try {
            shareReader.loadConfiguration(configuration)
            resources = shareReader.resources
        } catch (e) {
            console.warn("Share: " + e)
        }
        if ((!resources || resources.length === 0) && configuration && configuration.resources) {
            resources = configuration.resources
        }
        if (!resources || resources.length === 0) {
            return ""
        }
        var r = resources[0]
        var text = typeof r === "string" ? r : (r.status || r.data || r.linkTitle || "")
        text = ("" + text).trim()
        var url = text.match(/(https?|feed|podcast|pcast|itpc):\/\/[^\s"'<>]+/i)
        if (url) {
            // Podcast app schemes are web addresses: podcast://x -> https://x
            // (without punctuation that ends the sentence around it)
            return url[0].replace(/[.,;:!?)\]]+$/, "").replace(/^(feed|podcast|pcast|itpc):\/\/(https?:\/\/)?/i, function(all, scheme, inner) {
                return inner ? inner : "https://"
            })
        }
        return text.length > 200 ? text.substring(0, 200) : text
    }

    function receiveShare(configuration) {
        var text = sharedTextOf(configuration)
        // Without this the sharing menu keeps waiting and the app stays
        // in the background
        appWindow.activate()
        if (text.length === 0) {
            showMessage(qsTr("Nothing to add in the shared content"))
            return
        }
        _sharedText = text
        // The page stack may still be starting (app started by the share)
        shareTimer.restart()
    }

    Timer {
        id: shareTimer
        interval: 600
        onTriggered: {
            if (pageStack.depth === 0 || pageStack.busy) {
                restart()
                return
            }
            var text = appWindow._sharedText
            appWindow._sharedText = ""
            pageStack.push(Qt.resolvedUrl("pages/SearchPage.qml"), { initialText: text })
        }
    }

    function remorseUnsubscribe(podcastId, title) {
        windowRemorse.execute(qsTr("Unsubscribing %1").arg(title), function() { appWindow.unsubscribe(podcastId) })
    }

    // Covers of all podcasts into the image cache once the start-up update
    // is done (51): the podcast grid then shows them at once instead of
    // one after the other the first time
    property bool _coversPrefetched: false
    Connections {
        target: core
        onUpdatingChanged: {
            if (!core.updating && !appWindow._coversPrefetched) {
                appWindow._coversPrefetched = true
                var urls = core.coverUrls()
                for (var i = 0; i < urls.length; i++) {
                    covers.url(urls[i])
                }
            }
        }
    }

    // --- Mass "played" actions with a countdown (53) ---
    function remorseMarkAllPlayed(podcastId, title) {
        windowRemorse.execute(qsTr("Marking all of %1 as played").arg(title), function() { core.markAllPlayed(podcastId) })
    }

    function remorseMarkFolderPlayed(folderId, name) {
        windowRemorse.execute(qsTr("Marking all in %1 as played").arg(name), function() { core.markFolderPlayed(folderId) })
    }

    function remorseMarkOlderPlayed(episodeId) {
        windowRemorse.execute(qsTr("Marking older episodes as played"), function() { core.markOlderPlayed(episodeId) })
    }

    // --- Bookmarks (47) - called from context menus, so the dialog and the
    // countdown live here ---
    // Name (50) and note of a bookmark
    function editBookmark(bookmark) {
        var dialog = pageStack.push(Qt.resolvedUrl("pages/BookmarkNoteDialog.qml"),
                                    { bookmarkTitle: bookmark.title || "", note: bookmark.note || "",
                                      timeText: TimeFormat.clock(bookmark.position || 0) })
        var id = bookmark.bookmarkId
        dialog.accepted.connect(function() { core.editBookmark(id, dialog.bookmarkTitle, dialog.note) })
    }

    function remorseDeleteBookmark(bookmarkId) {
        windowRemorse.execute(qsTr("Deleting bookmark"), function() { core.removeBookmark(bookmarkId) })
    }

    // --- Folders (45) - called from context menus, so dialogs, callbacks
    // and countdowns live here (see remorseUnsubscribe) ---
    function newFolder() {
        // After the name: straight on to choosing its podcasts (46) - the
        // dialog replaces itself with that page, which gets the new id
        var dialog = pageStack.push(Qt.resolvedUrl("pages/FolderNameDialog.qml"),
                                    { acceptDestination: Qt.resolvedUrl("pages/FolderPodcastsPage.qml"),
                                      acceptDestinationAction: PageStackAction.Replace })
        dialog.accepted.connect(function() {
            var id = core.createFolder(dialog.folderName)
            var target = dialog.acceptDestinationInstance
            if (target) {
                target.folderId = id
                target.folderName = dialog.folderName.trim()
            }
        })
    }

    function renameFolder(folderId, name) {
        var dialog = pageStack.push(Qt.resolvedUrl("pages/FolderNameDialog.qml"),
                                    { title: qsTr("Rename folder"), initialName: name })
        dialog.accepted.connect(function() { core.renameFolder(folderId, dialog.folderName) })
    }

    function movePodcastToFolder(podcastId) {
        var podcast = core.podcast(podcastId)
        var dialog = pageStack.push(Qt.resolvedUrl("pages/FolderPickerDialog.qml"),
                                    { folderId: podcast.folderId || 0, podcastTitle: podcast.title || "" })
        dialog.accepted.connect(function() { core.setPodcastFolder(podcastId, dialog.folderId) })
    }

    function askDeleteFolder(folderId, name, podcastCount) {
        var dialog = pageStack.push(Qt.resolvedUrl("pages/FolderDeleteDialog.qml"),
                                    { folderName: name, podcastCount: podcastCount })
        dialog.accepted.connect(function() { appWindow.remorseDeleteFolder(folderId, name, dialog.withPodcasts) })
    }

    // withPodcasts: unsubscribe its podcasts too (also their feed secrets)
    function remorseDeleteFolder(folderId, name, withPodcasts) {
        windowRemorse.execute(withPodcasts ? qsTr("Deleting %1 and its podcasts").arg(name)
                                           : qsTr("Deleting folder %1").arg(name), function() {
            if (withPodcasts) {
                var ids = core.podcastIdsInFolder(folderId)
                for (var i = 0; i < ids.length; i++) {
                    appWindow.unsubscribe(ids[i])
                }
            }
            core.deleteFolder(folderId)
        })
    }

    // Unsubscribing also forgets the password of a feed with login.
    // Called directly from the page-level countdowns (19/21). Until 20 a
    // Timer with interval 0 and an array in a "property var" deferred the
    // call - on Qt 5.6 the unsubscribe never arrived.
    function unsubscribe(podcastId) {
        var hadLogin = (core.podcast(podcastId).authUser || "").length > 0
        core.unsubscribe(podcastId)
        if (hadLogin) {
            secretStore.remove("feed-" + podcastId)
        }
    }

    // Downloads started by the user: without Wi-Fi (setting "only on
    // Wi-Fi") the user is asked first
    function requestDownload(episodeId) {
        if (core.downloadAllowed()) {
            core.download(episodeId)
            return
        }
        var dialog = pageStack.push(Qt.resolvedUrl("pages/MobileDataDialog.qml"))
        dialog.accepted.connect(function() { core.download(episodeId) })
    }

    // --- Feeds with login: passwords in Sailfish Secrets ("feed-<id>"),
    // handed to the core before the first update ---
    property int _feedSecretsPending: 0
    property bool _startUpdatePending: false

    function loadFeedPasswords() {
        var ids = core.feedsWithLogin()
        _feedSecretsPending = ids.length
        for (var i = 0; i < ids.length; i++) {
            secretStore.load("feed-" + ids[i])
        }
        return ids.length
    }

    function _startUpdate() {
        if (_startUpdatePending) {
            _startUpdatePending = false
            feedSecretsTimer.stop()
            core.updateAll()
        }
    }

    // If Secrets does not answer, the update starts anyway
    Timer {
        id: feedSecretsTimer
        interval: 5000
        onTriggered: appWindow._startUpdate()
    }

    // Login of a podcast set or removed on its page
    function setFeedLogin(podcastId, user, password) {
        core.setFeedLogin(podcastId, user, password)
        if (user.length > 0) {
            secretStore.store("feed-" + podcastId, password)
        } else {
            secretStore.remove("feed-" + podcastId)
        }
    }

    // --- Sync: the password lives in Sailfish Secrets ---
    property bool _syncInteractive: false
    property bool _syncWanted: false

    // interactive: ask for the password if none is stored (not on start-up)
    function startSync(interactive) {
        if (!core.syncConfigured || core.syncing) {
            return
        }
        _syncInteractive = interactive
        _syncWanted = true
        secretStore.load("sync-password")
    }

    function askSyncPassword() {
        var dialog = pageStack.push(Qt.resolvedUrl("pages/PasswordDialog.qml"), { account: core.syncAccount })
        dialog.accepted.connect(function() {
            var password = dialog.password
            secretStore.store("sync-password", password)
            core.sync(password)
        })
        // No password, no sync - the feeds are still updated
        dialog.rejected.connect(function() { appWindow._updateAfterSyncNow() })
    }

    // --- "Update" in the pull-down menus (24): one entry for both - first
    // the sync (subscriptions and listening state from the server, new
    // podcasts loaded), then all feeds, as at app start. Without a sync
    // account only the feeds.
    property bool _updateAfterSync: false

    function updateEverything() {
        if (core.updating) {
            return
        }
        if (!core.syncConfigured) {
            core.updateAll()
            return
        }
        _updateAfterSync = true
        if (!core.syncing) {
            startSync(true)
        }
    }

    // "Sync und Update" (86, setting "sync.withUpdate", default on): one
    // pulley entry "Aktualisieren" for both; off: "Synchronisieren" and
    // "Aktualisieren" (feeds only) as two entries
    property bool syncWithUpdate: core.setting("sync.withUpdate", "1") === "1"
    readonly property bool separateSync: core.syncConfigured && !syncWithUpdate

    // Pulley "Synchronisieren" (86): only the sync, no feed update after it
    function syncOnly() {
        _updateAfterSync = false
        startSync(true)
    }

    // Pulley "Aktualisieren" as set: sync + feeds, or the feeds alone
    function updateFromPulley() {
        if (separateSync) {
            if (!core.updating) {
                core.updateAll()
            }
        } else {
            updateEverything()
        }
    }

    function _updateAfterSyncNow() {
        if (_updateAfterSync) {
            _updateAfterSync = false
            core.updateAll()
        }
    }

    // The sync is done (or failed): now the feeds
    Connections {
        target: core
        onSyncingChanged: {
            if (!core.syncing) {
                appWindow._updateAfterSyncNow()
            }
        }
    }

    Connections {
        target: secretStore
        onLoaded: {
            if (name.indexOf("feed-") === 0) {
                if (ok && value.length > 0) {
                    core.setFeedPassword(parseInt(name.substring(5)), value)
                }
                appWindow._feedSecretsPending -= 1
                if (appWindow._feedSecretsPending <= 0) {
                    appWindow._startUpdate()
                }
                return
            }
            if (name !== "sync-password" || !appWindow._syncWanted) {
                return
            }
            appWindow._syncWanted = false
            if (ok && value.length > 0) {
                core.sync(value)
            } else if (appWindow._syncInteractive) {
                appWindow.askSyncPassword()
            } else {
                appWindow._updateAfterSyncNow()
            }
        }
        onStored: {
            if ((name === "sync-password" || name.indexOf("feed-") === 0) && !ok) {
                appWindow.showMessage(qsTr("Could not store the password: %1").arg(error))
            }
        }
    }

    // --- Ring navigation (as in Sailwave): new episodes -> podcasts ->
    // playlist -> new episodes. The pages exist once for the whole session.
    property Page ringHomePage: HomePage { ringMember: true }
    property Page ringPodcastsPage: PodcastsPage { ringMember: true }
    property Page ringPlaylistPage: PlaylistPage { ringMember: true }

    function nextRingPage(page) {
        if (page === ringHomePage) return ringPodcastsPage
        if (page === ringPodcastsPage) return ringPlaylistPage
        if (page === ringPlaylistPage) return ringHomePage
        return null
    }

    function prevRingPage(page) {
        if (page === ringHomePage) return ringPlaylistPage
        if (page === ringPodcastsPage) return ringHomePage
        if (page === ringPlaylistPage) return ringPodcastsPage
        return null
    }

    function ringPageActivated(page) {
        _ringRetries = 0
        ringTimer.restart()
    }

    Connections {
        target: pageStack
        onCurrentPageChanged: appWindow.ringPageActivated(pageStack.currentPage)
        onBusyChanged: {
            if (!pageStack.busy) {
                appWindow.ringPageActivated(pageStack.currentPage)
            }
        }
    }

    property int _ringRetries: 0

    Timer {
        id: ringTimer
        interval: 50
        onTriggered: appWindow._applyRingNavigation()
    }

    // Keeps the ring's left neighbour below the current ring page and its
    // right neighbour attached, so swiping works in both directions forever
    function _applyRingNavigation() {
        var page = pageStack.currentPage
        if (!page || !page.ringMember) {
            return
        }
        if (pageStack.busy || page.status !== PageStatus.Active) {
            if (_ringRetries < 40) {
                _ringRetries += 1
                ringTimer.restart()
            }
            return
        }
        var wantedPrev = prevRingPage(page)
        var prev = pageStack.previousPage(page)
        var prevIsBase = prev && !pageStack.previousPage(prev)
        if (prev !== wantedPrev || !prevIsBase) {
            pageStack.clear()
            pageStack.push(wantedPrev, {}, PageStackAction.Immediate)
            pageStack.push(page, {}, PageStackAction.Immediate)
            return
        }
        if (!page.canNavigateForward) {
            pageStack.pushAttached(nextRingPage(page))
        }
    }

    // Start page (setting "ui.startPage", 43): home, podcasts or playlist
    function _startPage() {
        var name = core.setting("ui.startPage", "home")
        if (name === "podcasts") return ringPodcastsPage
        if (name === "playlist") return ringPlaylistPage
        return ringHomePage
    }

    Component.onCompleted: {
        pageStack.push(_startPage(), {}, PageStackAction.Immediate)
        ringPageActivated(pageStack.currentPage)
        playerController.restore()
        // Fresh data on start (setting "start.update", default on, 43):
        // feeds (after the passwords of feeds with login are loaded), then
        // (if a password is stored) the sync. The feed passwords are loaded
        // in any case - a later "Update" needs them too.
        var updateOnStart = core.setting("start.update", "1") === "1"
        if (core.podcastCount > 0) {
            _startUpdatePending = updateOnStart
            if (loadFeedPasswords() === 0) {
                _startUpdate()
            } else if (updateOnStart) {
                feedSecretsTimer.start()
            }
        }
        if (updateOnStart) {
            startSync(false)
        }
        ringHintTimer.start()
    }

    // The core outlives the QML engine (see main), so the position can
    // still be saved here
    Component.onDestruction: playerController.save()

    // --- First-use hints (52) ---
    // Set once the swipe hint was started or found unnecessary (56)
    property bool _ringHintChecked: false

    // source: the item asking (66) - the hint waits until its page is the
    // current one (the three ring pages are all built at the start, so the
    // folder hint appeared on the start page and the playlist one never)
    function showHint(key, text, source) {
        onceHint.show(key, text, pageOf(source))
    }

    function pageOf(item) {
        while (item) {
            if (item.hasOwnProperty("status") && item.hasOwnProperty("allowedOrientations")) {
                return item
            }
            item = item.parent
        }
        return null
    }

    // All hints again (double tap on the icon of the About page)
    function resetHints() {
        core.resetHints()
        onceHint.reset()
        appWindow.showMessage(qsTr("The hints will be shown again"))
    }


    // Swipe hint on the first start, on the first ring page; ends after its
    // runs or on any page change - and never comes back

    Timer {
        id: ringHintTimer
        interval: 1000
        onTriggered: {
            if (core.setting("hint.ring", "0") !== "1" && pageStack.currentPage && pageStack.currentPage.ringMember) {
                ringHint.start()
            }
            appWindow._ringHintChecked = true
        }
    }

    Connections {
        target: pageStack
        onCurrentPageChanged: {
            if (ringHint.active) {
                ringHint.stop()
            }
        }
    }

    // The player bar the first time it appears
    Connections {
        target: playerController
        onHasEpisodeChanged: {
            if (playerController.hasEpisode && !appWindow.landscapeLayout) {
                appWindow.showHint("playerBar", qsTr("Player bar: tap for the episode page, the moon for the sleep timer, the arrow for the chapters"))
            }
        }
    }

    // Overlay layer (54): same size and rotation as the current page, so
    // the player bar, panel, hints and messages turn with the page
    Item {
        id: uiLayer
        readonly property Item page: pageStack.currentPage
        anchors.centerIn: parent
        width: page ? page.width : parent.width
        height: page ? page.height : parent.height
        rotation: page ? page.rotation : 0
        z: 100

        PlayerBar {
            id: playerBar
            anchors.bottom: parent.bottom
            width: parent.width
            z: 100
            // Not in landscape: shown is false there, the panel takes over
        }

        PlayerPanel {
            id: playerPanel
            anchors {
                top: parent.top
                bottom: parent.bottom
                right: parent.right
            }
            width: appWindow.playerPanelFullWidth
            shown: appWindow.playerPanelWidth > 0
            barColor: playerBar.color
            z: 100
        }

        OnceHint {
            id: onceHint
            anchors.bottom: parent.bottom
            anchors.bottomMargin: appWindow.playerBarHeight
            width: parent.width - appWindow.playerPanelWidth
            z: 101
            // Not together with the swipe hint (55)
            // Not before the swipe hint was checked at the start, not with
            // the swipe hint, not under a message (55, 56)
            currentPage: pageStack.currentPage
            paused: !appWindow._ringHintChecked || ringHint.active || messageBanner.visible
        }

        RingHint {
            id: ringHint
            anchors.fill: parent
            anchors.rightMargin: appWindow.playerPanelWidth
            bottomMargin: appWindow.playerBarHeight
            z: 101
            onFinished: core.setSetting("hint.ring", "1")
        }

        MessageBanner {
            id: messageBanner
            anchors.bottom: parent.bottom
            anchors.bottomMargin: appWindow.playerBarHeight + Theme.paddingLarge
            width: parent.width - appWindow.playerPanelWidth - 2 * Theme.horizontalPageMargin
            z: 101
        }
    }
}

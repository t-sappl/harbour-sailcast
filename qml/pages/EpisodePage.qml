// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Nemo.KeepAlive 1.2
import "../"
import "../TimeFormat.js" as TimeFormat

// One episode with the player: image (with the podcast cover as a badge),
// title, state, position slider, skip/chapter/play buttons, download and
// toolbar (speed, download, playlist, sleep timer - 36), the chapters (tap: play from
// there) and the show notes. The player bar opens this page for the
// current episode (episode page and player page are one since build 12).
// For another episode the slider shows its stored position; moving it or
// tapping play starts that episode.
Page {
    id: page
    objectName: "episodePage"
    property int episodeId: -1
    // Opened from the playlist page: playing starts the playlist context
    property bool fromPlaylist: false
    property var episode: core.episode(episodeId)

    readonly property var player: appWindow.player

    // (258, Thomas 6 Nov: ten "Up next" rows pushed the show notes off the
    // screen) Tabs as in the player - Chapters, Shownotes, Bookmarks, Up
    // next, each only with content; Shownotes first shown (else the first)
    property string chosenTab: ""
    readonly property var tabList: {
        var list = []
        if ((episode.chapters || []).length > 0) list.push({ key: "chapters", text: qsTr("Chapters") })
        if (("" + (episode.description || "")).length > 0) list.push({ key: "notes", text: qsTr("Shownotes") })
        if (bookmarkList.length > 0) list.push({ key: "bookmarks", text: qsTr("Bookmarks") })
        if (isCurrent && upNextList.length > 0) list.push({ key: "next", text: qsTr("Up next") })
        return list
    }
    readonly property string curTab: {
        var first = ""
        for (var i = 0; i < tabList.length; i++) {
            if (tabList[i].key === chosenTab) return chosenTab
            if (tabList[i].key === "notes") first = "notes"
        }
        return first !== "" ? first : (tabList.length > 0 ? tabList[0].key : "")
    }

    // (256) The next playlist episodes while this one plays
    property var upNextList: []
    function refreshUpNext() {
        upNextList = isCurrent ? core.upNext(episodeId, 10) : []
    }
    Connections {
        target: core
        onQueueChanged: page.refreshUpNext()
    }
    // (256) ⏭ on this page: next chapter, after the last one the next
    // episode - the page then follows the player to that episode (so a video
    // keeps playing on it)
    property bool _followPlayer: false
    function next() {
        _followPlayer = true
        followTimer.restart()
        player.next()
    }
    Timer {
        id: followTimer
        interval: 3000
        onTriggered: page._followPlayer = false
    }
    Connections {
        target: appWindow.player
        onEpisodeIdChanged: {
            if (page._followPlayer && appWindow.player.episodeId > 0
                    && appWindow.player.episodeId !== page.episodeId) {
                page._followPlayer = false
                page.episodeId = appWindow.player.episodeId
                page.episode = core.episode(page.episodeId)
            }
        }
    }
    readonly property bool isCurrent: player.episodeId === episodeId
    readonly property bool isPlaying: isCurrent && player.playing
    // (241) The episode in the player has a picture
    readonly property bool showVideo: isCurrent && player.hasVideo
    // (245) The video area: the episode playing is a video, or its feed
    // offers a video version (then the switch can turn it on)
    readonly property bool showVideoArea: isCurrent && (player.hasVideo || episode.hasVideoAlternate === true)
    // (245) The picture really shown (not "sound only")
    readonly property bool pictureShown: showVideo && !player.pictureOff
    // (245) The app counts as away only after 1 s: unlocking the screen
    // makes it inactive for a moment - the output was taken away and a new
    // one got no picture
    property bool appAway: false
    // (247) The page counts as left only 1.5 s after it went inactive: going
    // on to another page and back, or through it, made two reloads (two
    // jumps); the output goes once, or not at all
    property bool pageAway: false
    // (257) The page has been active once (end of the push animation)
    property bool _arrived: false
    Timer {
        id: pageAwayTimer
        interval: 1500
        onTriggered: page.pageAway = page.status === PageStatus.Inactive
    }
    onStatusChanged: {
        if (status === PageStatus.Active) {
            _arrived = true
        }
        if (status === PageStatus.Inactive) {
            pageAwayTimer.restart()
        } else {
            pageAwayTimer.stop()
            pageAway = false
        }
    }
    Timer {
        id: awayTimer
        interval: 1000
        onTriggered: page.appAway = Qt.application.state !== Qt.ApplicationActive
    }
    Connections {
        target: Qt.application
        onStateChanged: {
            if (Qt.application.state === Qt.ApplicationActive) {
                awayTimer.stop()
                page.appAway = false
            } else {
                awayTimer.restart()
            }
        }
    }
    // (245) Switch video <-> sound only: a video version in the feed ->
    // the podcast's version (audio file instead of the video stream, saves
    // data); else only the picture off for this episode
    function toggleVideo() {
        if (episode.hasVideoAlternate === true) {
            var key = "podcast." + episode.podcastId + ".version"
            core.setSetting(key, core.setting(key, "") === "video" ? "" : "video")
            player.pictureOff = false
            player.reloadVersion(episode.podcastId)
        } else {
            player.pictureOff = !player.pictureOff
        }
    }
    // (245) Camera symbol: a video file, or a video version in the feed
    readonly property bool isVideoEpisode: core.mimeType(episodeId).indexOf("video/") === 0
                                         || core.mimeType(episodeId).toLowerCase().indexOf("mpegurl") >= 0
                                         || episode.hasVideoAlternate === true
    // (242) Full screen on this page: landscape, black, display kept on,
    // no back gesture (241: the own page could not be left) - "✕" ends it
    property bool videoFull: false
    property bool videoControls: true
    onPictureShownChanged: if (!pictureShown) videoFull = false
    onVideoFullChanged: videoControls = true
    allowedOrientations: videoFull ? (Orientation.Landscape | Orientation.LandscapeInverted) : Orientation.All
    backNavigation: !videoFull
    showNavigationIndicator: !videoFull
    readonly property var speeds: [0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 1.75, 2.0]
    readonly property var sleepMinutes: [0, 5, 10, 15, 30, 45, 60, -1, -2]   // -1: end of episode, -2: own duration (121)

    // Position and length shown by the slider: live for the current episode
    readonly property int shownPosition: isCurrent ? player.position : (episode.played ? 0 : (episode.position || 0))
    // (200) shownPosition for the pull-down menu, frozen while it is open
    // (no binding: it would go on changing while the menu is open)
    property int sharePosition: 0
    onShownPositionChanged: {
        if (!pullMenu.active) {
            sharePosition = shownPosition
        }
    }
    readonly property int shownDuration: isCurrent ? player.duration : (episode.duration || 0)

    // 1 -> "1,0×", 1.75 -> "1,75×" (decimal separator of the locale)
    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
    }
    property bool downloading: core.isDownloading(episodeId)
    property real downloadProgress: core.downloadProgress(episodeId)
    property bool queued: core.isQueued(episodeId)
    // Bookmarks of this episode (47)
    property var bookmarkList: core.bookmarks(episodeId)

    // The bookmark is stored right away; a small panel at the bottom then
    // offers its name and note (89) - tap beside it or swipe it down when
    // not needed. What was typed is kept when the panel closes.
    function addBookmark() {
        var seconds = page.isCurrent ? page.player.position : page.shownPosition
        var id = core.addBookmark(page.episodeId, seconds)
        if (id > 0) {
            bookmarkPanel.show(id, seconds)
        }
    }
    // Time stamps such as "12:34" or "1:02:03" in the show notes become
    // links that jump there (53) - only in text, not inside tags or links,
    // and only within the episode's length
    // (256) Shared with the Notes tab of the player (TimeFormat)
    function linkTimestamps(html) {
        return TimeFormat.linkTimestamps(html, page.shownDuration)
    }

    // Captions under the toolbar symbols for the first few visits (50)
    readonly property bool showHints: _hintCount < 5
    property int _hintCount: parseInt(core.setting("hint.toolbar", "0"))
    Component.onCompleted: {
        sharePosition = shownPosition
        core.setSetting("hint.toolbar", "" + (_hintCount + 1))
        refreshUpNext()
        // How bookmarks work: since 220 when the bookmark panel opens the
        // first time (BookmarkPanel)
    }

    // Open row of choices below the toolbar: "", "speed", "sleep"
    property string choices: ""
    onIsCurrentChanged: {
        if (!isCurrent) choices = ""
        refreshUpNext()
    }

    Connections {
        target: core
        onEpisodeChanged: {
            if (episodeId === page.episodeId) {
                page.episode = core.episode(page.episodeId)
                page.downloading = core.isDownloading(page.episodeId)
            }
        }
        onDownloadProgressChanged: {
            if (episodeId === page.episodeId) {
                page.downloading = true
                page.downloadProgress = progress
            }
        }
        onDownloadFinished: {
            if (episodeId === page.episodeId) {
                page.downloading = false
            }
        }
        onQueueChanged: page.queued = core.isQueued(page.episodeId)
        onBookmarksChanged: {
            if (episodeId === 0 || episodeId === page.episodeId) {
                page.bookmarkList = core.bookmarks(page.episodeId)
            }
        }
        onDataChanged: page.episode = core.episode(page.episodeId)
    }

    // Landscape (54): two columns - the player on the left, bookmarks,
    // chapters and show notes on the right, each scrolling on its own.
    // The info column moves between the two flickables.
    readonly property bool twoColumns: page.isLandscape

    SilicaFlickable {
        id: flick
        anchors {
            top: parent.top
            bottom: parent.bottom
            bottomMargin: appWindow.playerBarHeight
            left: parent.left
        }
        width: page.twoColumns ? Math.round(page.width / 2) : page.width
        clip: true
        contentHeight: column.height + (page.twoColumns ? 0 : Theme.paddingMedium + infoColumn.height) + Theme.paddingLarge

        PullDownMenu {
            id: pullMenu
            // (259, Thomas 6 Nov) Same order as the row menu: file, status,
            // other; then what only this page has
            MenuItem {
                visible: page.downloading
                text: qsTr("Cancel download")
                onClicked: core.cancelDownload(page.episodeId)
            }
            // Started episodes offer both: finish them or start over (22)
            MenuItem {
                visible: page.episode.played !== true
                text: qsTr("Mark as played")
                onClicked: {
                    if (page.isCurrent) {
                        page.player.pause()
                    }
                    core.markPlayed(page.episodeId)
                }
            }
            MenuItem {
                visible: page.episode.played === true || (page.episode.position || 0) > 0
                text: qsTr("Mark as unplayed")
                onClicked: {
                    if (page.isCurrent) {
                        page.player.pause()
                        page.player.seek(0)
                    }
                    core.markUnplayed(page.episodeId)
                }
            }
            // Listened to without subscribing (147): keeps the listening state
            MenuItem {
                visible: page.episode.provisional === true
                text: qsTr("Subscribe to the podcast")
                onClicked: appWindow.subscribeWithFolder(page.episode.podcastUrl || "", page.episode.podcastTitle || "")
            }
            MenuItem {
                text: qsTr("Show podcast")
                onClicked: pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: page.episode.podcastId })
            }
            // (83, 259) One entry: link, link from the shown position, file
            // (200) The position is taken before the menu opens and kept
            // while it is open: its text changed every second while
            // playing, and the pulley got stuck on a second pull
            MenuItem {
                text: qsTr("Share...")
                onClicked: pageStack.push(Qt.resolvedUrl("ShareChoicePage.qml"),
                                          { episodeId: page.episodeId, position: page.sharePosition })
            }
            MenuItem {
                visible: page.episode.downloaded === true && page.episode.kept !== true
                // (245) Video files go to Videos
                text: ("" + (page.episode.mimeType || "")).indexOf("video/") === 0 ? qsTr("Save in Videos") : qsTr("Save in Music")
                onClicked: core.keepEpisode(page.episodeId)
            }
            MenuItem {
                visible: (page.episode.link || "").length > 0
                text: qsTr("Open in browser")
                onClicked: Qt.openUrlExternally(page.episode.link)
            }
            MenuItem {
                visible: page.episode.hasChaptersUrl === true && page.episode.chapterCount === 0
                text: qsTr("Load chapters")
                onClicked: core.loadChapters(page.episodeId)
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: page.episode.podcastTitle || ""
                // (156) Listened to without subscribing
                description: page.episode.provisional === true ? qsTr("not subscribed") : ""
            }

            // (199) Not subscribed: "Abonnieren ›" right on the page (until
            // then only in the pull-down menu), below the header's "nicht
            // abonniert". (203) Only as high as its text, right below the
            // header (a full row centred it too far down); the touch area
            // keeps the height of a row, reaching up over the header
            Item {
                visible: page.episode.provisional === true
                width: parent.width
                height: subscribeLabel.height
                BackgroundItem {
                    id: subscribeLink
                    // Only around the words, not the whole width
                    anchors.right: parent.right
                    width: subscribeLabel.width + 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeExtraSmall
                    y: parent.height - height
                    onClicked: appWindow.subscribeWithFolder(page.episode.podcastUrl || "", page.episode.podcastTitle || "")
                }
                Label {
                    id: subscribeLabel
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    text: qsTr("Subscribe") + " ›"
                    font.pixelSize: Theme.fontSizeSmall
                    color: subscribeLink.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }

            // (241) Video episode playing: the picture instead of the
            // image, 16:9 over the width; a tap = full screen on this page
            // (242: the same VideoView moves into videoLayer - a second
            // output on its own page stayed black)
            BackgroundItem {
                id: videoArea
                visible: page.showVideoArea
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Math.round(width * 9 / 16)
                onClicked: if (page.pictureShown) page.videoFull = true

                VideoView {
                    id: videoView
                    parent: page.videoFull ? videoLayer : videoArea
                    anchors.fill: parent
                    // (243) Only while the page is on screen: an output that
                    // is not drawn stalled the player (242); the player
                    // loads again on each change (PlayerController)
                    // (245) not while "sound only"; short inactive phases
                    // (unlocking) keep it (appAway)
                    // (247) and leaving the page only after 1.5 s (pageAway)
                    // (257) only once the page has come in (not during the
                    // push animation: creating the output meant a reload)
                    wanted: page.pictureShown && page._arrived && !page.pageAway && !page.appAway
                    // (245) The episode image while it does not play, with
                    // "sound only" or before the picture comes
                    posterVisible: !page.pictureShown || !page.player.playing
                    episodeImageUrl: page.episode.episodeImageUrl || ""
                    coverUrl: page.episode.coverUrl || ""
                    fallbackText: page.episode.podcastTitle || ""
                }
            }

            // (250) Switch video <-> sound only, in a row below the picture:
            // as an overlay on the picture the player's video covered it
            Item {
                visible: page.showVideoArea
                width: parent.width
                height: Theme.itemSizeExtraSmall

                // (251) "Sound only" without an audio version in the feed
                // only hides the picture: the video file is still loaded
                Label {
                    visible: !page.pictureShown && page.episode.hasVideoAlternate !== true
                    anchors {
                        left: parent.left
                        leftMargin: Theme.horizontalPageMargin
                        right: videoSwitch.left
                        rightMargin: Theme.paddingMedium
                        verticalCenter: parent.verticalCenter
                    }
                    text: qsTr("The video is still loaded – this saves no data")
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                }

                BackgroundItem {
                    id: videoSwitch
                    anchors {
                        right: parent.right
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    width: switchRow.width + 2 * Theme.paddingLarge
                    height: Theme.itemSizeExtraSmall * 0.8
                    onClicked: page.toggleVideo()

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: Theme.rgba(Theme.highlightBackgroundColor, 0.25)
                    }
                    Row {
                        id: switchRow
                        anchors.centerIn: parent
                        spacing: Theme.paddingSmall
                        VideoIcon {
                            visible: !page.pictureShown
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.iconSizeExtraSmall
                            color: videoSwitch.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.pictureShown ? qsTr("Sound only") : qsTr("Video")
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: videoSwitch.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                }
            }

            EpisodeArt {
                visible: !page.showVideoArea
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 6 * Theme.horizontalPageMargin, page.height * (page.twoColumns ? 0.4 : 0.28))
                height: width
                episodeImageUrl: page.episode.episodeImageUrl || ""
                coverUrl: page.episode.coverUrl || ""
                fallbackText: page.episode.podcastTitle || ""
                // The podcast is in the page header (96)
                showBadge: false

                SleepBadge {
                    visible: page.isCurrent && running
                    anchors {
                        top: parent.top
                        left: parent.left
                        margins: Theme.paddingSmall
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: page.episode.title || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
            }

            Label {
                visible: page.isCurrent && page.player.chapterTitle.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: page.player.chapterTitle
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }

            // (245) Camera symbol for a video episode before the status line
            Row {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall

                VideoIcon {
                    id: episodeVideoIcon
                    visible: page.isVideoEpisode
                    y: Math.round((Theme.fontSizeSmall * 1.3 - height) / 2)
                    width: Theme.iconSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                }

                Label {
                    width: parent.width - (episodeVideoIcon.visible ? episodeVideoIcon.width + Theme.paddingSmall : 0)
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    text: {
                        var e = page.episode
                        var parts = [TimeFormat.day(e.published, qsTr("today"), qsTr("yesterday"))]
                        // Season / episode number from the feed (83)
                        if (e.episodeNumber > 0) {
                            parts.unshift(e.season > 0 ? qsTr("S%1 E%2").arg(e.season).arg(e.episodeNumber)
                                                       : qsTr("#%1").arg(e.episodeNumber))
                        }
                        if (e.duration > 0) {
                            parts.push(TimeFormat.length(e.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                        }
                        if (e.played) {
                            parts.push(qsTr("played"))
                        } else if (e.position > 0 && e.remaining > 0) {
                            parts.push(qsTr("%1 left").arg(TimeFormat.length(e.remaining, qsTr("%1 h %2 min"), qsTr("%1 min"))))
                        }
                        // (192) "downloaded" and "in playlist" are shown by the
                        // toolbar just below (delete / check mark) - the line wrapped
                        return parts.join(" · ")
                    }
                }
            }

            // Position: only the knob can be dragged (see SeekBar.qml)
            // A bit narrower than the page, centered
            SeekBar {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                position: page.shownPosition
                duration: page.shownDuration
                onSeekRequested: {
                    if (page.isCurrent) {
                        page.player.seek(seconds)
                    } else {
                        page.player.playFrom(page.episodeId, seconds, page.fromPlaylist)
                    }
                }
            }

            // Toolbar (36, after Podcast Addict): speed · download · playlist
            // · bookmark · sleep timer. Symbols only since 50 (five labels
            // overlapped); short captions as a hint for the first 5 times the
            // episode page is opened, status texts (download %, sleep
            // minutes, "podcast" speed) always. Speed and sleep timer open a
            // row of choices below (only for the episode playing now).
            Row {
                id: toolbar
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                readonly property real cellWidth: width / 5

                ToolbarButton {
                    width: toolbar.cellWidth
                    enabled: page.isCurrent
                    active: page.choices === "speed"
                    text: page.speedText(page.player.rate)
                    label: page.showHints ? qsTr("Speed") : (page.player.podcastRate > 0 ? qsTr("podcast") : "")
                    HoldNameBubble { id: speedBubble; attached: true; name: qsTr("Speed") }
                    onClicked: {
                        if (speedBubble.takeHeld()) return
                        page.choices = page.choices === "speed" ? "" : "speed"
                    }
                }
                ToolbarButton {
                    width: toolbar.cellWidth
                    enabled: !page.downloading || !page.episode.downloaded
                    iconSource: page.episode.downloaded ? "image://theme/icon-m-delete" : "image://theme/icon-m-cloud-download"
                    label: page.downloading ? Math.round(page.downloadProgress * 100) + " %"
                                            : (page.showHints ? (page.episode.downloaded ? qsTr("Delete") : qsTr("Load")) : "")
                    HoldNameBubble { id: downloadBubble; attached: true; name: page.downloading ? qsTr("Cancel download") : (page.episode.downloaded ? qsTr("Delete download") : qsTr("Download")) }
                    onClicked: {
                        if (downloadBubble.takeHeld()) return
                        if (page.downloading) {
                            core.cancelDownload(page.episodeId)
                        } else if (page.episode.downloaded) {
                            remorse.execute(qsTr("Deleting download"), function() { core.deleteDownload(page.episodeId) })
                        } else {
                            appWindow.requestDownload(page.episodeId)
                        }
                    }
                }
                ToolbarButton {
                    width: toolbar.cellWidth
                    active: page.queued
                    // (194) The playlist symbol of the lists (191) - highlighted
                    // when queued; the "+" sat in a filled disc among outlines.
                    // (254) Our own symbol, as in the episode rows: lines with
                    // "+" / check mark (the theme's looked like a play button)
                    playlistMark: page.queued ? 1 : 0
                    label: page.showHints ? qsTr("Playlist") : ""
                    HoldNameBubble { id: queueBubble; attached: true; name: page.queued ? qsTr("Remove from playlist") : qsTr("Add to end of playlist") }
                    onClicked: {
                        if (queueBubble.takeHeld()) return
                        page.queued ? core.queueRemove(page.episodeId) : core.queueAdd(page.episodeId, false)
                    }
                }
                // Bookmark at the current position (47)
                ToolbarButton {
                    width: toolbar.cellWidth
                    // Always the empty star (71: filled after the first
                    // bookmark it looked as if a second tap would undo it); the number of bookmarks below instead
                    bookmarkMark: 0
                    label: page.showHints ? qsTr("Mark")
                                          : (page.bookmarkList.length > 0 ? "" + page.bookmarkList.length : "")
                    HoldNameBubble { id: bookmarkBubble; attached: true; name: qsTr("Bookmark") }
                    onClicked: {
                        if (bookmarkBubble.takeHeld()) return
                        page.addBookmark()
                    }
                }
                ToolbarButton {
                    width: toolbar.cellWidth
                    enabled: page.isCurrent
                    moon: true
                    active: page.choices === "sleep" || appWindow.sleepTimer.active || page.player.stopAtEnd
                    label: appWindow.sleepTimer.active
                           ? qsTr("%n min", "", appWindow.sleepTimer.remainingMinutes)
                           // (253, usability audit #33) "End" alone was
                           // cryptic without the dropdown's own wording
                           : (page.player.stopAtEnd ? qsTr("Ends here") : (page.showHints ? qsTr("Timer") : ""))
                    HoldNameBubble { id: sleepBubble; attached: true; name: qsTr("Sleep timer · stop after this episode") }
                    onClicked: {
                        if (sleepBubble.takeHeld()) return
                        page.choices = page.choices === "sleep" ? "" : "sleep"
                    }
                }
            }

            // Choices of the speed or sleep timer button
            Flow {
                visible: page.choices !== "" && page.isCurrent
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                Repeater {
                    model: page.choices === "speed" ? page.speeds : (page.choices === "sleep" ? page.sleepMinutes : [])
                    // By id (145): see the tabs of SearchPage
                    BackgroundItem {
                        id: choiceItem
                        readonly property bool current: page.choices === "speed"
                                                        ? Math.abs(modelData - page.player.rate) < 0.01
                                                        : (modelData === 0 ? !appWindow.sleepTimer.active && !page.player.stopAtEnd
                                                                           : (modelData < 0 && page.player.stopAtEnd))
                        width: choiceLabel.implicitWidth + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        Label {
                            id: choiceLabel
                            anchors.centerIn: parent
                            text: page.choices === "speed" ? page.speedText(modelData)
                                  : (modelData === 0 ? qsTr("Off")
                                     : (modelData === -2 ? qsTr("Other …")
                                        : (modelData < 0 ? qsTr("End of episode") : qsTr("%n min", "", modelData))))
                            font.bold: choiceItem.current
                            color: choiceItem.current || choiceItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        onClicked: {
                            if (page.choices === "speed") {
                                page.player.setRate(modelData)
                            } else if (modelData === -2) {
                                appWindow.chooseSleepTime()
                            } else {
                                appWindow.sleepTimer.cancel()
                                page.player.stopAtEnd = false
                                if (modelData > 0) {
                                    appWindow.sleepTimer.start(modelData)
                                } else if (modelData < 0) {
                                    page.player.stopAtEnd = true
                                }
                            }
                            page.choices = ""
                        }
                    }
                }
            }

            // Chapters / skip / play
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    // (258, Thomas: the two buttons appeared only after
                    // Play) always there, dimmed when this is not the
                    // episode playing
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    icon.source: "image://theme/icon-m-previous"
                    HoldNameBubble { id: prevChapterBubble; attached: true; name: qsTr("Previous chapter") }
                    onClicked: {
                        if (prevChapterBubble.takeHeld()) return
                        page.player.previousChapter()
                    }
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    seconds: -page.player.skipBackSeconds
                    onSkip: page.player.skip(seconds)
                }
                IconButton {
                    id: playButton
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeMedium
                    height: width
                    icon.source: page.isPlaying ? "image://theme/icon-m-pause" : "image://theme/icon-m-play"
                    HoldNameBubble { id: playBubble; attached: true; name: page.isPlaying ? qsTr("Pause") : qsTr("Play") }
                    onClicked: {
                        if (playBubble.takeHeld()) return
                        page.player.playOrToggle(page.episodeId, page.fromPlaylist)
                    }

                    BusyIndicator {
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Medium
                        running: page.isCurrent && page.player.waiting
                    }
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    seconds: page.player.skipForwardSeconds
                    onSkip: page.player.skip(seconds)
                }
                // (256, Thomas) For the episode playing always (not only with
                // chapters): next chapter, after the last one the next
                // episode - as in the player bar (before: next chapter only,
                // so nothing happened in the last chapter)
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    icon.source: "image://theme/icon-m-next"
                    HoldNameBubble { id: nextBubble; attached: true; name: qsTr("Next chapter or episode") }
                    onClicked: {
                        if (nextBubble.takeHeld()) return
                        page.next()
                    }
                }
            }
        }

        Column {
            id: infoColumn
            parent: page.twoColumns ? infoFlick.contentItem : flick.contentItem
            y: page.twoColumns ? 0 : column.height + Theme.paddingMedium
            width: page.twoColumns ? infoFlick.width : flick.width
            spacing: Theme.paddingMedium

            Item {
                visible: page.twoColumns
                width: 1
                height: Theme.paddingLarge
            }

            // (258) The tabs; one alone needs no bar
            Row {
                visible: page.tabList.length > 1
                width: infoColumn.width
                Repeater {
                    model: page.tabList
                    BackgroundItem {
                        id: pageTabItem
                        width: infoColumn.width / Math.max(1, page.tabList.length)
                        height: Theme.itemSizeExtraSmall
                        readonly property bool current: page.curTab === modelData.key
                        onClicked: page.chosenTab = modelData.key
                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 2 * Theme.paddingSmall
                            horizontalAlignment: Text.AlignHCenter
                            truncationMode: TruncationMode.Fade
                            text: modelData.text
                            font.pixelSize: Theme.fontSizeSmall
                            color: pageTabItem.current || pageTabItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 2 * Theme.paddingLarge
                            height: Theme.paddingSmall / 2
                            color: Theme.highlightColor
                            visible: pageTabItem.current
                        }
                    }
                }
            }

            // Bookmarks (47): tap plays from there; menu: note, delete
            Repeater {
                model: page.curTab === "bookmarks" ? page.bookmarkList : []
                delegate: ListItem {
                    id: bookmarkRow
                    width: infoColumn.width
                    contentHeight: Math.max(Theme.itemSizeSmall, bookmarkText.height + 2 * Theme.paddingSmall)
                    onClicked: page.player.playFrom(page.episodeId, modelData.position, page.fromPlaylist)
                    menu: Component {
                        ContextMenu {
                            MenuItem {
                                text: qsTr("Rename / note")
                                onClicked: appWindow.editBookmark(modelData)
                            }
                            MenuItem {
                                text: qsTr("Delete bookmark")
                                // Countdown in the app window (not in this menu)
                                onClicked: appWindow.remorseDeleteBookmark(modelData.bookmarkId)
                            }
                        }
                    }
                    Label {
                        id: bookmarkTime
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        text: TimeFormat.clock(modelData.position)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Column {
                        id: bookmarkText
                        anchors {
                            left: bookmarkTime.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            // (191) Unnamed ones numbered, as in the player bar
                            text: modelData.title.length > 0 ? modelData.title : qsTr("Bookmark %1").arg(index + 1)
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeSmall
                            color: bookmarkRow.highlighted ? Theme.highlightColor
                                                           : (modelData.title.length > 0 ? Theme.primaryColor : Theme.secondaryColor)
                        }
                        Label {
                            visible: modelData.note.length > 0
                            width: parent.width
                            text: modelData.note
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryHighlightColor
                        }
                    }
                }
            }

            // (256, "one player view") Up next - for the episode playing
            // only, the next 10 of the playlist (as the player bar); a tap
            // plays one; "Full playlist ›" below. A Repeater, not a list
            // view inside the page's flickable (Qt 5.6 scroll conflicts).
            Repeater {
                model: page.isCurrent && page.curTab === "next" ? page.upNextList : []
                delegate: BackgroundItem {
                    id: upNextRow
                    width: infoColumn.width
                    height: Theme.itemSizeSmall
                    onClicked: page.player.playOrToggle(modelData.episodeId, modelData.followUp !== true)
                    EpisodeArt {
                        id: upNextArt
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.iconSizeMedium
                        height: Theme.iconSizeMedium
                        episodeImageUrl: modelData.episodeImageUrl || ""
                        coverUrl: modelData.coverUrl || ""
                        fallbackText: modelData.podcastTitle || ""
                        badgeFactor: 0.46
                    }
                    Column {
                        anchors {
                            left: upNextArt.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            text: modelData.title
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeSmall
                            color: upNextRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            text: modelData.podcastTitle || ""
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeTiny
                            color: upNextRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                    }
                }
            }
            BackgroundItem {
                visible: page.isCurrent && page.curTab === "next" && page.upNextList.length > 0
                width: infoColumn.width
                height: Theme.itemSizeExtraSmall
                onClicked: appWindow.showPlaylist()
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Full playlist") + " \u203a"
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }

            // Chapters
            Repeater {
                id: chapterRepeater
                model: page.curTab === "chapters" ? (page.episode.chapters || []) : []
                delegate: BackgroundItem {
                    readonly property bool current: page.isCurrent && index === page.player.chapterIndex
                    width: infoColumn.width
                    height: Theme.itemSizeSmall
                    highlighted: down || current
                    onClicked: page.player.playFrom(page.episodeId, modelData.start / 1000, page.fromPlaylist)
                    Label {
                        id: chapterTime
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        text: TimeFormat.clock(modelData.start / 1000)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        anchors {
                            left: chapterTime.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.title
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }

            // Show notes
            Label {
                id: notes
                visible: page.curTab === "notes"
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: page.linkTimestamps(page.episode.description || "")
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                linkColor: Theme.highlightColor
                onLinkActivated: {
                    // Time stamps in the show notes jump there (53)
                    if (link.indexOf("sailcast-seek:") === 0) {
                        var seconds = parseInt(link.substring(14))
                        if (page.isCurrent) {
                            page.player.seek(seconds)
                            if (!page.player.playing) {
                                page.player.resume()
                            }
                        } else {
                            page.player.playFrom(page.episodeId, seconds, page.fromPlaylist)
                        }
                        return
                    }
                    Qt.openUrlExternally(link)
                }
            }
        }

        VerticalScrollDecorator {}
    }

    // Right column in landscape (54)
    SilicaFlickable {
        id: infoFlick
        visible: page.twoColumns
        anchors {
            top: parent.top
            bottom: parent.bottom
            bottomMargin: appWindow.playerBarHeight
            right: parent.right
        }
        width: page.width - flick.width
        clip: true
        contentHeight: infoColumn.height + Theme.paddingLarge

        VerticalScrollDecorator {}
    }

    RemorsePopup {
        id: remorse
    }

    // (242) Full-screen video over the whole page
    DisplayBlanking {
        // (245) also on the page itself while a video plays
        preventBlanking: page.pictureShown && page.player.playing && Qt.application.active
                         && page.status === PageStatus.Active
    }

    Rectangle {
        id: videoLayer
        anchors.fill: parent
        z: 200
        visible: page.videoFull
        color: "black"
    }

    Item {
        anchors.fill: parent
        z: 201
        visible: page.videoFull

        MouseArea {
            anchors.fill: parent
            onClicked: {
                page.videoControls = !page.videoControls
                if (page.videoControls) {
                    videoHideTimer.restart()
                }
            }
        }

        Timer {
            id: videoHideTimer
            interval: 4000
            running: page.videoFull && page.videoControls && page.player.playing
            onTriggered: page.videoControls = false
        }

        // Top: title and "✕"
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            height: Theme.itemSizeMedium
            color: Qt.rgba(0, 0, 0, 0.6)
            opacity: page.videoControls ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { FadeAnimation {} }

            MouseArea {
                anchors.fill: parent
                onClicked: videoHideTimer.restart()
            }

            Label {
                anchors {
                    left: parent.left
                    leftMargin: Theme.horizontalPageMargin
                    right: fullSwitch.left
                    rightMargin: Theme.paddingSmall
                    verticalCenter: parent.verticalCenter
                }
                text: page.episode.title || ""
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
            }
            // (249) "Sound only" in full screen too (ends full screen)
            BackgroundItem {
                id: fullSwitch
                anchors {
                    right: closeButton.left
                    verticalCenter: parent.verticalCenter
                }
                width: fullSwitchLabel.width + 2 * Theme.paddingMedium
                height: Theme.itemSizeExtraSmall * 0.8
                onClicked: {
                    videoHideTimer.restart()
                    page.toggleVideo()
                }

                Rectangle {
                    anchors.fill: parent
                    radius: height / 2
                    color: Qt.rgba(1, 1, 1, 0.15)
                }
                Label {
                    id: fullSwitchLabel
                    anchors.centerIn: parent
                    text: qsTr("Sound only")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: fullSwitch.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }
            IconButton {
                id: closeButton
                anchors {
                    right: parent.right
                    rightMargin: Theme.paddingMedium
                    verticalCenter: parent.verticalCenter
                }
                icon.source: "image://theme/icon-m-close"
                onClicked: page.videoFull = false
            }
        }

        // Bottom: position and skip / play / skip
        Rectangle {
            anchors {
                left: parent.left
                right: parent.right
                bottom: parent.bottom
            }
            height: videoControlsColumn.height + 2 * Theme.paddingMedium
            color: Qt.rgba(0, 0, 0, 0.6)
            opacity: page.videoControls ? 1 : 0
            visible: opacity > 0
            Behavior on opacity { FadeAnimation {} }

            MouseArea {
                anchors.fill: parent
                onClicked: videoHideTimer.restart()
            }

            Column {
                id: videoControlsColumn
                y: Theme.paddingMedium
                width: parent.width

                SeekBar {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    compact: true
                    position: page.player.position
                    duration: page.player.duration
                    onSeekRequested: {
                        page.player.seek(seconds)
                        videoHideTimer.restart()
                    }
                }

                Row {
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.paddingLarge

                    SkipButton {
                        anchors.verticalCenter: parent.verticalCenter
                        seconds: -page.player.skipBackSeconds
                        onSkip: {
                            page.player.skip(seconds)
                            videoHideTimer.restart()
                        }
                    }
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        icon.source: page.player.playing ? "image://theme/icon-l-pause" : "image://theme/icon-l-play"
                        onClicked: {
                            page.player.toggle()
                            videoHideTimer.restart()
                        }
                    }
                    SkipButton {
                        anchors.verticalCenter: parent.verticalCenter
                        seconds: page.player.skipForwardSeconds
                        onSkip: {
                            page.player.skip(seconds)
                            videoHideTimer.restart()
                        }
                    }
                    // (256, Thomas: no way to the next episode while watching)
                    IconButton {
                        anchors.verticalCenter: parent.verticalCenter
                        icon.source: "image://theme/icon-m-next"
                        onClicked: {
                            page.next()
                            videoHideTimer.restart()
                        }
                    }
                }
            }
        }
    }

    // Name and note of a new bookmark (89, BookmarkPanel since 171)
    BookmarkPanel {
        id: bookmarkPanel
        width: page.width
        // Above the page's flickables (two columns in landscape)
        z: 100
    }
    // Leaving the page with the panel open keeps what was typed
    Connections {
        target: page
        onStatusChanged: {
            if (page.status === PageStatus.Deactivating && bookmarkPanel.open) {
                bookmarkPanel.open = false
            }
        }
    }
}

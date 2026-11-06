// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// The playlist page, top to bottom (order since 24):
//   "Now playing"        - the current episode, always shown here; no
//                          context menu (tap: episode page with all actions)
//   "Playlist"           - the manual queue: episodes added with "Play
//                          next" / "Add to end of playlist"; started from
//                          here they are played one after the other (see
//                          PlayerController, playback context); a played
//                          episode leaves it. Sorted by dragging the handle
//                          on the left; dragged onto the area below the
//                          last episode ("Release here to remove") it
//                          leaves the list.
//   "Continue listening" - episodes in progress, most recently played
//                          first; collapsible (tap the header): collapsed
//                          only the header with the number, expanded the
//                          latest 5 and "Show all"; the state is stored.
// "Now playing" is in the list header, "Continue listening" in the footer:
// a growing header of a ListView grows upwards on Qt 5.6 and made open
// context menus jump away (23) - a footer grows downwards.
// The current episode appears only once, under "Now playing".
// Part of the ring navigation.
Page {
    id: page
    objectName: "playlistPage"
    property bool ringMember: false

    readonly property int currentId: appWindow.player.hasEpisode ? appWindow.player.episodeId : -1
    readonly property int collapsedRows: 5
    property bool continueExpanded: core.setting("ui.continueExpanded", "0") === "1"

    function toggleContinue() {
        list._keepTop = false
        continueExpanded = !continueExpanded
        core.setSetting("ui.continueExpanded", continueExpanded ? "1" : "0")
    }

    EpisodeListModel {
        id: nowPlaying
        filter: EpisodeListModel.Single
        episodeId: page.currentId
    }

    EpisodeListModel {
        id: continuing
        filter: EpisodeListModel.InProgress
        excludeEpisodeId: page.currentId
        // One more than shown, to know whether "Show all" is needed
        limit: page.collapsedRows + 1
        onCountChanged: page.refreshContinueTotal()
    }

    // Number for the collapsed header
    property int continueTotal: 0

    function refreshContinueTotal() {
        continueTotal = core.inProgressCount(currentId)
    }

    onCurrentIdChanged: refreshContinueTotal()
    Component.onCompleted: refreshContinueTotal()

    Connections {
        target: core
        onDataChanged: page.refreshContinueTotal()
    }

    // (188) "Recently heard": heard to the end or skipped in the player,
    // newest first - to come back to a skipped one; collapsed by default
    property bool recentExpanded: core.setting("ui.recentExpanded", "0") === "1"
    function toggleRecent() {
        list._keepTop = false
        recentExpanded = !recentExpanded
        core.setSetting("ui.recentExpanded", recentExpanded ? "1" : "0")
    }
    EpisodeListModel {
        id: recent
        filter: EpisodeListModel.Recent
        excludeEpisodeId: page.currentId
        limit: 10
    }

    // (190) Tiles below the header: time left, episodes, downloaded
    // (246) The other playlist episodes from the core; the one playing
    // added here with its live position - counted in both tiles, the time
    // at the current speed, updated every 5 s while it plays
    readonly property int playingId: appWindow.player.hasEpisode ? appWindow.player.episodeId : -1
    property var queueStats: core.queueStats(playingId)
    onPlayingIdChanged: queueStats = core.queueStats(playingId)
    Connections {
        target: core
        onQueueChanged: page.queueStats = core.queueStats(page.playingId)
        onDataChanged: page.queueStats = core.queueStats(page.playingId)
    }
    property int livePosition: appWindow.player.position
    Timer {
        interval: 5000
        repeat: true
        running: appWindow.player.playing && page.status === PageStatus.Active
        onTriggered: page.livePosition = appWindow.player.position
    }
    Connections {
        target: appWindow.player
        onEpisodeIdChanged: page.livePosition = appWindow.player.position
        onPlayingChanged: page.livePosition = appWindow.player.position
    }
    readonly property bool playingCounts: playingId > 0 && appWindow.player.info.played !== true
    readonly property int tileCount: (queueStats.count || 0) + (playingCounts ? 1 : 0)
    readonly property int tileDownloaded: (queueStats.downloaded || 0)
                                          + (playingCounts && appWindow.player.info.downloaded === true ? 1 : 0)
    readonly property real tileSeconds: {
        var rest = playingCounts ? Math.max(0, appWindow.player.duration - livePosition) : 0
        var rate = appWindow.player.rate > 0 ? appWindow.player.rate : 1
        return ((queueStats.seconds || 0) + rest) / rate
    }

    EpisodeListModel {
        id: queue
        filter: EpisodeListModel.Queue
        excludeEpisodeId: page.currentId
    }

    // Two or more episodes in the playlist for the first time: sorting and
    // removing (52)
    // (254, usability audit #7) "sort" alone didn't say *what* it sorts -
    // forum feedback echoed this. Now says it directly: the play order.
    // New key ("playlist2"): existing users already dismissed the old,
    // vaguer wording, so they get the clearer one once too.
    function _playlistHint() {
        if (queue.count >= 2) {
            appWindow.showHint("playlist2", qsTr("Drag an episode by its handle to set the play order, below the list to remove it"), page)
        }
    }
    Connections {
        target: queue
        onCountChanged: page._playlistHint()
    }
    onStatusChanged: _playlistHint()

    RemorsePopup {
        id: remorse
    }

    // --- Dragging (as the favourites in Sailwave) ---
    property int dragIndex: -1
    property real dragViewY: 0   // finger position in view coordinates

    function startDrag(index, viewY) {
        list._keepTop = false
        list.interactive = false
        dragIndex = index
        dragViewY = viewY
    }

    // Finger over the area below the last episode: release removes
    property bool overRemoveZone: false

    function moveDraggedTo(viewY) {
        dragViewY = viewY
        if (dragIndex < 0) {
            return
        }
        var zone = list.footerItem ? list.footerItem.removeZone : null
        if (zone) {
            var y = zone.mapFromItem(list, 0, viewY).y
            overRemoveZone = y >= 0 && y <= zone.height
            if (overRemoveZone) {
                return
            }
        }
        var contentY = viewY + list.contentY
        var target = list.indexAt(Theme.horizontalPageMargin, contentY)
        if (target < 0) {
            // Above the first row (header) or below the last one
            target = contentY < list.originY + list.headerItem.height ? 0 : queue.count - 1
        }
        target = Math.max(0, Math.min(queue.count - 1, target))
        if (target !== dragIndex) {
            queue.move(dragIndex, target)
            dragIndex = target
        }
    }

    function finishDrag() {
        list.interactive = true
        if (dragIndex < 0) {
            return
        }
        var removeId = overRemoveZone ? queue.episodeIdAt(dragIndex) : -1
        dragIndex = -1
        overRemoveZone = false
        queue.commitQueueOrder()
        if (removeId > 0) {
            remorse.execute(qsTr("Removing from playlist"), function() { core.queueRemove(removeId) })
        }
    }

    // Scrolls while the finger is held near the top or bottom edge
    Timer {
        id: autoScroll
        interval: 30
        repeat: true
        running: page.dragIndex >= 0
                 && (page.dragViewY < Theme.itemSizeMedium
                     || page.dragViewY > list.height - Theme.itemSizeMedium)
        onTriggered: {
            var step = page.dragViewY < Theme.itemSizeMedium ? -Theme.paddingLarge : Theme.paddingLarge
            var maxY = list.originY + list.contentHeight - list.height
            list.contentY = Math.max(list.originY, Math.min(maxY, list.contentY + step))
            page.moveDraggedTo(page.dragViewY)
        }
    }

    // (264, Thomas 6 Nov: the menu under "Recently heard" darkened the row
    // but nothing showed) The rows of the footer are no delegates of the
    // list, so Silica does not scroll their menu into view - it opened below
    // the visible area (behind the player bar). Scrolled by hand once the
    // menu has opened.
    function revealRow(row) {
        revealTimer.row = row
        revealTimer.restart()
    }
    Timer {
        id: revealTimer
        interval: 300
        property Item row: null
        onTriggered: {
            if (!row || !row.menuOpen) {
                return
            }
            var bottom = row.mapToItem(list.contentItem, 0, 0).y + row.height + Theme.paddingLarge
            var viewBottom = list.contentY + list.height
            if (bottom > viewBottom) {
                var maxY = list.originY + list.contentHeight - list.height
                list.contentY = Math.min(list.contentY + (bottom - viewBottom), Math.max(list.contentY, maxY))
            }
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: queue
        // Opens at the top (65: it used to start at the first playlist
        // row, "Now playing" above it out of view): no current item to
        // follow, and while nobody has scrolled yet the top is kept when
        // the header grows (Now playing, covers loading)
        currentIndex: -1
        property bool _keepTop: true
        onMovementStarted: _keepTop = false
        onContentHeightChanged: if (_keepTop) positionViewAtBeginning()
        Component.onCompleted: positionViewAtBeginning()

        header: Column {
            width: list.width
            // (169) After scrolling, a growing header (chapters of "Now
            // playing" opened) grew upwards out of view, because the list
            // keeps its first row in place: while the header is in view,
            // its top stays where it is instead
            property real _lastHeight: 0
            onHeightChanged: {
                var delta = height - _lastHeight
                _lastHeight = height
                if (list._keepTop) {
                    list.positionViewAtBeginning()
                } else if (delta !== 0 && list.contentY < y + height) {
                    list.contentY = Math.max(list.originY, list.contentY - delta)
                }
            }

            PageHeader {
                title: qsTr("Playlist")
                // (190) The number is in the tiles now
            }
            // (255) Neighbours of the ring, below the header
            RingNeighbours { kind: "playlist"; ringMember: page.ringMember }

            // (190) Is it enough for the trip? Only with a playlist
            StatTiles {
                visible: page.tileCount > 0
                tiles: [
                    { value: TimeFormat.length(Math.round(page.tileSeconds), qsTr("%1 h %2 min"), qsTr("%1 min")),
                      label: qsTr("left to listen") },
                    // (231) "1 episodes": singular (no English .ts, so no %n plural form)
                    { value: "" + page.tileCount, label: page.tileCount === 1 ? qsTr("episode") : qsTr("episodes") },
                    // (253, usability audit #10) Tap: the Downloads page
                    { value: "" + page.tileDownloaded, label: qsTr("downloaded"), tappable: true }
                ]
                onTileClicked: if (index === 2) pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
            }
            Item { width: 1; height: Theme.paddingMedium; visible: page.tileCount > 0 }

            // --- Now playing ---
            SectionHeader {
                visible: nowPlaying.count > 0
                text: qsTr("Now playing")
            }

            Repeater {
                model: nowPlaying
                delegate: EpisodeDelegate {
                    width: list.width
                    inPlaylist: model.queued
                    playlistContext: model.queued
                    // A menu here would grow the list header (see above)
                    menuEnabled: false
                }
            }

            // --- Playlist ---
            SectionHeader {
                visible: nowPlaying.count > 0 || continuing.count > 0 || queue.count > 0
                text: qsTr("Playlist")
            }

            Label {
                visible: queue.count === 0 && (nowPlaying.count > 0 || continuing.count > 0)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingLarge
                wrapMode: Text.Wrap
                text: qsTr("Nothing in the playlist - add episodes with \"Play next\" or \"Add to end of playlist\" in their menu")
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }
        }

        // Also downloads and bookmarks (73, before in the start page pulley):
        // everything waiting to be listened to. Countdown entry on top.
        PullDownMenu {
            visible: queue.count > 0 || core.podcastCount > 0 || core.bookmarkCount > 0
            // (259) Same order as the other pulleys: Bookmarks, Downloads
            // (shared item; the count is read when the menu opens, 218),
            // Search; "Clear playlist" far at the top, "Play playlist" last
            id: playlistMenu
            onActiveChanged: if (active) playlistDownloads.refresh()
            MenuItem {
                visible: queue.count > 0
                text: qsTr("Clear playlist")
                onClicked: remorse.execute(qsTr("Clearing playlist"), function() { core.queueClear() })
            }
            MenuItem {
                visible: core.bookmarkCount > 0
                text: qsTr("Bookmarks (%1)").arg(core.bookmarkCount)
                onClicked: pageStack.push(Qt.resolvedUrl("BookmarksPage.qml"))
            }
            DownloadsMenuItem {
                id: playlistDownloads
            }
            // Search on the device (86)
            MenuItem {
                visible: core.podcastCount > 0
                text: qsTr("Search my podcasts")
                onClicked: pageStack.push(Qt.resolvedUrl("LocalSearchPage.qml"))
            }
            MenuItem {
                visible: queue.count > 0
                text: qsTr("Play playlist")
                onClicked: appWindow.player.playOrToggle(queue.episodeIdAt(0), true)
            }
        }

        delegate: EpisodeDelegate {
            inPlaylist: true
            playlistContext: true
            dragHandle: true
            // (259) An opening menu ends "keep the top" (else the list is
            // pulled back up and the menu runs behind the player bar)
            onMenuOpenChanged: if (menuOpen) list._keepTop = false
            dragged: page.dragIndex === index
            onDragStarted: page.startDrag(index, viewY)
            onDragMoved: page.moveDraggedTo(viewY)
            onDragEnded: page.finishDrag()
        }

        footer: Column {
            width: list.width
            // For the drag code: where the removal area is
            property alias removeZone: removeZone

            // Space below the last episode: its drag handle must not sit at
            // the bottom screen edge, where a swipe starts the system
            // gesture. While an episode is dragged, the area to drop it for
            // removal.
            Item {
                id: removeZone
                width: parent.width
                // (191) Half as high at rest (the gap to Continue listening
                // looked empty), full while an episode is dragged
                height: queue.count > 0 ? (page.dragIndex >= 0 ? Theme.itemSizeLarge : Theme.itemSizeLarge / 2) : 0

                Rectangle {
                    anchors.fill: parent
                    anchors.margins: Theme.paddingMedium
                    radius: Theme.paddingMedium
                    visible: page.dragIndex >= 0
                    color: page.overRemoveZone ? Theme.rgba(Theme.errorColor, 0.35)
                                               : Theme.rgba(Theme.highlightBackgroundColor, 0.15)
                    border.width: Math.max(1, Theme.paddingSmall / 3)
                    border.color: page.overRemoveZone ? Theme.errorColor : Theme.rgba(Theme.highlightColor, 0.5)

                    Row {
                        anchors.centerIn: parent
                        spacing: Theme.paddingMedium
                        Image {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.iconSizeMedium
                            height: width
                            sourceSize.width: width
                            sourceSize.height: height
                            source: "image://theme/icon-m-delete?" + (page.overRemoveZone ? Theme.primaryColor : Theme.highlightColor)
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Release here to remove")
                            font.pixelSize: Theme.fontSizeSmall
                            color: page.overRemoveZone ? Theme.primaryColor : Theme.highlightColor
                        }
                    }
                }
            }

            // --- Continue listening (collapsible, below the playlist since 24) ---
            BackgroundItem {
                id: continueHeader
                visible: continuing.count > 0
                width: parent.width
                height: Theme.itemSizeExtraSmall
                onClicked: page.toggleContinue()

                Image {
                    id: arrow
                    anchors {
                        right: parent.right
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    width: Theme.iconSizeSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    source: "image://theme/icon-m-down?" + Theme.highlightColor
                    rotation: page.continueExpanded ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: 150 } }
                }
                Label {
                    anchors {
                        right: arrow.left
                        rightMargin: Theme.paddingMedium
                        verticalCenter: parent.verticalCenter
                    }
                    text: qsTr("Continue listening (%1)").arg(page.continueTotal)
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            Repeater {
                model: continuing
                delegate: EpisodeDelegate {
                    // The extra row only tells that there are more
                    id: continueRow
                    visible: page.continueExpanded && index < page.collapsedRows
                    width: list.width
                    onMenuOpenChanged: if (menuOpen) { list._keepTop = false; page.revealRow(continueRow) }
                }
            }

            BackgroundItem {
                visible: page.continueExpanded && continuing.count > page.collapsedRows
                width: parent.width
                height: Theme.itemSizeExtraSmall
                onClicked: pageStack.push(Qt.resolvedUrl("ContinueListeningPage.qml"))
                Label {
                    anchors {
                        right: parent.right
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    text: qsTr("Show all")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }

            // --- Recently heard (188, collapsible, below Continue listening) ---
            BackgroundItem {
                visible: recent.count > 0
                width: parent.width
                height: Theme.itemSizeExtraSmall
                onClicked: page.toggleRecent()

                Image {
                    id: recentArrow
                    anchors {
                        right: parent.right
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    width: Theme.iconSizeSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    source: "image://theme/icon-m-down?" + Theme.highlightColor
                    rotation: page.recentExpanded ? 180 : 0
                    Behavior on rotation { NumberAnimation { duration: 150 } }
                }
                Label {
                    anchors {
                        right: recentArrow.left
                        rightMargin: Theme.paddingMedium
                        verticalCenter: parent.verticalCenter
                    }
                    text: qsTr("Recently heard (%1)").arg(recent.count)
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
            }

            Repeater {
                model: recent
                delegate: EpisodeDelegate {
                    id: recentRow
                    visible: page.recentExpanded
                    width: list.width
                    onMenuOpenChanged: if (menuOpen) { list._keepTop = false; page.revealRow(recentRow) }
                }
            }

            // Room below the last row
            Item { width: 1; height: Theme.paddingLarge }
        }

        ViewPlaceholder {
            enabled: queue.count === 0 && nowPlaying.count === 0 && continuing.count === 0
            text: qsTr("The playlist is empty")
            hintText: qsTr("Add episodes with \"Play next\" or \"Add to end of playlist\" in their menu")
        }

        VerticalScrollDecorator {}
    }
}

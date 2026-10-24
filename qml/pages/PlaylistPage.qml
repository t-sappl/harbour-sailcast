// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

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

    EpisodeListModel {
        id: queue
        filter: EpisodeListModel.Queue
        excludeEpisodeId: page.currentId
    }

    // Two or more episodes in the playlist for the first time: sorting and
    // removing (52)
    function _playlistHint() {
        if (queue.count >= 2) {
            appWindow.showHint("playlist", qsTr("Drag an episode by its handle to sort, below the list to remove it"), page)
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
            onHeightChanged: if (list._keepTop) list.positionViewAtBeginning()

            PageHeader {
                title: qsTr("Playlist")
                description: queue.count > 0 ? qsTr("%n episodes", "", queue.count) : ""
            }

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
            MenuItem {
                visible: queue.count > 0
                text: qsTr("Clear playlist")
                onClicked: remorse.execute(qsTr("Clearing playlist"), function() { core.queueClear() })
            }
            MenuItem {
                visible: core.podcastCount > 0
                text: core.downloadCount > 0 ? qsTr("Downloads (%1)").arg(core.downloadCount) : qsTr("Downloads")
                onClicked: pageStack.push(Qt.resolvedUrl("DownloadsPage.qml"))
            }
            MenuItem {
                visible: core.bookmarkCount > 0
                text: qsTr("Bookmarks (%1)").arg(core.bookmarkCount)
                onClicked: pageStack.push(Qt.resolvedUrl("BookmarksPage.qml"))
            }
            // Search on the device (86)
            MenuItem {
                visible: core.podcastCount > 0
                text: qsTr("Search")
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
                height: queue.count > 0 ? Theme.itemSizeLarge : 0

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
                    visible: page.continueExpanded && index < page.collapsedRows
                    width: list.width
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

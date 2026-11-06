// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// (256, "one player view", usability check 254) The tabs of the player -
// Chapters · Bookmarks · Notes · Up next, each only with content - taken out
// of the expanded player bar (178/167) so the landscape panel shows the same
// (it had the chapters only). The owner passes the bookmarks and the next
// playlist episodes (the bar keeps them up to date) and the number of rows.
Column {
    id: tabs

    property int rows: 5
    // The area is open (the current chapter is scrolled into view)
    property bool expanded: false
    property var bookmarkList: []
    property var upNextList: []
    signal openPlaylist()
    // (268) Downward drag from the top of a tab: the owner may close
    signal swipedDown()

    readonly property var player: appWindow.player

    // Show notes of the episode playing
    property string notes: ""
    function refreshNotes() {
        notes = player.hasEpisode ? (core.episode(player.episodeId).description || "") : ""
    }
    Component.onCompleted: refreshNotes()

    // A choice holds until the next episode
    property string chosenTab: ""
    Connections {
        target: appWindow.player
        onEpisodeIdChanged: {
            tabs.chosenTab = ""
            tabs.refreshNotes()
        }
    }
    readonly property var tabList: {
        var list = []
        if (player.chapters.length > 0) list.push({ key: "chapters", text: qsTr("Chapters") })
        if (notes.length > 0) list.push({ key: "notes", text: qsTr("Shownotes") })
        if (bookmarkList.length > 0) list.push({ key: "bookmarks", text: qsTr("Bookmarks") })
        list.push({ key: "next", text: qsTr("Up next") })
        return list
    }
    readonly property string currentTab: {
        for (var i = 0; i < tabList.length; i++) {
            if (tabList[i].key === chosenTab) return chosenTab
        }
        return tabList[0].key
    }

    width: parent ? parent.width : 0

    // (178) Chapters, bookmarks and the next playlist episodes as
    // tabs over one area of fixed height (before: one below the
    // other, "Up next" only without chapters and bookmarks). Tabs
    // without content are left out; the first one with content is
    // chosen, a choice holds until the next episode.
    Row {
        id: barTabs
        width: parent.width
        Repeater {
            model: tabs.tabList
            // By id (145)
            BackgroundItem {
                id: barTabItem
                width: barTabs.width / Math.max(1, tabs.tabList.length)
                height: Theme.itemSizeExtraSmall
                readonly property bool current: tabs.currentTab === modelData.key
                onClicked: tabs.chosenTab = modelData.key
                Label {
                    anchors.centerIn: parent
                    width: parent.width - 2 * Theme.paddingSmall
                    horizontalAlignment: Text.AlignHCenter
                    truncationMode: TruncationMode.Fade
                    text: modelData.text
                    font.pixelSize: Theme.fontSizeSmall
                    color: barTabItem.current || barTabItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
                }
                Rectangle {
                    anchors.bottom: parent.bottom
                    anchors.horizontalCenter: parent.horizontalCenter
                    width: parent.width - 2 * Theme.paddingLarge
                    height: Theme.paddingSmall / 2
                    color: Theme.highlightColor
                    visible: barTabItem.current
                }
            }
        }
    }

    Item {
        id: tabArea
        width: parent.width
        height: tabs.rows * Theme.itemSizeSmall

        ChapterList {
            width: parent.width
            visible: tabs.currentTab === "chapters"
            visibleRows: tabs.rows
            expanded: tabs.expanded && visible
            onSwipedDown: tabs.swipedDown()
        }

        // (167) Bookmarks of the episode; tap jumps there
        SilicaListView {
            id: bookmarkView
            anchors.fill: parent
            visible: tabs.currentTab === "bookmarks"
            clip: true
            // (268) Always interactive: a short list must still take the
            // drag (otherwise the ListItem keeps it and nothing happens).
            // A downward drag that starts at the top pulls the content
            // below its start; past two paddings that closes the bar.
            // (267 tested verticalVelocity > 0 - wrong sign: dragging
            // down makes contentY smaller, the velocity negative.)
            interactive: true
            boundsBehavior: Flickable.DragOverBounds
            property bool _fromTop: false
            onDraggingVerticallyChanged: _fromTop = draggingVertically && atYBeginning
            onContentYChanged: {
                if (_fromTop && draggingVertically && contentY < originY - 2 * Theme.paddingLarge) {
                    _fromTop = false
                    tabs.swipedDown()
                }
            }
            model: tabs.bookmarkList
            // (182) A ListItem with the menu of the episode page (183:
            // without "Open episode" - the cover opens it)
            delegate: ListItem {
                id: bookmarkRow
                width: bookmarkView.width
                // (173) The note below the name, as on the bookmarks page
                readonly property bool hasNote: (modelData.note || "").length > 0
                contentHeight: hasNote ? Math.max(Theme.itemSizeMedium, bookmarkText.height + 2 * Theme.paddingSmall)
                                       : Theme.itemSizeSmall
                onClicked: tabs.player.seek(modelData.position)
                menu: Component {
                    ContextMenu {
                        MenuItem {
                            text: qsTr("Rename / note")
                            onClicked: appWindow.editBookmark(modelData)
                        }
                        MenuItem {
                            text: qsTr("Delete bookmark")
                            onClicked: appWindow.remorseDeleteBookmark(modelData.bookmarkId)
                        }
                    }
                }
                Label {
                    id: bookmarkTime
                    x: Theme.horizontalPageMargin
                    // (177) On the line of the name, not centred on name
                    // and note (179: by y - anchors only reach
                    // siblings, the name is inside the Column)
                    y: bookmarkText.y + bookmarkName.baselineOffset - baselineOffset
                    width: Theme.itemSizeMedium
                    text: TimeFormat.clock(modelData.position)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: bookmarkRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
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
                        id: bookmarkName
                        width: parent.width
                        // (170) Numbered: not "Bookmark" under "Bookmarks"
                        text: (modelData.title || "").length > 0 ? modelData.title
                                                                 : qsTr("Bookmark %1").arg(index + 1)
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: bookmarkRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        visible: bookmarkRow.hasNote
                        width: parent.width
                        text: modelData.note || ""
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                    }
                }
            }
            VerticalScrollDecorator {}
        }

        // (256) The show notes of the episode playing (they were on the
        // episode page only); time stamps jump there
        SilicaFlickable {
            id: notesView
            anchors.fill: parent
            visible: tabs.currentTab === "notes"
            clip: true
            contentHeight: notesLabel.height + 2 * Theme.paddingMedium
            // (268) Always interactive: a short list must still take the
            // drag (otherwise the ListItem keeps it and nothing happens).
            // A downward drag that starts at the top pulls the content
            // below its start; past two paddings that closes the bar.
            // (267 tested verticalVelocity > 0 - wrong sign: dragging
            // down makes contentY smaller, the velocity negative.)
            interactive: true
            boundsBehavior: Flickable.DragOverBounds
            property bool _fromTop: false
            onDraggingVerticallyChanged: _fromTop = draggingVertically && atYBeginning
            onContentYChanged: {
                if (_fromTop && draggingVertically && contentY < originY - 2 * Theme.paddingLarge) {
                    _fromTop = false
                    tabs.swipedDown()
                }
            }
            Label {
                id: notesLabel
                x: Theme.horizontalPageMargin
                y: Theme.paddingMedium
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: TimeFormat.linkTimestamps(tabs.notes, tabs.player.duration / 1000)
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                linkColor: Theme.highlightColor
                onLinkActivated: {
                    if (link.indexOf("sailcast-seek:") === 0) {
                        tabs.player.seek(parseInt(link.substring(14)))
                        if (!tabs.player.playing) {
                            tabs.player.resume()
                        }
                        return
                    }
                    Qt.openUrlExternally(link)
                }
            }
            VerticalScrollDecorator {}
        }


        // (167) The next playlist episodes in the order they will
        // play; tap plays one
        SilicaListView {
            id: upNextView
            anchors.fill: parent
            visible: tabs.currentTab === "next"
            clip: true
            // (268) Always interactive: a short list must still take the
            // drag (otherwise the ListItem keeps it and nothing happens).
            // A downward drag that starts at the top pulls the content
            // below its start; past two paddings that closes the bar.
            // (267 tested verticalVelocity > 0 - wrong sign: dragging
            // down makes contentY smaller, the velocity negative.)
            interactive: true
            boundsBehavior: Flickable.DragOverBounds
            property bool _fromTop: false
            onDraggingVerticallyChanged: _fromTop = draggingVertically && atYBeginning
            onContentYChanged: {
                if (_fromTop && draggingVertically && contentY < originY - 2 * Theme.paddingLarge) {
                    _fromTop = false
                    tabs.swipedDown()
                }
            }
            model: tabs.upNextList
            footer: BackgroundItem {
                width: upNextView.width
                height: Theme.itemSizeExtraSmall
                visible: tabs.upNextList.length > 0
                onClicked: tabs.openPlaylist()
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Full playlist") + " \u203a"
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }
            // (183) Menu: play next, remove; reordering on the
            // playlist page (footer)
            delegate: ListItem {
                id: nextRow
                width: upNextView.width
                contentHeight: Theme.itemSizeSmall
                onClicked: tabs.player.playOrToggle(modelData.episodeId, modelData.followUp !== true)
                // (207) The trailer's follow-up is not in the playlist
                menu: modelData.followUp === true ? null : queueMenu
                Component {
                    id: queueMenu
                    ContextMenu {
                        MenuItem {
                            visible: index > (tabs.upNextList.length > 0
                                              && tabs.upNextList[0].followUp === true ? 1 : 0)
                            text: qsTr("Play next")
                            onClicked: core.queueAdd(modelData.episodeId, true)
                        }
                        MenuItem {
                            text: qsTr("Remove from playlist")
                            onClicked: core.queueRemove(modelData.episodeId)
                        }
                    }
                }
                EpisodeArt {
                    id: nextArt
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
                        left: nextArt.right
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
                        color: nextRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        text: modelData.followUp === true
                              ? qsTr("After the trailer · %1").arg(modelData.podcastTitle)
                              : modelData.podcastTitle
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeTiny
                        color: nextRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    }
                }
            }
            VerticalScrollDecorator {}
        }
        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            height: Theme.itemSizeSmall
            visible: tabs.currentTab === "next" && tabs.upNextList.length === 0
            verticalAlignment: Text.AlignVCenter
            text: qsTr("The playlist is empty")
            color: Theme.secondaryHighlightColor
            font.pixelSize: Theme.fontSizeSmall
            truncationMode: TruncationMode.Fade
        }
    }
}

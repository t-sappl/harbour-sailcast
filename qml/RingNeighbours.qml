// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (255, usability check 254: the swiping between the main pages was found
// by nobody) A small, dimmed line below the page header of each ring page:
// the previous page at the left edge, the next one at the right edge, the
// middle empty. A tap goes there (as the swipe does). It follows the
// setting "Discover as a swipe page". With a large font the names give way
// to the bare arrows. Thomas 6 Nov, variant 2 "neighbours only".
Item {
    id: line

    // "home", "podcasts", "playlist" or "discover" - the page it sits on
    property string kind: ""
    // Only on the pages of the ring (Discover is also opened from pulleys)
    property bool ringMember: true

    readonly property bool discoverOn: appWindow.discoverRingOn
    // (301) Downloads as the last ring page: between Playlist and New episodes
    readonly property bool downloadsOn: appWindow.downloadsRingOn
    // (309) Downloads right after New episodes
    readonly property string prevKind: kind === "home" ? "playlist"
                                     : kind === "downloads" ? "home"
                                     : kind === "discover" ? (downloadsOn ? "downloads" : "home")
                                     : kind === "podcasts" ? (discoverOn ? "discover" : (downloadsOn ? "downloads" : "home"))
                                     : kind === "playlist" ? "podcasts" : ""
    readonly property string nextKind: kind === "home" ? (downloadsOn ? "downloads" : (discoverOn ? "discover" : "podcasts"))
                                     : kind === "downloads" ? (discoverOn ? "discover" : "podcasts")
                                     : kind === "discover" ? "podcasts"
                                     : kind === "podcasts" ? "playlist"
                                     : kind === "playlist" ? "home" : ""

    function pageName(key) {
        if (key === "home") return qsTr("New episodes")
        if (key === "podcasts") return qsTr("Podcasts")
        if (key === "playlist") return qsTr("Playlist")
        if (key === "discover") return qsTr("Discover")
        if (key === "downloads") return qsTr("Downloads")
        return ""
    }

    readonly property real half: (width - 2 * Theme.horizontalPageMargin) / 2
    // (256, Thomas on the device: the right name under the right-aligned
    // title read as a subtitle) The arrows large and in the highlight
    // colour, so the line reads as navigation; the names stay small
    readonly property real arrowSize: Theme.fontSizeLarge

    width: parent ? parent.width : Screen.width
    height: ringMember && prevKind !== "" ? Theme.itemSizeExtraSmall : 0
    visible: height > 0

    // Measuring: do the names fit into their halves (beside the arrow)?
    Label {
        id: prevMeasure
        visible: false
        font.pixelSize: Theme.fontSizeExtraSmall
        text: line.pageName(line.prevKind)
    }
    Label {
        id: nextMeasure
        visible: false
        font.pixelSize: Theme.fontSizeExtraSmall
        text: line.pageName(line.nextKind)
    }

    BackgroundItem {
        id: prevItem
        anchors.left: parent.left
        width: parent.width / 2
        height: parent.height
        onClicked: if (pageStack.depth > 1 && !pageStack.busy) pageStack.navigateBack()
        Row {
            anchors {
                left: parent.left
                leftMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.paddingSmall
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "\u2039"
                font.pixelSize: line.arrowSize
                color: Theme.highlightColor
            }
            Label {
                // (258, Gemini: bare arrows look like "back") the name is
                // shortened before it goes; only a sliver of room drops it
                visible: line.half - line.arrowSize > Theme.paddingLarge * 2
                width: Math.min(implicitWidth, line.half - line.arrowSize - Theme.paddingSmall)
                truncationMode: TruncationMode.Fade
                anchors.verticalCenter: parent.verticalCenter
                text: line.pageName(line.prevKind)
                font.pixelSize: Theme.fontSizeExtraSmall
                color: prevItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
            }
        }
    }
    BackgroundItem {
        id: nextItem
        anchors.right: parent.right
        width: parent.width / 2
        height: parent.height
        readonly property var curPage: pageStack.currentPage
        onClicked: if (curPage && curPage.canNavigateForward && !pageStack.busy) pageStack.navigateForward()
        Row {
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.paddingSmall
            Label {
                visible: line.half - line.arrowSize > Theme.paddingLarge * 2
                width: Math.min(implicitWidth, line.half - line.arrowSize - Theme.paddingSmall)
                truncationMode: TruncationMode.Fade
                anchors.verticalCenter: parent.verticalCenter
                text: line.pageName(line.nextKind)
                font.pixelSize: Theme.fontSizeExtraSmall
                color: nextItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: "\u203a"
                font.pixelSize: line.arrowSize
                color: Theme.highlightColor
            }
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// One cover of the podcast grid (45, taken from PodcastsPage): unplayed
// count as a badge, ⏸ for "manual update" (podcast or its folder), a red
// dot for a feed problem. Tap opens the podcast page; press and hold: the
// same context menu as the list rows (65).
ListItem {
    id: cell

    property int podcastId: 0
    property string title: ""
    property string coverUrl: ""
    property int unplayed: 0
    property bool manualUpdate: false
    // New episodes since the podcast page was last opened (50)
    property bool fresh: false
    property string lastError: ""

    onClicked: pageStack.push(Qt.resolvedUrl("pages/EpisodesPage.qml"), { podcastId: cell.podcastId })

    menu: Component {
        PodcastMenu {
            podcastId: cell.podcastId
            title: cell.title
            unplayed: cell.unplayed
        }
    }

    CoverImage {
        anchors.fill: parent
        anchors.margins: Theme.paddingSmall
        source: cell.coverUrl
        fallbackText: cell.title
        fadeIn: true
        opacity: cell.highlighted ? 0.7 : 1.0
    }

    // Unplayed episodes (the badge stays the same since 59; new episodes
    // get the dot top left instead)
    Rectangle {
        id: badge
        visible: cell.unplayed > 0
        anchors {
            top: parent.top
            right: parent.right
            margins: Theme.paddingSmall
        }
        height: badgeLabel.height + Theme.paddingSmall
        width: Math.max(height, badgeLabel.width + Theme.paddingMedium)
        radius: height / 2
        color: Theme.highlightBackgroundColor
        Label {
            id: badgeLabel
            anchors.centerIn: parent
            text: cell.unplayed > 999 ? "999+" : cell.unplayed
            font.pixelSize: Theme.fontSizeExtraSmall
            font.bold: true
            color: Theme.primaryColor
        }
    }

    // New episodes since the podcast was last opened (59)
    NewDot {
        visible: cell.fresh
        // 69: larger, on a dark spot; the glow reaches over the cover's
        // corner, the ball sits inside
        size: Theme.itemSizeExtraSmall * 1.5
        backdrop: true
        x: -Theme.paddingSmall
        y: -Theme.paddingSmall
    }

    // Left out of "update all" (44/45): small pause sign bottom right
    Rectangle {
        visible: cell.manualUpdate
        anchors {
            bottom: parent.bottom
            right: parent.right
            margins: Theme.paddingSmall
        }
        width: Theme.iconSizeSmall
        height: width
        radius: width / 2
        color: Theme.rgba("black", 0.55)
        Image {
            anchors.centerIn: parent
            width: Theme.iconSizeExtraSmall
            height: width
            sourceSize.width: width
            sourceSize.height: height
            source: "image://theme/icon-m-pause?" + Theme.primaryColor
        }
    }

    // Feed problem
    Rectangle {
        visible: cell.lastError.length > 0
        anchors {
            bottom: parent.bottom
            left: parent.left
            margins: Theme.paddingSmall
        }
        width: Theme.iconSizeExtraSmall
        height: width
        radius: width / 2
        color: Theme.errorColor
    }
}

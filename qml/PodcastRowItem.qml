// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// One row of the podcast list (45, taken from PodcastsPage): cover, title,
// counts, listening time, "manual update", feed problem; context menu.
// The menu only calls appWindow/core (no callbacks created in the menu,
// see DESIGN "Countdowns from context menus").
ListItem {
    id: row

    property int podcastId: 0
    property string title: ""
    property string coverUrl: ""
    property int unplayed: 0
    property int episodeCount: 0
    property real listenedSeconds: 0
    property int downloadedCount: 0
    property bool archived: false
    property bool manualUpdate: false
    property bool fresh: false      // new episodes since last opened (50)
    property string lastError: ""

    contentHeight: Math.max(Theme.itemSizeLarge, textColumn.height + 2 * Theme.paddingMedium)
    onClicked: pageStack.push(Qt.resolvedUrl("pages/EpisodesPage.qml"), { podcastId: row.podcastId })

    menu: Component {
        PodcastMenu {
            podcastId: row.podcastId
            title: row.title
            unplayed: row.unplayed
        }
    }

    // New episodes since the podcast was last opened (59): in the left
    // margin, as the unread dot in Sailfeed
    NewDot {
        visible: row.fresh
        size: Theme.itemSizeExtraSmall
        // 69: closer to the cover (it stuck to the screen edge)
        x: cover.x - width + Math.round(width * 0.3)
        z: 1
        anchors.verticalCenter: parent.verticalCenter
    }

    CoverImage {
        id: cover
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: Theme.itemSizeMedium
        height: width
        source: row.coverUrl
        fallbackText: row.title
    }

    Column {
        id: textColumn
        anchors {
            left: cover.right
            leftMargin: Theme.paddingMedium
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        Label {
            width: parent.width
            text: row.title
            truncationMode: TruncationMode.Fade
            color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            text: {
                var parts = [qsTr("%n unplayed", "", row.unplayed), qsTr("%n episodes", "", row.episodeCount)]
                if (row.listenedSeconds >= 60) {
                    parts.push(qsTr("%1 listened").arg(TimeFormat.length(row.listenedSeconds, qsTr("%1 h %2 min"), qsTr("%1 min"))))
                }
                if (row.downloadedCount > 0) {
                    parts.push(qsTr("%n downloaded", "", row.downloadedCount))
                }
                if (row.archived) {
                    parts.push(qsTr("archived"))
                } else if (row.manualUpdate) {
                    parts.push("⏸ " + qsTr("manual update"))
                }
                return parts.join(" · ")
            }
            // Two lines at most (71): the statistics were cut off
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
        Label {
            visible: row.lastError.length > 0
            width: parent.width
            text: row.lastError
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.errorColor
        }
    }
}

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
    property bool video: false      // (247) video podcast: camera after the counts
    property string lastError: ""
    // (286) Auto-Add is on for this podcast: the symbol of the playlist handle
    property bool autoAdd: core.setting("podcast." + podcastId + ".autoAdd", "") !== ""

    Connections {
        target: core
        onSettingsChanged: row.autoAdd = core.setting("podcast." + row.podcastId + ".autoAdd", "") !== ""
    }

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

        // (293) Number of unplayed episodes as in the grid (was only in the text)
        Rectangle {
            visible: row.unplayed > 0
            anchors {
                top: parent.top
                right: parent.right
                margins: Theme.paddingSmall / 2
            }
            height: unplayedLabel.height + Theme.paddingSmall
            width: Math.max(height, unplayedLabel.width + Theme.paddingMedium)
            radius: height / 2
            color: Theme.highlightBackgroundColor
            Label {
                id: unplayedLabel
                anchors.centerIn: parent
                text: row.unplayed > 999 ? "999+" : row.unplayed
                font.pixelSize: Theme.fontSizeExtraSmall
                font.bold: true
                color: Theme.primaryColor
            }
        }

        // (248) Video podcast: camera bottom left on a dark spot, as in the
        // grid (the symbol after the counts was hardly visible)
        Rectangle {
            visible: row.video
            anchors {
                left: parent.left
                bottom: parent.bottom
                margins: Theme.paddingSmall / 2
            }
            width: Math.round(parent.width * 0.38)
            height: width
            radius: width / 2
            color: Qt.rgba(0, 0, 0, 0.55)
            VideoIcon {
                anchors.centerIn: parent
                width: Math.round(parent.width * 0.62)
                color: "white"
            }
        }
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
        Item {
            width: parent.width
            height: titleLabel.height
            Label {
                id: titleLabel
                width: parent.width - (autoMark.visible ? autoMark.width + Theme.paddingSmall : 0)
                text: row.title
                truncationMode: TruncationMode.Fade
                // (293) Nothing unplayed: weaker
                opacity: row.unplayed > 0 || row.highlighted ? 1.0 : 0.6
                color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            // (286) Auto-Add on: the playlist symbol with the circle arrow,
            // quiet (the colour of the second line), after the title
            HighlightImage {
                id: autoMark
                visible: row.autoAdd
                source: Qt.resolvedUrl("images/icon-autoadd.svg")
                color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                width: Math.max(Math.round(Theme.iconSizeSmall * 0.8 * 0.95), 36)
                height: Math.round(width * 34 / 40)
                sourceSize.width: width
                sourceSize.height: height
                fillMode: Image.PreserveAspectFit
                anchors.verticalCenter: titleLabel.verticalCenter
                x: Math.min(titleLabel.contentWidth + Theme.paddingSmall, parent.width - width)
            }
        }
        Label {
            width: parent.width
            text: {
                var parts = [qsTr("%n unplayed", "", row.unplayed), qsTr("%n episodes", "", row.episodeCount)]
                if (row.listenedSeconds >= 60) {
                    parts.push(qsTr("%1 listened").arg(TimeFormat.length(row.listenedSeconds, qsTr("%1 h %2 min"), qsTr("%1 min"))))
                }
                // (194) A line breaks only between the parts, not inside
                // one ("166 h 42 min listened" stays together)
                for (var i = 0; i < parts.length; i++) {
                    parts[i] = parts[i].replace(/ /g, "\u00a0")
                }
                // (191) An arrow instead of "downloaded": the line fits more
                // often in one row; (255, usability check 254: "↓ 4" was
                // not understood) the word again, "4 downloaded"; (194)
                // kept with the part before it, so it never stands alone on
                // the second line
                if (row.downloadedCount > 0) {
                    parts[parts.length - 1] += "\u00a0·\u00a0" + qsTr("%1 downloaded").arg(row.downloadedCount).replace(/ /g, "\u00a0")
                }
                if (row.archived) {
                    parts.push(qsTr("ended").replace(/ /g, "\u00a0"))
                } else if (row.manualUpdate) {
                    parts.push(("⏸ " + qsTr("manual update")).replace(/ /g, "\u00a0"))
                }
                // The dot stays at the end of a line, not at the start
                return parts.join("\u00a0· ")
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

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Chapters of the current episode for the expanded player bar (like the
// track preview there in Sailwave): visibleRows rows visible at once, more
// by scrolling inside the area. The current chapter is highlighted and
// scrolled into view when the bar opens; a tap jumps to the chapter.
SilicaListView {
    id: list

    property int visibleRows: 5
    // The bar is open: show the current chapter
    property bool expanded: false

    readonly property var player: appWindow.player
    readonly property real rowHeight: Theme.itemSizeSmall

    height: Math.min(count, visibleRows) * rowHeight
    clip: true
    interactive: count > visibleRows
    model: player.chapters

    function showCurrent() {
        if (player.chapterIndex >= 0 && player.chapterIndex < count) {
            positionViewAtIndex(player.chapterIndex, ListView.Center)
        }
    }

    onExpandedChanged: {
        if (expanded) {
            showCurrent()
        }
    }
    onCountChanged: {
        if (expanded) {
            showCurrent()
        }
    }

    delegate: BackgroundItem {
        id: row
        readonly property bool current: index === list.player.chapterIndex
        width: list.width
        height: list.rowHeight
        highlighted: down || current
        onClicked: list.player.seek(modelData.start / 1000)

        Label {
            id: timeLabel
            x: Theme.horizontalPageMargin
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.itemSizeMedium
            text: TimeFormat.clock(modelData.start / 1000)
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
        Label {
            anchors {
                left: timeLabel.right
                leftMargin: Theme.paddingMedium
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            text: modelData.title
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            font.bold: row.current
            color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
    }

    VerticalScrollDecorator {}
}

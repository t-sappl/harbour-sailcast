// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (256) The two explanations of the Podcasts page, drawn with the real
// symbols: the blue glowing ball of NewDot = new episodes, the badge of
// the cover grid = unplayed episodes (255 used "●" and "⑩" from the font -
// they did not look like the symbols on the covers). Right-aligned under
// the first open folder header (Thomas 6 Nov, variant A); without folders
// under the sort line. (257, Thomas: "new episodes" under the number pill
// read as the meaning of the number, which stays while the dot goes) The
// dot with its real meaning, and the number always explained with a neutral
// sample "10": list view in the outlined pill of the folder header, grid
// view in the red badge of the covers.
Item {
    id: legend

    // true: the red badge of the cover grid; false: the outlined pill of
    // the folder header
    property bool gridStyle: true
    // The number in the sample pill/badge: a neutral example, not the
    // folder's own number (it is in the header right above - Thomas: no
    // second display of the same number); the line appears once per page
    property bool alignRight: true
    // (258, Thomas 6 Nov: no pill exists in a list without folder headers)
    // false: the dot only - the rows say "unplayed" in words
    property bool showNumber: true

    width: parent ? parent.width : 0
    implicitHeight: row.height + Theme.paddingSmall
    // Width of "· (10) unplayed" after the text
    readonly property real tail: sep.width + (gridStyle ? badge.width : pill.width) + unplayedLabel.width

    Row {
        id: row
        anchors {
            right: legend.alignRight ? parent.right : undefined
            left: legend.alignRight ? undefined : parent.left
            rightMargin: Theme.horizontalPageMargin
            leftMargin: Theme.horizontalPageMargin
            top: parent.top
        }
        spacing: Theme.paddingSmall

        NewDot {
            id: dot
            anchors.verticalCenter: parent.verticalCenter
            size: Theme.iconSizeSmall
        }
        // (268, Thomas 6 Nov: "since your last visit" - a visit of what?)
        // Names the podcast: the dot goes when its page is opened (seenAt,
        // 253). Longer now, so it wraps instead of running off the left
        // edge in large font sizes (TextMetrics: no binding loop on Qt 5.6)
        TextMetrics {
            id: newMetrics
            font: newLabel.font
            text: newLabel.text
        }
        Label {
            id: newLabel
            anchors.verticalCenter: parent.verticalCenter
            width: Math.min(Math.ceil(newMetrics.advanceWidth) + 1,
                            legend.width - 2 * Theme.horizontalPageMargin - dot.width
                            - (legend.showNumber ? legend.tail + 4 * row.spacing : row.spacing))
            wrapMode: Text.Wrap
            horizontalAlignment: legend.alignRight ? Text.AlignRight : Text.AlignLeft
            text: qsTr("new since you last opened the podcast")
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
        Label {
            id: sep
            visible: legend.showNumber
            anchors.verticalCenter: parent.verticalCenter
            text: "·"
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
        // Same look as the badge of PodcastTile
        Rectangle {
            id: badge
            visible: legend.showNumber && legend.gridStyle
            anchors.verticalCenter: parent.verticalCenter
            height: badgeLabel.height + Theme.paddingSmall / 2
            width: Math.max(height, badgeLabel.width + Theme.paddingSmall)
            radius: height / 2
            color: Theme.highlightBackgroundColor
            Label {
                id: badgeLabel
                anchors.centerIn: parent
                text: "10"
                font.pixelSize: Theme.fontSizeTiny
                font.bold: true
                color: Theme.primaryColor
            }
        }
        // Same look as the pill of the folder header
        Rectangle {
            id: pill
            visible: legend.showNumber && !legend.gridStyle
            anchors.verticalCenter: parent.verticalCenter
            height: pillLabel.height + Theme.paddingSmall / 2
            width: Math.max(height, pillLabel.width + Theme.paddingMedium)
            radius: height / 2
            color: "transparent"
            border.color: Theme.rgba(Theme.secondaryHighlightColor, 0.6)
            border.width: 1
            Label {
                id: pillLabel
                anchors.centerIn: parent
                text: "10"
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
            }
        }
        Label {
            id: unplayedLabel
            visible: legend.showNumber
            anchors.verticalCenter: parent.verticalCenter
            text: qsTr("unplayed")
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (279) A section header that folds its section in and out (Downloads:
// "Downloaded", "Archived"): the name in the accent colour like a section
// header, below it in small the number and size, and an arrow that turns.
// The page keeps the state (it is not stored) and listens to clicked().
BackgroundItem {
    id: header

    property string title: ""
    property string subtitle: ""
    property bool open: false
    // (284) false: a plain header (no arrow, no tap), e.g. "Downloaded"
    property bool foldable: true
    enabled: foldable

    width: parent ? parent.width : Screen.width
    height: Theme.itemSizeMedium

    Column {
        anchors {
            left: parent.left
            right: header.foldable ? chevron.left : parent.right
            leftMargin: Theme.horizontalPageMargin
            rightMargin: header.foldable ? Theme.paddingSmall : Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignRight
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.highlightColor
            text: header.title
        }
        Label {
            width: parent.width
            horizontalAlignment: Text.AlignRight
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: header.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            text: header.subtitle
        }
    }

    Label {
        id: chevron
        visible: header.foldable
        anchors {
            right: parent.right
            rightMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        text: "›"
        rotation: header.open ? 90 : 0
        color: Theme.highlightColor
    }
}

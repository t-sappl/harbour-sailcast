// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// One entry of the import/export page: title and an explanation below
BackgroundItem {
    id: item
    property string title: ""
    property string description: ""

    width: parent ? parent.width : 0
    height: textColumn.height + 2 * Theme.paddingMedium
    opacity: enabled ? 1.0 : 0.4

    Column {
        id: textColumn
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter

        Label {
            width: parent.width
            text: item.title
            truncationMode: TruncationMode.Fade
            color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            text: item.description
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }
}

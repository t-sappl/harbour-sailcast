// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Round button "−30" / "+30" for skipping backwards/forwards
BackgroundItem {
    id: button
    property int seconds: 30

    width: Theme.itemSizeMedium
    height: width

    Rectangle {
        anchors.fill: parent
        anchors.margins: Theme.paddingSmall
        radius: width / 2
        color: "transparent"
        border.width: Math.max(1, Theme.paddingSmall / 3)
        border.color: button.down ? Theme.highlightColor : Theme.primaryColor
    }

    Label {
        anchors.centerIn: parent
        text: (button.seconds < 0 ? "−" : "+") + Math.abs(button.seconds)
        font.pixelSize: Theme.fontSizeSmall
        font.bold: true
        color: button.down ? Theme.highlightColor : Theme.primaryColor
    }
}

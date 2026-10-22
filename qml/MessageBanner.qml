// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Short message above the player bar, disappears after a few seconds.
// Tap: hide at once.
Rectangle {
    id: banner

    function show(text) {
        label.text = text
        opacity = 1
        hideTimer.restart()
    }

    width: parent.width - 2 * Theme.horizontalPageMargin
    x: Theme.horizontalPageMargin
    height: label.height + 2 * Theme.paddingMedium
    radius: Theme.paddingSmall
    color: Theme.rgba(Theme.highlightDimmerColor, 0.95)
    border.color: Theme.rgba(Theme.highlightColor, 0.4)
    border.width: 1
    opacity: 0
    visible: opacity > 0
    Behavior on opacity { FadeAnimation { duration: 250 } }

    Label {
        id: label
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        font.pixelSize: Theme.fontSizeSmall
        color: Theme.highlightColor
    }

    MouseArea {
        anchors.fill: parent
        onClicked: banner.opacity = 0
    }

    Timer {
        id: hideTimer
        interval: 4000
        onTriggered: banner.opacity = 0
    }
}

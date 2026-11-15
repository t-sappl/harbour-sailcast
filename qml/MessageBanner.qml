// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Short message above the player bar, disappears after a few seconds.
// Tap: hide at once.
Rectangle {
    id: banner

    // (289) A message that comes while another one is still shown is put
    // under it (up to three lines) instead of replacing it: a failure was
    // overwritten within seconds by "n new episodes" and not seen
    property var _lines: []

    function show(text) {
        var lines = opacity > 0 ? _lines.slice() : []
        if (lines.indexOf(text) < 0) {
            lines.push(text)
        }
        while (lines.length > 3) {
            lines.shift()
        }
        _lines = lines
        label.text = lines.join("\n")
        opacity = 1
        hideTimer.interval = 4000 + (lines.length - 1) * 1500
        hideTimer.restart()
    }

    width: parent.width - 2 * Theme.horizontalPageMargin
    x: Theme.horizontalPageMargin
    height: label.height + 2 * Theme.paddingMedium
    radius: Theme.paddingMedium
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

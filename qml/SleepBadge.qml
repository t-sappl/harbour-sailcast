// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (Taken over from Sailwave.) Small badge with a moon symbol and the remaining time while the sleep
// timer is running. Meant for the top-left corner of cover images.
//
// compact: moon + minutes only (for small areas such as the PlayerBar)
//
// The moon is built from two circles (a light circle with an offset circle
// in the background colour on top). This only works on an opaque
// background, which is why the pill is deliberately not transparent.

Rectangle {
    id: badge

    property bool compact: false
    readonly property bool running: appWindow.sleepTimer && appWindow.sleepTimer.active
    readonly property int minutes: appWindow.sleepTimer ? appWindow.sleepTimer.remainingMinutes : 0

    height: compact ? Math.round(Theme.iconSizeExtraSmall * 0.85) : Theme.iconSizeSmall
    width: contentRow.width + height * 0.7
    radius: height / 2
    color: "#1c1c1c"

    visible: opacity > 0
    opacity: running ? 1.0 : 0.0
    Behavior on opacity { FadeAnimation {} }

    Row {
        id: contentRow
        anchors.centerIn: parent
        spacing: Math.round(badge.height * 0.18)

        Item {
            id: moon
            width: Math.round(badge.height * 0.55)
            height: width
            anchors.verticalCenter: parent.verticalCenter
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: width / 2
                color: "white"
            }
            Rectangle {
                width: parent.width
                height: parent.height
                radius: width / 2
                color: badge.color
                x: parent.width * 0.38
                y: -parent.height * 0.22
            }
        }

        Label {
            anchors.verticalCenter: parent.verticalCenter
            text: badge.compact ? String(badge.minutes) : qsTr("%n min", "", badge.minutes)
            color: "white"
            font.pixelSize: badge.compact ? Theme.fontSizeTiny : Theme.fontSizeExtraSmall
        }
    }
}

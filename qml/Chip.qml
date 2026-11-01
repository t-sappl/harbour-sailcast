// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// A small rounded label to tap (139, "Meine Interessen"): languages and
// keywords. "on" fills it with the highlight background.
Rectangle {
    id: chip
    property alias text: label.text
    property bool on: false
    property bool negative: false   // a "Nie" keyword: struck through
    signal clicked()

    width: label.implicitWidth + 2 * Theme.paddingMedium
    height: label.implicitHeight + Theme.paddingSmall * 2
    radius: height / 2
    color: mouse.pressed ? Theme.rgba(Theme.highlightBackgroundColor, 0.5)
                         : (on ? Theme.rgba(Theme.highlightBackgroundColor, 0.35) : "transparent")
    border.width: on ? 0 : 1
    border.color: Theme.rgba(Theme.primaryColor, 0.3)

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Theme.fontSizeSmall
        font.strikeout: chip.negative
        color: chip.on || mouse.pressed ? Theme.highlightColor : Theme.primaryColor
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        onClicked: chip.clicked()
    }
}

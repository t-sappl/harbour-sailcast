// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (253, usability audit) Small bubble with a name above an icon-only
// button, shown while the button is held - Silica has no tooltip/hover,
// and this is the same look and placement as the skip buttons' hold
// bubble (248). A pure overlay (negative y, not part of any layout), so
// it never changes the width or height of the row it sits in.
//
// (256) With "attached: true" it handles press and hold itself (see
// takeHeld). Otherwise the enclosing button is responsible for setting
// "shown" true on
// onPressAndHold and false again on release/cancel, and for suppressing
// its own "clicked" action after a hold (see SkipButton's "_held"
// pattern, or any of the usage sites: PlayerBar.qml, PlayerPanel.qml).
Rectangle {
    id: bubble

    property string name: ""
    property bool shown: false
    // (256) Attached mode: the bubble follows press-and-hold of its parent
    // button by itself; the button's onClicked starts with
    // "if (nameBubble.takeHeld()) return" so a hold does nothing else
    property bool attached: false
    property bool _held: false
    function takeHeld() {
        var held = _held
        _held = false
        return held
    }
    // The button is raised while the bubble shows, so no neighbour paints
    // over it
    onShownChanged: if (attached && parent) parent.z = shown ? 100 : 0

    Connections {
        target: bubble.attached ? bubble.parent : null
        ignoreUnknownSignals: true
        onPressAndHold: {
            bubble._held = true
            bubble.shown = true
        }
        onPressedChanged: if (!bubble.parent.pressed) bubble.shown = false
        onCanceled: {
            bubble._held = false
            bubble.shown = false
        }
    }

    visible: shown && name.length > 0
    anchors.horizontalCenter: parent.horizontalCenter
    y: -height - Theme.paddingSmall
    z: 10
    width: label.width + 2 * Theme.paddingMedium
    height: label.height + Theme.paddingSmall
    radius: height / 2
    color: Qt.rgba(0, 0, 0, 0.75)

    Label {
        id: label
        anchors.centerIn: parent
        font.pixelSize: Theme.fontSizeExtraSmall
        color: "white"
        text: bubble.name
    }
}

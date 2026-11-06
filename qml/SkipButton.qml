// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Round button "−30" / "+30" for skipping backwards/forwards.
// (246) Tap = the skip time; press and hold = the large step ("player.
// skipLarge", 1 min), repeated every half second while held (as the
// auto-fire buttons of gpodder-sailfish). Use onSkip, not onClicked.
BackgroundItem {
    id: button
    property int seconds: 30
    readonly property int largeSeconds: seconds < 0 ? -appWindow.player.skipLargeSeconds
                                                    : appWindow.player.skipLargeSeconds
    signal skip(int seconds)
    property bool _held: false
    // (248) While held: the jumped total and the target, in a bubble above
    property int _holdTotal: 0
    function _holdSkip() {
        _holdTotal += largeSeconds
        skip(largeSeconds)
    }

    onClicked: {
        if (_held) {
            _held = false
            return
        }
        skip(seconds)
        appWindow.showHint("skipHold", qsTr("Press and hold the skip buttons to jump %1 at a time").arg(
                               appWindow.player.skipLargeSeconds % 60 === 0
                               ? qsTr("%n min", "", appWindow.player.skipLargeSeconds / 60)
                               : appWindow.player.skipLargeSeconds + " s"), button)
    }
    onPressAndHold: {
        _held = true
        _holdTotal = 0
        _holdSkip()
        repeatTimer.start()
    }
    onPressedChanged: {
        if (!pressed) {
            repeatTimer.stop()
            _holdTotal = 0
        }
    }
    onCanceled: {
        repeatTimer.stop()
        _held = false
        _holdTotal = 0
    }
    Timer {
        id: repeatTimer
        interval: 500
        repeat: true
        onTriggered: button._holdSkip()
    }

    Rectangle {
        id: bubble
        visible: button._holdTotal !== 0
        anchors.horizontalCenter: parent.horizontalCenter
        y: -height - Theme.paddingSmall
        z: 10
        width: bubbleLabel.width + 2 * Theme.paddingMedium
        height: bubbleLabel.height + Theme.paddingSmall
        radius: height / 2
        color: Qt.rgba(0, 0, 0, 0.75)
        Label {
            id: bubbleLabel
            anchors.centerIn: parent
            font.pixelSize: Theme.fontSizeExtraSmall
            color: "white"
            text: {
                var total = Math.abs(button._holdTotal)
                var amount = total % 60 === 0 ? qsTr("%n min", "", total / 60) : total + " s"
                return (button._holdTotal < 0 ? "−" : "+") + amount + " → "
                        + TimeFormat.clock(Math.max(0, appWindow.player.position))
            }
        }
    }

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
        // (181) Smaller circles (player bar) get the smaller text
        font.pixelSize: button.width < Theme.itemSizeSmall ? Theme.fontSizeExtraSmall : Theme.fontSizeSmall
        font.bold: true
        color: button.down ? Theme.highlightColor : Theme.primaryColor
    }
}

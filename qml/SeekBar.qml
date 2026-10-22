// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Position bar of the episode page. Unlike a Silica Slider (which takes
// every horizontal touch across the whole width) only the knob can be
// dragged: swiping left/right over the bar - the gesture for going back or
// to the next page - no longer seeks by accident. Tapping the bar does
// nothing either. While dragging, the target time is shown above the knob;
// the seek happens on release.
Item {
    id: bar

    property int position: 0     // seconds
    property int duration: 0     // seconds

    signal seekRequested(int seconds)

    readonly property bool dragging: knobArea.pressed && knobArea.moved
    property real _dragValue: 0
    // After the release the target is kept briefly, until the player has
    // reached it (no jump back of the knob)
    readonly property real _value: dragging || holdTimer.running ? _dragValue : position

    Timer {
        id: holdTimer
        interval: 1500
    }
    readonly property real _ratio: duration > 0 ? Math.max(0, Math.min(1, _value / duration)) : 0

    height: Theme.itemSizeMedium

    Item {
        id: track
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        height: parent.height
        readonly property real centerY: Theme.paddingLarge + Theme.paddingSmall

        Rectangle {
            id: groove
            y: track.centerY - height / 2
            width: parent.width
            height: Math.max(2, Math.round(Theme.paddingSmall / 2))
            radius: height / 2
            color: Theme.rgba(Theme.primaryColor, 0.2)
        }

        Rectangle {
            y: groove.y
            width: parent.width * bar._ratio
            height: groove.height
            radius: groove.radius
            color: Theme.highlightColor
        }

        // Knob
        Rectangle {
            id: knob
            readonly property real size: bar.dragging ? Theme.paddingLarge * 1.6 : Theme.paddingLarge
            width: size
            height: size
            radius: size / 2
            x: track.width * bar._ratio - width / 2
            y: track.centerY - height / 2
            color: Theme.highlightColor
            visible: bar.duration > 0
            Behavior on width { NumberAnimation { duration: 100 } }
        }

        // Target time while dragging
        Label {
            visible: bar.dragging
            anchors.bottom: knob.top
            anchors.bottomMargin: Theme.paddingSmall
            x: Math.max(0, Math.min(track.width - width, knob.x + knob.width / 2 - width / 2))
            text: TimeFormat.clock(bar._dragValue)
            font.pixelSize: Theme.fontSizeSmall
            font.bold: true
            color: Theme.highlightColor
        }

        Label {
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: TimeFormat.clock(bar._value)
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: bar.duration > 0
            text: "−" + TimeFormat.clock(bar.duration - bar._value)
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
        }

        // Touch area of the knob only (a finger's width); stays where the
        // drag started, so it does not move away under the finger
        MouseArea {
            id: knobArea
            property bool moved: false
            property real _pressKnobX: 0
            enabled: bar.duration > 0
            width: Theme.itemSizeSmall
            height: Theme.itemSizeSmall
            x: (pressed ? _pressKnobX : knob.x + knob.width / 2) - width / 2
            y: track.centerY - height / 2
            preventStealing: true

            function valueAt(mouseX) {
                var p = mapToItem(track, mouseX, 0)
                return Math.max(0, Math.min(bar.duration - 1, Math.round(p.x / track.width * bar.duration)))
            }

            onPressed: {
                _pressKnobX = knob.x + knob.width / 2
                moved = false
                bar._dragValue = bar.position
            }
            onPositionChanged: {
                var value = valueAt(mouse.x)
                if (!moved && Math.abs(value - bar.position) * track.width / Math.max(1, bar.duration) < Theme.paddingSmall) {
                    return
                }
                moved = true
                bar._dragValue = value
            }
            onReleased: {
                if (moved) {
                    holdTimer.restart()
                    bar.seekRequested(bar._dragValue)
                }
                moved = false
            }
            onCanceled: moved = false
        }
    }
}

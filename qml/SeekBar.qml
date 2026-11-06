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
// (179) compact: one line for the player bar - elapsed time, bar, remaining
// time side by side, a smaller knob that stays inside the bar; while
// dragging the elapsed time shows the target (no label above the knob).
Item {
    id: bar

    property int position: 0     // seconds
    property int duration: 0     // seconds
    property bool compact: false

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

    height: compact ? Theme.itemSizeExtraSmall : Theme.itemSizeMedium

    // (179) Compact: the times beside the bar, with a fixed width (the
    // widest form for this length) so the bar does not jump while playing
    Label {
        id: widthProbe
        visible: false
        text: bar.duration >= 3600 ? "−0:00:00" : "−00:00"
        font.pixelSize: Theme.fontSizeExtraSmall
        font.bold: true
    }
    Label {
        visible: bar.compact
        x: Theme.horizontalPageMargin
        width: widthProbe.implicitWidth
        anchors.verticalCenter: parent.verticalCenter
        text: TimeFormat.clock(bar._value)
        font.pixelSize: Theme.fontSizeExtraSmall
        font.bold: bar.dragging
        color: bar.dragging ? Theme.highlightColor : Theme.secondaryHighlightColor
    }
    Label {
        visible: bar.compact && bar.duration > 0
        anchors.right: parent.right
        anchors.rightMargin: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: widthProbe.implicitWidth
        horizontalAlignment: Text.AlignRight
        text: "−" + TimeFormat.clock(Math.max(0, bar.duration - bar._value))
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryHighlightColor
    }

    Item {
        id: track
        readonly property real labelSpace: bar.compact ? widthProbe.implicitWidth + Theme.paddingMedium : 0
        x: Theme.horizontalPageMargin + labelSpace
        width: parent.width - 2 * Theme.horizontalPageMargin - 2 * labelSpace
        height: parent.height
        readonly property real centerY: bar.compact ? height / 2 : Theme.paddingLarge + Theme.paddingSmall

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
            readonly property real size: bar.compact
                                         ? (bar.dragging ? Theme.paddingLarge * 1.3 : Theme.paddingMedium * 1.4)
                                         : (bar.dragging ? Theme.paddingLarge * 1.6 : Theme.paddingLarge)
            width: size
            height: size
            radius: size / 2
            // Compact: always inside the bar (not half over its ends)
            x: bar.compact ? (track.width - width) * bar._ratio : track.width * bar._ratio - width / 2
            y: track.centerY - height / 2
            color: Theme.highlightColor
            visible: bar.duration > 0
            Behavior on width { NumberAnimation { duration: 100 } }
        }

        // Target time while dragging
        Label {
            visible: bar.dragging && !bar.compact
            anchors.bottom: knob.top
            anchors.bottomMargin: Theme.paddingSmall
            x: Math.max(0, Math.min(track.width - width, knob.x + knob.width / 2 - width / 2))
            text: TimeFormat.clock(bar._dragValue)
            font.pixelSize: Theme.fontSizeSmall
            font.bold: true
            color: Theme.highlightColor
        }

        Label {
            visible: !bar.compact
            anchors.left: parent.left
            anchors.bottom: parent.bottom
            text: TimeFormat.clock(bar._value)
            font.pixelSize: Theme.fontSizeExtraSmall
            // (180) Highlighted while dragging, as in the compact form
            font.bold: bar.dragging
            color: bar.dragging ? Theme.highlightColor : Theme.secondaryHighlightColor
        }

        Label {
            anchors.right: parent.right
            anchors.bottom: parent.bottom
            visible: bar.duration > 0 && !bar.compact
            text: "−" + TimeFormat.clock(Math.max(0, bar.duration - bar._value))
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

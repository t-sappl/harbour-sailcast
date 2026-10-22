// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// One-time hint on the first start (taken over from Sailwave, 52): the
// three ring pages (new episodes, podcasts, playlist) are reached by
// swiping sideways. A
// Silica TouchInteractionHint (circle sliding from right to left, a few
// runs) plus an InteractionHintLabel at the bottom, above the PlayerBar.
// Takes no touches itself, so the app can be used while it runs. Ends after
// the last run or as soon as stop() is called (e.g. on a page change);
// finished() is emitted once, so the caller can store that it was shown.
Item {
    id: ringHint

    // Distance of the label from the bottom (height of the PlayerBar)
    property real bottomMargin: 0
    readonly property bool active: _started || label.opacity > 0

    signal finished()

    property bool _started: false

    function start() {
        if (_started) return
        _started = true
        touchHint.start()
    }

    function stop() {
        if (!_started) return
        _started = false
        touchHint.stop()
        finished()
    }

    TouchInteractionHint {
        id: touchHint
        direction: TouchInteraction.Left
        anchors.verticalCenter: parent.verticalCenter
        loops: 3
        onRunningChanged: if (!running) ringHint.stop()
    }

    InteractionHintLabel {
        id: label
        anchors.bottom: parent.bottom
        anchors.bottomMargin: ringHint.bottomMargin
        width: parent.width
        text: qsTr("Swipe sideways for new episodes, podcasts and the playlist")
        opacity: ringHint._started ? 1.0 : 0.0
        visible: opacity > 0
        Behavior on opacity { FadeAnimation { duration: 400 } }
    }
}

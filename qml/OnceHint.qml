// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// One-time hints above the player bar (52, as in Sailwave): show(key, text)
// shows the text once - unless "hint.<key>" is already stored - for 8
// seconds or until tapped, then stores it. One hint at a time; a second
// request while one is shown waits and follows 1.5 s later. Double tap on
// the icon of the About page resets all hints (AppCore::resetHints).
InteractionHintLabel {
    id: hint

    // While something else is at the bottom of the screen (the swipe hint
    // at the first start, a message), hints wait. A hint already on screen
    // goes back to the front of the queue and comes again afterwards (55,
    // 56: both were drawn on top of each other).
    property bool paused: false
    // Hints that belong to a page wait until it is the current one (66)
    property Item currentPage: null

    property string _key: ""
    property string _text: ""
    property Item _owner: null
    property var _waiting: []
    // Keys known to be shown already - no database read for every list
    // row that asks (e.g. every episode with chapters)
    property var _done: ({})

    function shownBefore(key) {
        if (_done[key] === true) {
            return true
        }
        if (core.setting("hint." + key, "0") === "1") {
            _done[key] = true
            return true
        }
        return false
    }

    function reset() {
        _done = {}
    }

    function show(key, text, owner) {
        if (key === _key || shownBefore(key)) {
            return
        }
        for (var i = 0; i < _waiting.length; i++) {
            if (_waiting[i].key === key) {
                return
            }
        }
        var list = _waiting.slice(0)
        list.push({ key: key, text: text, owner: owner || null, paged: !!owner })
        _waiting = list
        _pump()
    }

    // Shows the next waiting hint when nothing is in the way
    function _pump() {
        if (_key.length > 0 || paused || _waiting.length === 0) {
            return
        }
        // The first one for this page (or for any page)
        var list = _waiting.slice(0)
        var at = -1
        // Entries of a page that is gone (an episode page closed) are dropped
        list = list.filter(function(e) { return !e.paged || e.owner })
        for (var i = 0; i < list.length; i++) {
            if (!list[i].paged || list[i].owner === currentPage) {
                at = i
                break
            }
        }
        if (at < 0) {
            _waiting = list
            return
        }
        var next = list.splice(at, 1)[0]
        _waiting = list
        _owner = next.paged ? next.owner : null
        _key = next.key
        _text = next.text
        hint.text = next.text
        _done[next.key] = true
        core.setSetting("hint." + next.key, "1")
        hideTimer.restart()
    }

    function dismiss() {
        if (_key.length === 0) {
            return
        }
        _key = ""
        hideTimer.stop()
        if (_waiting.length > 0) {
            nextTimer.restart()
        }
    }

    // Another page: a hint of the old page goes back into the queue (and
    // counts as not shown); one of the new page may follow
    onCurrentPageChanged: {
        if (_key.length > 0 && _owner && _owner !== currentPage) {
            var list = _waiting.slice(0)
            list.unshift({ key: _key, text: _text, owner: _owner, paged: true })
            _waiting = list
            delete _done[_key]
            core.setSetting("hint." + _key, "0")
            _key = ""
            hideTimer.stop()
        }
        if (_waiting.length > 0) {
            nextTimer.restart()
        }
    }

    onPausedChanged: {
        if (paused) {
            if (_key.length > 0) {
                // Back to the front of the queue, shown again afterwards
                var list = _waiting.slice(0)
                list.unshift({ key: _key, text: _text, owner: _owner, paged: _owner !== null })
                _waiting = list
                _key = ""
                hideTimer.stop()
            }
            nextTimer.stop()
        } else if (_waiting.length > 0) {
            nextTimer.restart()
        }
    }

    opacity: _key.length > 0 ? 1.0 : 0.0
    visible: opacity > 0
    Behavior on opacity { FadeAnimation { duration: 400 } }

    MouseArea {
        anchors.fill: parent
        onClicked: hint.dismiss()
    }

    Timer {
        id: hideTimer
        interval: 8000
        onTriggered: hint.dismiss()
    }

    Timer {
        id: nextTimer
        interval: 1500
        onTriggered: hint._pump()
    }
}

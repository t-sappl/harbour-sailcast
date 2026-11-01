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
    // (220) Gradient towards the top edge when shown there
    invert: showTop

    // While something else is at the bottom of the screen (the swipe hint
    // at the first start, a message), hints wait. A hint already on screen
    // goes back to the front of the queue and comes again afterwards (55,
    // 56: both were drawn on top of each other).
    property bool paused: false
    // Hints that belong to a page wait until it is the current one (66)
    property Item currentPage: null

    property string _key: ""
    property string _text: ""
    // (220) Shown at the top of the screen instead of above the player bar
    // (player bar hint: not over the list; bookmark hint: above the panel
    // and the keyboard)
    property bool atTop: false
    property bool _top: false
    // (232) While the player bar is expanded, every hint goes to the top
    // (above the bar it would sit in the middle of the list)
    property bool barExpanded: false
    readonly property bool showTop: atTop || barExpanded
    // (231) When the hint came on screen: one that was readable for 2 s
    // counts as read when its page goes or something else comes first
    property real _shownAt: 0
    readonly property int _readMs: 2000
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

    function show(key, text, owner, top) {
        if (key === _key || shownBefore(key)) {
            return
        }
        for (var i = 0; i < _waiting.length; i++) {
            if (_waiting[i].key === key) {
                return
            }
        }
        var list = _waiting.slice(0)
        list.push({ key: key, text: text, owner: owner || null, paged: !!owner, top: !!top })
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
        _top = !!next.top
        atTop = _top
        _shownAt = Date.now()
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
        if (_key.length > 0 && _owner && _owner !== currentPage && Date.now() - _shownAt >= _readMs) {
            // (231) Seen long enough (e.g. the user followed it to another
            // page): read, not queued again
            _key = ""
            hideTimer.stop()
        } else if (_key.length > 0 && _owner && _owner !== currentPage) {
            var list = _waiting.slice(0)
            list.unshift({ key: _key, text: _text, owner: _owner, paged: true, top: _top })
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
            if (_key.length > 0 && Date.now() - _shownAt >= _readMs) {
                // (231) Seen long enough: read
                _key = ""
                hideTimer.stop()
            } else if (_key.length > 0) {
                // Back to the front of the queue, shown again afterwards
                var list = _waiting.slice(0)
                list.unshift({ key: _key, text: _text, owner: _owner, paged: _owner !== null, top: _top })
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

    // (230) A dark backing under the whole text: the label's own gradient
    // is almost transparent at its far edge, so long hints (3-4 lines) lay
    // directly on the list below and could not be read. Short fade at the
    // edge towards the page, then nearly opaque. Drawn below the label's
    // own gradient and text (z -1); turned with "invert" (hint at the top).
    Rectangle {
        z: -1
        anchors.fill: parent
        rotation: hint.showTop ? 180 : 0
        gradient: Gradient {
            GradientStop { position: 0.0; color: Theme.rgba(Theme.highlightDimmerColor, 0.0) }
            GradientStop {
                position: Math.min(0.3, 2 * Theme.paddingLarge / Math.max(1, hint.height))
                color: Theme.rgba(Theme.highlightDimmerColor, 0.95)
            }
            // (231) Nearly opaque: over the expanded player bar's list the
            // rows still showed through at 0.85
            GradientStop { position: 1.0; color: Theme.rgba(Theme.highlightDimmerColor, 0.97) }
        }
    }

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

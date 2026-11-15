// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Nemo.DBus 2.0

// Sailfish has no audio focus and routes the headset key itself. So:
// (1) when Sailcast starts playing (by any route), every OTHER MPRIS player
//     that is playing is paused (e.g. a radio station in Sailwave);
// (2) the headset key arriving while Sailcast is paused: if another player
//     is playing, the key meant "pause that one" (pauseOthers); if another
//     player was paused more recently than Sailcast, the key meant "go on
//     with that one" (resumeLatestOther) - Sailcast must not start itself.
//
// (292) Through plain D-Bus (Nemo.DBus): the first version used the type
// MprisManager, which does not exist in Amber.Mpris (it is in the older
// module org.nemomobile.mpris) - "MprisManager is not a type", the file
// never loaded (device log of 9 Nov, in Sailcast and Sailwave).
//
// How: the services "org.mpris.MediaPlayer2.*" on the session bus are
// listed (ListNames, every 5 s); the playback status of each is read every
// 2 s (org.freedesktop.DBus.Properties.Get, asynchronous) and kept in
// "status"; a change from Playing to Paused is remembered ("pausedAt").
// Pause/Play are sent to /org/mpris/MediaPlayer2.
//
// Loaded through a Loader (like MprisIntegration): without Nemo.DBus access
// the app keeps running, just without this function. Everything is logged
// ("MprisOthers: ...") - Sailjail may filter the bus names of other apps.
Item {
    id: root
    visible: false
    width: 0
    height: 0

    readonly property string ownName: "org.mpris.MediaPlayer2.harbour-sailcast"
    readonly property string prefix: "org.mpris.MediaPlayer2."
    // When Sailcast was paused last (ms since epoch); 0 = not since start
    property double ownPausedAt: 0
    // Foreign players found on the bus
    property var services: []
    // service -> last read "Playing" / "Paused" / "Stopped"
    property var status: ({})
    // service -> when it went from Playing to Paused (ms)
    property var pausedAt: ({})

    function isOwn(service) {
        // also covers suffixed instance names (".instance1234")
        return service === ownName || service.indexOf(ownName + ".") === 0
    }

    // --- Services on the bus ---
    DBusInterface {
        id: lister
        bus: DBus.SessionBus
        service: "org.freedesktop.DBus"
        path: "/org/freedesktop/DBus"
        iface: "org.freedesktop.DBus"
    }

    function refreshServices() {
        lister.call("ListNames", [], function(names) {
            var found = []
            for (var i = 0; i < names.length; ++i) {
                var name = "" + names[i]
                if (name.indexOf(root.prefix) === 0 && !root.isOwn(name)) {
                    found.push(name)
                }
            }
            found.sort()
            if (found.join("|") !== root.services.join("|")) {
                console.log("MprisOthers: other players: " + (found.length > 0 ? found.join(", ") : "none"))
                root.services = found
            }
        }, function(error) {
            console.log("MprisOthers: ListNames failed: " + error)
        })
    }

    Timer {
        interval: 5000
        repeat: true
        running: true
        triggeredOnStart: true
        onTriggered: root.refreshServices()
    }

    // --- Status of the others ---
    Timer {
        id: pollTimer
        interval: 2000
        repeat: true
        running: root.services.length > 0
        onTriggered: {
            for (var i = 0; i < players.count; ++i) {
                var e = players.itemAt(i)
                if (e) {
                    e.poll()
                }
            }
        }
    }

    function noteStatus(service, value) {
        var now = "" + value
        var before = status[service]
        if (before === now) {
            return
        }
        if (before === "Playing" && now === "Paused") {
            pausedAt[service] = Date.now()
        }
        console.log("MprisOthers: " + service + " " + (before || "?") + " -> " + now)
        status[service] = now
    }

    Repeater {
        id: players
        model: root.services
        delegate: Item {
            id: entry
            property string name: modelData

            DBusInterface {
                id: props
                bus: DBus.SessionBus
                service: entry.name
                path: "/org/mpris/MediaPlayer2"
                iface: "org.freedesktop.DBus.Properties"
            }

            DBusInterface {
                id: ctl
                bus: DBus.SessionBus
                service: entry.name
                path: "/org/mpris/MediaPlayer2"
                iface: "org.mpris.MediaPlayer2.Player"
            }

            function poll() {
                props.typedCall("Get", [
                                    { "type": "s", "value": "org.mpris.MediaPlayer2.Player" },
                                    { "type": "s", "value": "PlaybackStatus" }
                                ], function(value) {
                                    root.noteStatus(entry.name, value)
                                }, function(error) {
                                    console.log("MprisOthers: status of " + entry.name + " failed: " + error)
                                })
            }

            function pauseIfPlaying() {
                if (root.status[entry.name] !== "Playing") {
                    return false
                }
                console.log("MprisOthers: pausing " + entry.name)
                ctl.call("Pause", [], function() {}, function(error) {
                    console.log("MprisOthers: Pause failed: " + error)
                })
                // the next poll confirms; remember it as paused now
                root.pausedAt[entry.name] = Date.now()
                root.status[entry.name] = "Paused"
                return true
            }

            function resumeNow() {
                console.log("MprisOthers: resuming " + entry.name)
                ctl.call("Play", [], function() {}, function(error) {
                    console.log("MprisOthers: Play failed: " + error)
                })
                root.status[entry.name] = "Playing"
            }
        }
    }

    // Pauses all other playing players; returns how many were paused
    function pauseOthers() {
        var n = 0
        for (var i = 0; i < players.count; ++i) {
            var e = players.itemAt(i)
            if (e && e.pauseIfPlaying()) {
                ++n
            }
        }
        return n
    }

    // Continues the other (paused) player that stopped most recently, if it
    // stopped after Sailcast did; returns true if one was started
    function resumeLatestOther() {
        var best = null
        var own = settle.running ? root._stoppedAt : root.ownPausedAt
        for (var i = 0; i < players.count; ++i) {
            var e = players.itemAt(i)
            if (!e || root.status[e.name] !== "Paused") {
                continue
            }
            var at = root.pausedAt[e.name] || 0
            if (at <= own) {
                continue
            }
            if (!best || at > (root.pausedAt[best.name] || 0)) {
                best = e
            }
        }
        if (best) {
            best.resumeNow()
            return true
        }
        return false
    }

    // An internal reload (stop, then play within a moment) is not a real
    // pause: it neither marks "paused at" nor pauses the others again
    property double _stoppedAt: 0
    Timer {
        id: settle
        interval: 1500
        onTriggered: {
            if (!appWindow.player.playing) {
                root.ownPausedAt = root._stoppedAt
            }
        }
    }

    Connections {
        target: appWindow.player
        onPlayingChanged: {
            if (appWindow.player.playing) {
                if (settle.running) {
                    settle.stop()
                } else {
                    root.pauseOthers()
                }
            } else {
                root._stoppedAt = Date.now()
                settle.restart()
            }
        }
    }

    Component.onCompleted: console.log("MprisOthers: ready (D-Bus)")
}

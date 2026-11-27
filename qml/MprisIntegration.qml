// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import QtQml 2.2
import Amber.Mpris 1.0

// Lock screen, multitasking view and headset controls (MPRIS), as in
// Sailwave. Loaded through a Loader: without the Amber.Mpris plugin the app
// keeps working, just without these controls. No visual children here -
// they broke the MPRIS registration in Sailwave.
//
// "Next"/"Previous" as set in "player.remoteNext"/"player.remotePrev" (99; the cover has its
// own setting since 87 - see
// PlayerController.remoteNext): by default next chapter, after the last one
// the next playlist episode, else the latest one in progress / chapter start,
// previous chapter, without chapters the usual skip back.
MprisPlayer {
    id: mpris
    serviceName: "harbour-sailcast"
    identity: "Sailcast"
    desktopEntry: "harbour-sailcast"

    canControl: true
    canPlay: appWindow.player.hasEpisode
    canPause: appWindow.player.hasEpisode
    canGoNext: appWindow.player.hasEpisode
    canGoPrevious: appWindow.player.hasEpisode
    // Seeking for remote controls such as Sailplay's car screen, which
    // draws its progress bar from Position and the length (as in Audiobook
    // and Hutspot)
    canSeek: appWindow.player.hasEpisode && appWindow.player.duration > 0
    canQuit: false
    canRaise: false

    // Paused (not Stopped) keeps the player on the lock screen
    playbackStatus: !appWindow.player.hasEpisode ? Mpris.Stopped
                                                 : (appWindow.player.playing ? Mpris.Playing : Mpris.Paused)

    // The lock screen does not scroll (checked 13 Oct): keep the title short -
    // the chapter alone with a small arrow, else the episode title; the
    // podcast is the artist line (24)
    metaData.title: appWindow.player.chapterTitle.length > 0
                    ? "\u25B8 " + appWindow.player.chapterTitle
                    : appWindow.player.title
    metaData.contributingArtist: appWindow.player.hasEpisode ? [appWindow.player.podcastTitle] : []
    metaData.artUrl: appWindow.mprisArtUrl
    // Length in milliseconds (Amber converts for D-Bus)
    metaData.duration: appWindow.player.hasEpisode ? appWindow.player.duration * 1000 : 0

    // Position in milliseconds, read on demand (no signal every second)
    onPositionRequested: position = Math.max(0, appWindow.player.positionMs)
    onSeekRequested: _seekTo(appWindow.player.positionMs + offset)
    onSetPositionRequested: _seekTo(position)
    function _seekTo(ms) {
        if (!appWindow.player.hasEpisode) {
            return
        }
        var target = Math.max(0, Math.min(ms, appWindow.player.duration * 1000))
        appWindow.player.seek(Math.floor(target / 1000))
        _lastPosition = target
        _lastPositionTime = Date.now()
        mpris.seeked(target)
    }

    // MPRIS asks to announce jumps (chapter, skip, a jump on the phone), so
    // that a remote control does not keep counting from the old place
    property real _lastPosition: 0
    property real _lastPositionTime: 0
    // As a property, not a child: MprisPlayer takes no child objects
    property Connections _jumps: Connections {
        target: appWindow.player
        onPositionMsChanged: {
            var now = Date.now()
            var pos = appWindow.player.positionMs
            var expected = mpris._lastPosition
                    + (appWindow.player.playing ? (now - mpris._lastPositionTime) * appWindow.player.rate : 0)
            if (mpris._lastPositionTime > 0 && Math.abs(pos - expected) > 3000) {
                mpris.seeked(pos)
            }
            mpris._lastPosition = pos
            mpris._lastPositionTime = now
        }
        onEpisodeIdChanged: {
            mpris._lastPositionTime = 0
        }
    }

    onPlayRequested: appWindow.player.resume()
    onPauseRequested: appWindow.player.pause()
    // Headset key while Sailcast is paused: Sailfish sends it here even when
    // another player (e.g. a radio station) is playing - then it means "pause
    // that one" - or was paused after Sailcast - then it means "go on with
    // that one". Sailcast must not start itself in both cases.
    onPlayPauseRequested: {
        if (!appWindow.player.playing
                && (appWindow.pauseOtherPlayers() > 0 || appWindow.resumeOtherPlayer())) {
            return
        }
        appWindow.player.toggle()
    }
    onStopRequested: appWindow.player.pause()
    onNextRequested: appWindow.player.remoteNext()
    onPreviousRequested: appWindow.player.remotePrevious()
}

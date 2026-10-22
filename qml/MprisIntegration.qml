// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Amber.Mpris 1.0

// Lock screen, multitasking view and headset controls (MPRIS), as in
// Sailwave. Loaded through a Loader: without the Amber.Mpris plugin the app
// keeps working, just without these controls. No visual children here -
// they broke the MPRIS registration in Sailwave.
//
// "Next": next chapter, after the last one the next playlist episode, else
// the latest one in progress (as the cover). "Previous": chapter start /
// previous chapter, without chapters the usual skip back.
MprisPlayer {
    serviceName: "harbour-sailcast"
    identity: "Sailcast"
    desktopEntry: "harbour-sailcast"

    canControl: true
    canPlay: appWindow.player.hasEpisode
    canPause: appWindow.player.hasEpisode
    canGoNext: appWindow.player.hasEpisode
    canGoPrevious: appWindow.player.hasEpisode
    canSeek: false
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

    onPlayRequested: appWindow.player.resume()
    onPauseRequested: appWindow.player.pause()
    onPlayPauseRequested: appWindow.player.toggle()
    onStopRequested: appWindow.player.pause()
    onNextRequested: appWindow.player.next()
    onPreviousRequested: appWindow.player.previous()
}

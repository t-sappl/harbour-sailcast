// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import QtMultimedia 5.6

// (241/242) The picture of a video episode. The one MediaPlayer of the
// PlayerController plays video files already (sound only until 240) - a
// VideoOutput attached to it shows the picture. Detaching and attaching an
// output makes the player rebuild (241: a second output stayed black,
// changing pages buffered), so there is one view per episode page, kept
// while the page exists, and full screen moves this same item (242).
// (243) The page detaches it while it is not on screen (another page on
// top, app in the background) - an output that is not drawn stalls the
// player; the PlayerController loads the source again on every change.
Rectangle {
    id: view

    // Set by the page: the current episode has a picture
    property bool wanted: true
    // (245) The episode image over the area (paused, sound only, before the
    // first picture) - black was shown until now
    property bool posterVisible: false
    property string episodeImageUrl: ""
    property string coverUrl: ""
    property string fallbackText: ""

    color: "black"

    Loader {
        anchors.fill: parent
        active: view.wanted
        sourceComponent: Component {
            VideoOutput {
                source: appWindow.player.mediaPlayer
                fillMode: VideoOutput.PreserveAspectFit
                // (243) The player loads again when the first output comes
                // or the last one goes (attaching while playing hangs)
                Component.onCompleted: appWindow.player.videoOutputAdded()
                Component.onDestruction: appWindow.player.videoOutputRemoved()
            }
        }
    }

    Rectangle {
        anchors.fill: parent
        color: "black"
        visible: view.posterVisible

        EpisodeArt {
            anchors.centerIn: parent
            height: Math.round(parent.height * 0.85)
            width: height
            episodeImageUrl: view.episodeImageUrl
            coverUrl: view.coverUrl
            fallbackText: view.fallbackText
            showBadge: false
        }
    }
}

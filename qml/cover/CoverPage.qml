// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// App cover: podcast name (fixed, one line), image (episode image with the
// podcast cover as a badge), the chapter ("▸ chapter") or else the episode
// title in up to 3 lines - no scrolling, covers are read at a glance (24) -
// and the remaining time, with play/pause and
// "next" (next chapter; after the last one the next playlist episode, else
// the latest one in progress - see PlayerController.next()); without an
// episode the app name.
CoverBackground {
    readonly property var player: appWindow.player

    Column {
        anchors.centerIn: parent
        width: parent.width - 2 * Theme.paddingLarge
        spacing: Theme.paddingSmall
        visible: player.hasEpisode

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: player.podcastTitle
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }

        EpisodeArt {
            anchors.horizontalCenter: parent.horizontalCenter
            width: parent.width * 0.6
            height: width
            episodeImageUrl: player.episodeImageUrl
            coverUrl: player.coverUrl
            fallbackText: player.podcastTitle
            badgeFactor: 0.38
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: player.chapterTitle.length > 0 ? "\u25B8 " + player.chapterTitle : player.title
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeSmall
        }

        Label {
            width: parent.width
            horizontalAlignment: Text.AlignHCenter
            text: player.duration > 0 ? "-" + Math.ceil((player.duration - player.position) / 60) + " min" : ""
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }
    }

    Label {
        anchors.centerIn: parent
        visible: !player.hasEpisode
        text: "Sailcast"
        font.pixelSize: Theme.fontSizeLarge
    }

    CoverActionList {
        enabled: player.hasEpisode
        CoverAction {
            iconSource: player.playing ? "image://theme/icon-cover-pause" : "image://theme/icon-cover-play"
            onTriggered: player.toggle()
        }
        CoverAction {
            iconSource: "image://theme/icon-cover-next"
            onTriggered: player.next()
        }
    }
}

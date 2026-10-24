// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// App cover: podcast name (fixed, one line), image (episode image with the
// podcast cover as a badge), the chapter ("▸ chapter") or else the episode
// title in up to 3 lines - no scrolling, covers are read at a glance (24) -
// and the remaining time, with play/pause and
// a second button as set (87, PlayerController.coverButton): next (chapter,
// then the next playlist episode, else the latest one in progress), skip
// back, skip forward or the sleep timer; without an episode the app name.
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
            text: feedbackTimer.running ? feedbackText : player.podcastTitle
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: feedbackTimer.running ? Theme.highlightColor : Theme.secondaryColor
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

    // Short confirmation of a bookmark set from the cover (93): shown for
    // a moment instead of the podcast name
    property string feedbackText: ""
    Timer {
        id: feedbackTimer
        interval: 2500
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
        // Second button as set (87): next / skip back / skip forward /
        // sleep timer (15 minutes, a second tap ends it)
        CoverAction {
            iconSource: player.coverButton === "back" ? "image://theme/icon-cover-previous"
                      : player.coverButton === "sleep" ? Qt.resolvedUrl("../images/cover-sleep.png")
                      : player.coverButton === "bookmark" ? Qt.resolvedUrl("../images/cover-bookmark.png")
                      : "image://theme/icon-cover-next"
            onTriggered: {
                if (player.coverButton === "bookmark") {
                    // (93) An unnamed bookmark at the current position;
                    // the cover shows it for a moment
                    var seconds = player.position
                    if (core.addBookmark(player.episodeId, seconds) > 0) {
                        feedbackText = "\u2605 " + TimeFormat.clock(seconds)
                        feedbackTimer.restart()
                    }
                } else if (player.coverButton === "sleep") {
                    if (appWindow.sleepTimer.active) {
                        appWindow.sleepTimer.cancel()
                    } else {
                        appWindow.sleepTimer.start(15)
                    }
                } else {
                    player.coverAction()
                }
            }
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// All episodes in progress, most recently played first ("Show all" under
// "Continue listening" on the playlist page). The episode playing now is
// left out - it is "Now playing" on the playlist page.
Page {
    id: page
    objectName: "continueListeningPage"

    EpisodeListModel {
        id: episodes
        filter: EpisodeListModel.InProgress
        excludeEpisodeId: appWindow.player.hasEpisode ? appWindow.player.episodeId : -1
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: episodes
        cacheBuffer: height

        header: PageHeader {
            title: qsTr("Continue listening")
            description: qsTr("%n episodes", "", episodes.count)
        }

        delegate: EpisodeDelegate {}

        ViewPlaceholder {
            enabled: episodes.count === 0
            text: qsTr("No episodes in progress")
        }

        VerticalScrollDecorator {}
    }
}

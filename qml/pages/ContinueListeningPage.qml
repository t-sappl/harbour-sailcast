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
        // Opens at the top with the page header (66, 83): no current item
        // (ListView keeps item 0 in view while the header grows), and the
        // top is kept until the user moves the list
        currentIndex: -1
        property bool _keepTop: true
        onMovementStarted: _keepTop = false
        onContentHeightChanged: if (_keepTop) positionViewAtBeginning()
        onOriginYChanged: if (_keepTop) positionViewAtBeginning()
        Component.onCompleted: positionViewAtBeginning()
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

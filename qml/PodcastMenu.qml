// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Context menu of a podcast in the Podcasts page - the same for the list
// rows and the grid tiles (65). Actions with dialogs or countdowns run in
// the app window (see remorseUnsubscribe).
ContextMenu {
    property int podcastId: 0
    property string title: ""
    property int unplayed: 0

    MenuItem {
        text: qsTr("Update")
        onClicked: core.updatePodcast(podcastId)
    }
    MenuItem {
        visible: unplayed > 0
        text: qsTr("Mark all as played")
        onClicked: appWindow.remorseMarkAllPlayed(podcastId, title)
    }
    MenuItem {
        text: qsTr("Podcast settings")
        onClicked: pageStack.push(Qt.resolvedUrl("pages/PodcastSettingsPage.qml"),
                                  { podcastId: podcastId, podcastTitle: title })
    }
    MenuItem {
        text: qsTr("Move to folder …")
        onClicked: appWindow.movePodcastToFolder(podcastId)
    }
    MenuItem {
        text: qsTr("Unsubscribe")
        onClicked: appWindow.remorseUnsubscribe(podcastId, title)
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// Start page: the newest unplayed episodes of all podcasts; started ones
// show their progress (the episode playing now live). "Continue listening"
// is on the playlist page since build 12.
// Part of the ring navigation (swipe left: podcasts, playlist).
Page {
    id: page
    objectName: "homePage"
    property bool ringMember: false

    EpisodeListModel {
        id: episodes
        filter: EpisodeListModel.Home
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
        // Keeps a few screens of delegates (and their images) alive
        cacheBuffer: height

        header: PageHeader {
            title: qsTr("New episodes")
            description: core.updating ? qsTr("Updating %1 %").arg(Math.round(core.updateProgress * 100))
                                       : (core.syncing ? qsTr("Synchronizing") : "")
        }

        PullDownMenu {
            busy: core.updating || core.syncing
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                visible: core.podcastCount === 0
                text: qsTr("Import and export")
                onClicked: pageStack.push(Qt.resolvedUrl("ImportExportPage.qml"))
            }
            // Downloads and bookmarks: in the playlist pulley since 73
            // Search on the device (86)
            MenuItem {
                visible: core.podcastCount > 0
                text: qsTr("Search")
                onClicked: pageStack.push(Qt.resolvedUrl("LocalSearchPage.qml"))
            }
            // Separate sync entry (86) when "Sync und Update" is off
            MenuItem {
                visible: appWindow.separateSync
                enabled: !core.syncing
                text: qsTr("Synchronize")
                onClicked: appWindow.syncOnly()
            }
            MenuItem {
                visible: core.podcastCount > 0 || core.syncConfigured
                enabled: !core.updating && !core.syncing
                // Sync (if set up and combined, 86) and then all feeds - see
                // appWindow.updateFromPulley
                text: qsTr("Update")
                onClicked: appWindow.updateFromPulley()
            }
        }

        delegate: EpisodeDelegate {}

        ViewPlaceholder {
            enabled: episodes.count === 0
            text: core.databaseError.length > 0 ? qsTr("Database error")
                                                : (core.podcastCount === 0 ? qsTr("No podcasts yet")
                                                                           : qsTr("All caught up"))
            hintText: core.databaseError.length > 0 ? core.databaseError
                                                    : (core.podcastCount === 0
                                                       ? qsTr("Swipe to Podcasts and pull down to search for podcasts, or set up the sync in the settings")
                                                       : qsTr("No unplayed episodes"))
        }

        VerticalScrollDecorator {}
    }
}

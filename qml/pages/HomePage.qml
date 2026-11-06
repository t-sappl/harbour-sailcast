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

        header: Column {
            width: list.width
            PageHeader {
                title: qsTr("New episodes")
                description: core.updating ? qsTr("Updating %1 %").arg(Math.round(core.updateProgress * 100))
                                           : (core.syncing ? qsTr("Synchronizing") : "")
            }
            // (255) Neighbours of the ring, below the header
            RingNeighbours { kind: "home"; ringMember: page.ringMember }
        }

        PullDownMenu {
            busy: core.updating || core.syncing
            // (253) The download count is read when the menu opens
            onActiveChanged: if (active) homeDownloads.refresh()
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                visible: core.podcastCount === 0
                text: qsTr("Import and export")
                onClicked: pageStack.push(Qt.resolvedUrl("ImportExportPage.qml"))
            }
            // Bookmarks: in the playlist pulley since 73, here too since 255
            MenuItem {
                visible: core.bookmarkCount > 0
                text: qsTr("Bookmarks (%1)").arg(core.bookmarkCount)
                onClicked: pageStack.push(Qt.resolvedUrl("BookmarksPage.qml"))
            }
            // (253) Downloads here too (in the playlist pulley since 73,
            // hard to find there - usability audit #10)
            DownloadsMenuItem {
                id: homeDownloads
            }
            // Search on the device (86)
            MenuItem {
                visible: core.podcastCount > 0
                text: qsTr("Search my podcasts")
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

        // (258, Thomas 6 Nov: the menu of a row ran down behind the player
        // bar) _keepTop pulled the list back to the top as soon as the
        // opening menu grew the content, so it was never scrolled into
        // view - an opening menu ends the "keep the top" state
        delegate: EpisodeDelegate {
            newEpisodesList: true
            onMenuOpenChanged: if (menuOpen) list._keepTop = false
        }

        ViewPlaceholder {
            // (219) Without podcasts: GetStarted below
            enabled: episodes.count === 0 && (core.podcastCount > 0 || core.databaseError.length > 0)
            text: core.databaseError.length > 0 ? qsTr("Database error") : qsTr("All caught up")
            hintText: core.databaseError.length > 0 ? core.databaseError : qsTr("No unplayed episodes")
        }

        VerticalScrollDecorator {}
    }

    // (219) First steps as buttons (as on the Podcasts page)
    GetStarted {
        visible: core.podcastCount === 0 && core.databaseError.length === 0
        anchors.verticalCenter: parent.verticalCenter
    }
}

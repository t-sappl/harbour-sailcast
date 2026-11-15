// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Pull-down menu of the podcast page (shared by the grid and the list view)
PullDownMenu {
    id: menu
    // (329) Set by the page: true while there is no folder yet
    property bool showNewFolder: true

    busy: core.updating || core.syncing
    // (253) The download count is read when the menu opens
    onActiveChanged: if (active) podcastsDownloads.refresh()

    MenuItem {
        text: qsTr("Settings")
        onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
    }
    // Import and export: in the settings since 88 (64-87 here, 51-63 in the
    // settings) - not needed every day; here only while there are no
    // podcasts yet (first steps: OPML or gPodder import)
    MenuItem {
        visible: core.podcastCount === 0
        text: qsTr("Import and export")
        onClicked: pageStack.push(Qt.resolvedUrl("ImportExportPage.qml"))
    }
    // (314) Order (all pulleys): settings, own data, tools of the page,
    // finding, update last (nearest to the thumb). Grid/list is in the sort
    // menu of the page now.
    // (297) Listening statistics, behind the bookmarks; the length of the
    // pulley gets another look in the UI review before 1.4
    // (329) Only without the line on the Podcasts page (Settings → Pages and
    // navigation); the statistics stay reachable either way
    MenuItem {
        visible: !appWindow.statsLineOn
        text: qsTr("Listening statistics")
        onClicked: pageStack.push(Qt.resolvedUrl("StatisticsPage.qml"))
    }
    // (329, Thomas: pulley too long) Bookmarks no longer here - they stay in
    // the pulleys of the playlist and New episodes (here since 255)
    // (253) Downloads here too (usability audit #10)
    DownloadsMenuItem {
        id: podcastsDownloads
    }
    // (329) Only for the first folder; with folders it is the first entry of
    // a folder header's menu (and "New folder …" in every folder choice)
    MenuItem {
        visible: menu.showNewFolder
        text: qsTr("New folder")
        onClicked: appWindow.newFolder()
    }
    // Search and feed address in one page (51); only for adding
    MenuItem {
        visible: !appWindow.discoverRingOn
        text: qsTr("Discover")
        onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
    }
    // (237) Search on the device here too - Discover is a swipe page by
    // default (229), so the pulley entry is only needed without the ring
    // (then both: whoever took it out knows the two). Before 237 only
    // "Discover" here, to keep the two apart (86/99).
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

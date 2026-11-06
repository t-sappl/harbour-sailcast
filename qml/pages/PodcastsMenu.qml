// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Pull-down menu of the podcast page (shared by the grid and the list view)
PullDownMenu {
    property bool gridMode: true
    signal toggleView()

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
    // (255) Bookmarks list here too (usability check 254: it was only in
    // the playlist pulley)
    MenuItem {
        visible: core.bookmarkCount > 0
        text: qsTr("Bookmarks (%1)").arg(core.bookmarkCount)
        onClicked: pageStack.push(Qt.resolvedUrl("BookmarksPage.qml"))
    }
    // (253) Downloads here too (usability audit #10)
    DownloadsMenuItem {
        id: podcastsDownloads
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
    // Search and feed address in one page (51); only for adding
    MenuItem {
        visible: !appWindow.discoverRingOn
        text: qsTr("Discover")
        onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"))
    }
    MenuItem {
        text: qsTr("New folder")
        onClicked: appWindow.newFolder()
    }
    MenuItem {
        text: gridMode ? qsTr("Show as list") : qsTr("Show as grid")
        onClicked: toggleView()
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

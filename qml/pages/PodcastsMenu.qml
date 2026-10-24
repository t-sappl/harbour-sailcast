// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Pull-down menu of the podcast page (shared by the grid and the list view)
PullDownMenu {
    property bool gridMode: true
    signal toggleView()

    busy: core.updating || core.syncing

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
    // Search and feed address in one page (51); only for adding (99 -
    // "Suchen" in the other pulleys is the search on the device)
    MenuItem {
        text: qsTr("Search / add podcast")
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

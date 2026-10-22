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
    // Back in the pulley, second from the top (64; 51-63 in the
    // settings)
    MenuItem {
        text: qsTr("Import and export")
        onClicked: pageStack.push(Qt.resolvedUrl("ImportExportPage.qml"))
    }
    // Search and feed address in one page (51)
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
    MenuItem {
        visible: core.podcastCount > 0 || core.syncConfigured
        enabled: !core.updating && !core.syncing
        // Sync (if set up) and then all feeds - see appWindow.updateEverything
        text: qsTr("Update")
        onClicked: appWindow.updateEverything()
    }
}

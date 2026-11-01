// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// All subscriptions as a cover grid (with the number of unplayed episodes
// as a badge) or as a list - switchable in the pull-down menu, the choice
// is stored. Part of the ring navigation.
//
// Folders (45, like the favourite groups in Sailwave): one collapsible
// section per folder with its header right-aligned between the rows, the
// podcasts without a folder last under "Other podcasts". Without any folder
// there is one section without a header (as before 45).
//
// One flickable column with a Repeater per folder instead of a GridView/
// ListView: Qt 5.6 grids have no sections, and the context menus of folder
// headers and podcast rows must not sit in a list header (they grew
// upwards, see DESIGN). With a few dozen podcasts all delegates at once
// are no problem (covers come from the image cache).
Page {
    id: page
    objectName: "podcastsPage"
    property bool ringMember: false
    property bool gridMode: core.setting("ui.podcastGrid", "1") === "1"

    function toggleView() {
        gridMode = !gridMode
        core.setSetting("ui.podcastGrid", gridMode ? "1" : "0")
    }

    FolderListModel {
        id: folders
    }

    // (220) After the first subscription: since 231 one hint for the
    // podcast's context menu and folders (a separate menu hint would have
    // been a third one here); the hint of 52 on the folder header follows
    // once there is a folder
    function folderHint() {
        if (core.podcastCount > 0) {
            appWindow.showHint("podcastMenu", qsTr("Press and hold a podcast:\nsettings · move to folder\nNew folder: pull down"), page)
        }
    }
    Component.onCompleted: folderHint()
    Connections {
        target: core
        onDataChanged: page.folderHint()
    }

    SilicaFlickable {
        id: flickable
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        contentHeight: column.height + Theme.paddingLarge

        PodcastsMenu {
            gridMode: page.gridMode
            onToggleView: page.toggleView()
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Podcasts")
                description: core.updating
                             ? qsTr("Updating %1 %").arg(Math.round(core.updateProgress * 100))
                             : (core.syncing ? qsTr("Synchronizing") : "")
            }

            Repeater {
                model: folders
                FolderSection {
                    folderId: model.folderId
                    name: model.name
                    manualUpdate: model.manualUpdate
                    unplayed: model.unplayed
                    podcastCount: model.podcastCount
                    showHeader: folders.folderCount > 0
                    gridMode: page.gridMode
                    canMoveUp: model.folderId > 0 && index > 0
                    canMoveDown: model.folderId > 0 && index < folders.folderCount - 1
                }
            }
        }

        VerticalScrollDecorator {}
    }

    // (219) First steps as buttons instead of a hint to the pulley
    GetStarted {
        visible: core.podcastCount === 0
        anchors.verticalCenter: parent.verticalCenter
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0

// One folder on the Podcasts page (45, after the favourite groups of
// Sailwave): a right-aligned header (chevron, name, ⏸ for manual update,
// unplayed count) that collapses/expands the folder (stored as
// "ui.folderCollapsed.<id>"), then its podcasts as a 3-column grid or as
// list rows. Folder 0 is "Other podcasts" (podcasts without a folder);
// without any folder the header is hidden. Context menu on the header of a
// real folder: rename, move up/down, include in "Update", mark all as
// played, delete (podcasts stay / with podcasts).
Column {
    id: section

    property int folderId: 0
    property string name: ""
    property bool manualUpdate: false
    property int unplayed: 0
    property int podcastCount: 0
    property bool showHeader: true
    property bool gridMode: true
    property bool canMoveUp: false
    property bool canMoveDown: false

    property bool collapsed: showHeader && core.setting("ui.folderCollapsed." + folderId, "0") === "1"

    function toggle() {
        collapsed = !collapsed
        core.setSetting("ui.folderCollapsed." + folderId, collapsed ? "1" : "0")
    }

    width: parent ? parent.width : 0

    // First folder header: how folders work (52)
    function _hint() {
        if (showHeader && folderId > 0) {
            appWindow.showHint("folder", qsTr("Tap a folder name to fold it, press and hold for its options"), section)
        }
    }
    Component.onCompleted: _hint()
    onShowHeaderChanged: _hint()

    PodcastListModel {
        id: podcasts
        folderId: section.folderId
    }

    ListItem {
        id: header
        visible: section.showHeader
        width: parent.width
        contentHeight: Theme.itemSizeExtraSmall
        onClicked: section.toggle()
        menu: section.folderId > 0 ? folderMenu : null

        Row {
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.paddingSmall

            // New episodes in this folder (70): the glowing ball, small and
            // dim, left of the folder name
            NewDot {
                visible: podcasts.freshCount > 0
                anchors.verticalCenter: parent.verticalCenter
                size: Theme.iconSizeSmall
                opacity: 0.8
            }

            Image {
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.iconSizeExtraSmall
                height: width
                sourceSize.width: width
                sourceSize.height: height
                source: "image://theme/icon-m-right?" + Theme.highlightColor
                rotation: section.collapsed ? 0 : 90
                Behavior on rotation { NumberAnimation { duration: 150 } }
            }
            Label {
                anchors.verticalCenter: parent.verticalCenter
                text: section.name
                color: header.highlighted ? Theme.primaryColor : Theme.highlightColor
                truncationMode: TruncationMode.Fade
                width: Math.min(implicitWidth, header.width / 2)
            }
            Label {
                visible: section.manualUpdate
                anchors.verticalCenter: parent.verticalCenter
                text: "⏸"
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
            }
            Rectangle {
                visible: section.unplayed > 0
                anchors.verticalCenter: parent.verticalCenter
                height: countLabel.height + Theme.paddingSmall / 2
                width: Math.max(height, countLabel.width + Theme.paddingMedium)
                radius: height / 2
                color: "transparent"
                border.color: Theme.rgba(Theme.secondaryHighlightColor, 0.6)
                border.width: 1
                Label {
                    id: countLabel
                    anchors.centerIn: parent
                    text: section.unplayed > 9999 ? "9999+" : section.unplayed
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                }
            }
        }

        Component {
            id: folderMenu
            ContextMenu {
                MenuItem {
                    text: qsTr("Choose podcasts …")
                    onClicked: pageStack.push(Qt.resolvedUrl("pages/FolderPodcastsPage.qml"),
                                              { folderId: section.folderId, folderName: section.name })
                }
                MenuItem {
                    text: qsTr("Rename")
                    onClicked: appWindow.renameFolder(section.folderId, section.name)
                }
                MenuItem {
                    visible: section.canMoveUp
                    text: qsTr("Move up")
                    onClicked: core.moveFolder(section.folderId, -1)
                }
                MenuItem {
                    visible: section.canMoveDown
                    text: qsTr("Move down")
                    onClicked: core.moveFolder(section.folderId, 1)
                }
                MenuItem {
                    text: section.manualUpdate ? qsTr("Include in \"Update\"") : qsTr("Update only by hand")
                    onClicked: core.setFolderManualUpdate(section.folderId, !section.manualUpdate)
                }
                MenuItem {
                    visible: section.unplayed > 0
                    text: qsTr("Mark all as played")
                    onClicked: appWindow.remorseMarkFolderPlayed(section.folderId, section.name)
                }
                MenuItem {
                    text: qsTr("Delete folder …")
                    // Dialog and countdown live in the app window (not in
                    // this menu); the dialog asks about the podcasts (50)
                    onClicked: appWindow.askDeleteFolder(section.folderId, section.name, section.podcastCount)
                }
            }
        }
    }

    // Grid: 3 covers per row in portrait; in landscape (54) as many as fit
    // with about the portrait tile size
    readonly property int gridColumns: Math.max(3, Math.round(width / (Math.min(Screen.width, Screen.height) / 3)))

    // A Flow (65, as Sailwave's tiles): an open context menu makes its tile
    // taller and the others move down
    Flow {
        visible: section.gridMode && !section.collapsed
        width: parent.width
        Repeater {
            model: section.gridMode && !section.collapsed ? podcasts : null
            PodcastTile {
                width: section.width / section.gridColumns
                contentHeight: width
                podcastId: model.podcastId
                title: model.title
                coverUrl: model.coverUrl
                unplayed: model.unplayed
                manualUpdate: model.manualUpdate || section.manualUpdate
                lastError: model.lastError
                fresh: model.fresh
            }
        }
    }

    // List rows
    Column {
        visible: !section.gridMode && !section.collapsed
        width: parent.width
        Repeater {
            model: !section.gridMode && !section.collapsed ? podcasts : null
            PodcastRowItem {
                width: section.width
                podcastId: model.podcastId
                title: model.title
                coverUrl: model.coverUrl
                unplayed: model.unplayed
                episodeCount: model.episodeCount
                listenedSeconds: model.listenedSeconds
                downloadedCount: model.downloadedCount
                archived: model.archived
                manualUpdate: model.manualUpdate || section.manualUpdate
                lastError: model.lastError
                fresh: model.fresh
            }
        }
    }
}

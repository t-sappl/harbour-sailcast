// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Pickers 1.0
import Sailfish.Share 1.0
import "../"

// Import and export (replaces the test import of stage 4a):
//  - Import: OPML file, gpodder-sailfish database, Sailcast database
//    (replaces everything) - each through the Sailfish file picker
//  - Export: OPML file and a copy of the database into Downloads, or the
//    OPML file through the sharing menu
//  - Archive folders: podcast folders in the archive folder (episodes.json)
//    that can be taken over without network
// Writing goes to Downloads only (Sailjail permission "Downloads"), as
// Sailwave rejected a free folder picker for the same reason.
Page {
    id: page
    objectName: "importExportPage"

    property var archiveFolders: core.archiveFolders()
    // Folders (45): target of an OPML import (for podcasts the file puts in
    // no folder; 0 = "Other podcasts") and the folders of an OPML export
    // (empty = everything)
    property int importFolderId: 0
    property var exportFolders: []
    readonly property bool hasFolders: core.folders().length > 0

    function exportFoldersText() {
        if (exportFolders.length === 0) {
            return qsTr("Everything")
        }
        var names = []
        var list = core.folders()
        for (var i = 0; i < list.length; i++) {
            if (exportFolders.indexOf(list[i].folderId) >= 0) {
                names.push(list[i].name)
            }
        }
        if (exportFolders.indexOf(0) >= 0) {
            names.push(qsTr("Other podcasts"))
        }
        return names.join(", ")
    }

    // The picker closes itself after the choice; the action runs once this
    // page is active again (no push during an animation, as in Sailwave)
    property string pendingAction: ""
    property string pendingPath: ""

    onStatusChanged: {
        if (status === PageStatus.Active && pendingPath.length > 0) {
            var action = pendingAction
            var path = pendingPath
            pendingAction = ""
            pendingPath = ""
            run(action, path)
        }
    }

    function fileName(path) {
        return path.substring(path.lastIndexOf("/") + 1)
    }

    function pick(action, filters) {
        pendingAction = action
        pendingPath = ""
        pageStack.push(filePicker, { nameFilters: filters })
    }

    function run(action, path) {
        if (action === "opml") {
            core.importOpml(path, importFolderId)
        } else if (action === "gpodder") {
            core.importGpodder(path)
        } else if (action === "database") {
            remorse.execute(qsTr("Replacing the database"), function() {
                core.importDatabase(path)
                page.archiveFolders = core.archiveFolders()
            })
        }
    }

    Component {
        id: filePicker
        FilePickerPage {
            onSelectedContentPropertiesChanged: page.pendingPath = selectedContentProperties.filePath
        }
    }

    ShareAction {
        id: shareAction
        title: qsTr("Share subscriptions")
        mimeType: "application/xml"
    }

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Import and export")
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Import") }

            FolderComboBox {
                id: importFolderBox
                visible: page.hasFolders
                label: qsTr("Folder for imported podcasts")
                description: qsTr("For podcasts the file puts in no folder; folders named in the file are taken over")
                folderId: page.importFolderId
                onFolderIdChanged: page.importFolderId = folderId
            }

            ImportExportItem {
                title: qsTr("OPML file")
                description: qsTr("Adds the podcasts of a subscription list from another app")
                onClicked: page.pick("opml", ["*.opml", "*.xml"])
            }

            ImportExportItem {
                title: qsTr("gpodder-sailfish")
                description: qsTr("Subscriptions, played episodes and positions from the file \"Database\". Copy it to Downloads first: ~/.local/share/org.gpodder/gpodder_sailfish/Database")
                onClicked: page.pick("gpodder", ["Database", "*.db", "*.sqlite"])
            }

            ImportExportItem {
                title: qsTr("Sailcast database")
                description: qsTr("Replaces everything (podcasts, listening state, playlist, sync account) with a copy, e.g. a backup or the database of the desktop tool")
                onClicked: page.pick("database", ["*.db"])
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Export") }

            ValueButton {
                visible: page.hasFolders
                label: qsTr("Folders")
                value: page.exportFoldersText()
                onClicked: {
                    var selectPage = pageStack.push(Qt.resolvedUrl("FolderSelectPage.qml"), { selected: page.exportFolders })
                    selectPage.selectedChanged.connect(function() { page.exportFolders = selectPage.selected })
                }
            }

            ImportExportItem {
                title: qsTr("Save OPML file")
                description: qsTr("The subscriptions (with their folders), in Downloads. Private feed addresses with a key are included.")
                enabled: core.podcastCount > 0
                onClicked: {
                    var path = core.exportOpml(page.exportFolders)
                    if (path.length > 0) {
                        appWindow.showMessage(qsTr("Saved as %1").arg(page.fileName(path)))
                    }
                }
            }

            ImportExportItem {
                title: qsTr("Share OPML file")
                description: qsTr("Saves the file and opens the sharing menu (e-mail, Bluetooth, …)")
                enabled: core.podcastCount > 0
                onClicked: {
                    var path = core.exportOpml(page.exportFolders)
                    if (path.length > 0) {
                        shareAction.resources = [path]
                        shareAction.trigger()
                    }
                }
            }

            ImportExportItem {
                title: qsTr("Back up database")
                description: qsTr("A copy of the whole database in Downloads (no passwords)")
                onClicked: {
                    var path = core.exportDatabase()
                    if (path.length > 0) {
                        appWindow.showMessage(qsTr("Saved as %1").arg(page.fileName(path)))
                    }
                }
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Archive folders") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: page.archiveFolders.length > 0
                      ? qsTr("Takes over the episodes, files and listening state of an archived podcast - without network.")
                      : qsTr("No archived podcasts in %1").arg(core.archiveDirectory())
            }

            Item { width: 1; height: Theme.paddingSmall }

            Repeater {
                model: page.archiveFolders
                delegate: ImportExportItem {
                    title: modelData.title
                    description: qsTr("%n episodes", "", modelData.episodes)
                                 + (modelData.subscribed ? " · " + qsTr("subscribed") : "")
                    onClicked: {
                        core.importArchiveFolder(modelData.path)
                        page.archiveFolders = core.archiveFolders()
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}

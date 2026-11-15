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
    readonly property bool noFolders: exportFolders.length === 1 && exportFolders[0] === -1

    function exportFoldersText() {
        if (exportFolders.length === 0) {
            return qsTr("Everything")
        }
        // (248) None chosen: no export
        if (page.noFolders) {
            return qsTr("None")
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
        // (327) Back from the archive folders page: taken over ones changed
        if (status === PageStatus.Active) {
            archiveFolders = core.archiveFolders()
        }
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
            core.importOpml(path, importFolderId, markOlderSwitch.checked)
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

            // (331, Thomas: "zu unübersichtlich"; Gemini challenge) Three
            // sections Import / Export / Clean up and replace. The OPML
            // sentence moved into the "OPML file" row.
            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Import") }

            // (331, Gemini) The two OPML options before the action: the import
            // starts right after the file choice. They apply to OPML only, so
            // their texts say so (gpodder-sailfish below ignores them)
            FolderComboBox {
                id: importFolderBox
                visible: page.hasFolders
                label: qsTr("Folder for imported podcasts")
                description: qsTr("For an OPML file that names no folder; folders named in it are kept.")
                folderId: page.importFolderId
                onFolderIdChanged: page.importFolderId = folderId
            }

            // (168) Otherwise every episode of every imported podcast is
            // unplayed - many hundreds after a move from another app. Off by
            // default (174, Thomas)
            TextSwitch {
                id: markOlderSwitch
                text: qsTr("Only the newest episode unplayed")
                description: qsTr("After an OPML import, all older episodes are marked as played.")
                checked: core.setting("ui.importMarkOlder", "0") === "1"
                onCheckedChanged: core.setSetting("ui.importMarkOlder", checked ? "1" : "0")
            }

            ImportExportItem {
                title: qsTr("OPML file")
                description: qsTr("Adds the podcasts of a subscription list (OPML) from another app.")
                onClicked: page.pick("opml", ["*.opml", "*.xml"])
            }

            ImportExportItem {
                title: qsTr("gpodder-sailfish")
                description: qsTr("Subscriptions, played episodes and positions from its file \"Database\" – copy it to Downloads first (~/.local/share/org.gpodder/gpodder_sailfish/).")
                onClicked: page.pick("gpodder", ["Database", "*.db", "*.sqlite"])
            }

            // (327, Gemini) The archive folders on a page of their own: the list
            // (one row per podcast) pushed Export far down
            ImportExportItem {
                title: qsTr("Archive folders")
                // (328, Gemini) The count as a value next to the title, like
                // "Never delete for 0 of 83"
                value: page.archiveFolders.length > 0 ? qsTr("%n podcasts", "", page.archiveFolders.length) : ""
                description: page.archiveFolders.length > 0
                             ? qsTr("Takes over the episodes, files and listening state of an archived podcast – without network.")
                             : qsTr("No archived podcasts in %1").arg(core.archiveDirectory())
                enabled: page.archiveFolders.length > 0
                onClicked: pageStack.push(Qt.resolvedUrl("ArchiveFoldersPage.qml"))
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Export") }

            // (326, settings restructured) Actions as pills, not rows that look
            // like settings; "Back up database" is gone (Backup and restore:
            // "Back up now")
            ValueButton {
                visible: page.hasFolders
                // (328, Gemini) "OPML folders": only for the OPML file;
                // "Share my podcasts" has its own folder choice on its page (330)
                label: qsTr("OPML folders")
                value: page.exportFoldersText()
                onClicked: {
                    var selectPage = pageStack.push(Qt.resolvedUrl("FolderSelectPage.qml"), { selected: page.exportFolders })
                    selectPage.selectedChanged.connect(function() { page.exportFolders = selectPage.selected })
                }
                PageArrow {}
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                // (331) Without folders there is no choice above
                text: page.hasFolders
                      ? qsTr("The chosen folders as an OPML file in Downloads. Private feed addresses with a key are included.")
                      : qsTr("Your subscriptions as an OPML file in Downloads. Private feed addresses with a key are included.")
            }

            Item { width: 1; height: Theme.paddingSmall }

            Flow {
                property bool flow: true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                ActionPill {
                    text: qsTr("Save OPML file")
                    enabled: core.podcastCount > 0 && !page.noFolders
                    onClicked: {
                        var path = core.exportOpml(page.exportFolders)
                        if (path.length > 0) {
                            appWindow.showMessage(qsTr("Saved as %1").arg(page.fileName(path)))
                        }
                    }
                }
                ActionPill {
                    text: qsTr("Share OPML file")
                    enabled: core.podcastCount > 0 && !page.noFolders
                    onClicked: {
                        var path = core.exportOpml(page.exportFolders)
                        if (path.length > 0) {
                            shareAction.resources = [path]
                            shareAction.trigger()
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingMedium }

            // (294) Web page: own page (switches, chapter loading, progress);
            // (330) with its own folder choice there ("OPML folders" above is
            // for the OPML file only; 326-329 the page always got everything). (327, Gemini) A row with ›, as it opens a page
            ImportExportItem {
                title: qsTr("Share my podcasts")
                description: qsTr("An overview of your podcasts with covers, latest episodes and links – to send on or keep. Podcasts with a login are left out.")
                enabled: core.podcastCount > 0
                onClicked: pageStack.push(Qt.resolvedUrl("HtmlExportPage.qml"), { folders: [] })
            }

            // ---------------------------------------------------------
            // (331, Gemini) Rare and far-reaching: at the end of the page.
            // "Sailcast database" replaces everything (with a countdown and
            // before-restore.db)
            SectionHeader { text: qsTr("Clean up and replace") }

            // (326, settings restructured) From General: needed mostly after a
            // move from another app
            SettingsEntry {
                visible: core.podcastCount > 0
                title: qsTr("Mark podcasts as played")
                summary: qsTr("Of all or chosen podcasts – all episodes, or all but the newest of each")
                target: Qt.resolvedUrl("MarkAllPlayedDialog.qml")
            }

            ImportExportItem {
                title: qsTr("Sailcast database")
                description: qsTr("Replaces your entire Sailcast database (podcasts, listening state, playlist, sync account) with another file. Own backups: Backup and restore.")
                onClicked: page.pick("database", ["*.db"])
            }
        }

        VerticalScrollDecorator {}
    }
}

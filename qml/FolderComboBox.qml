// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Folder choice (45): "Other podcasts" (no folder), the folders and
// "New folder …" (asks for the name, creates the folder and selects it).
// folderId is the choice; the owner decides what to do with it.
// (333) A row that opens pages/FolderChoicePage.qml instead of a Silica
// ComboBox: with many folders the ComboBox showed its own page, which lay
// under the player bar. The name of the file and its properties stay, so the
// pages using it are unchanged. The wait for the closing list page (327) is
// no longer needed: the choice page is replaced by the name dialog.
ValueButton {
    id: box

    property int folderId: 0
    property var folderList: core.folders()

    function _openNameDialog() {
        var dialog = pageStack.replace(Qt.resolvedUrl("pages/FolderNameDialog.qml"))
        dialog.accepted.connect(function() {
            var id = core.createFolder(dialog.folderName)
            if (id > 0) {
                box.folderList = core.folders()
                box.folderId = id
            }
        })
    }

    label: qsTr("Folder")
    // (254, usability audit #4/jargon) "Other podcasts" on its own didn't
    // say that it just means no folder.
    description: qsTr("\"Other podcasts\": no folder")
    value: {
        for (var i = 0; i < folderList.length; i++) {
            if (folderList[i].folderId === folderId) {
                return folderList[i].name
            }
        }
        return qsTr("Other podcasts")
    }

    onClicked: {
        folderList = core.folders()
        var choice = pageStack.push(Qt.resolvedUrl("pages/FolderChoicePage.qml"),
                                    { title: box.label, folderId: box.folderId, folderList: box.folderList })
        choice.chosen.connect(function(id) { box.folderId = id })
        choice.newFolderWanted.connect(function() { box._openNameDialog() })
    }
}

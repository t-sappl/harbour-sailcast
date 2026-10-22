// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Folder choice (45): "Other podcasts" (no folder), the folders and
// "New folder …" (asks for the name, creates the folder and selects it).
// folderId is the choice; the owner decides what to do with it.
ComboBox {
    id: box

    property int folderId: 0
    property var folderList: core.folders()

    function indexFor(id) {
        for (var i = 0; i < folderList.length; i++) {
            if (folderList[i].folderId === id) {
                return i + 1
            }
        }
        return 0
    }

    function newFolder() {
        var dialog = pageStack.push(Qt.resolvedUrl("pages/FolderNameDialog.qml"))
        dialog.accepted.connect(function() {
            var id = core.createFolder(dialog.folderName)
            if (id > 0) {
                box.folderList = core.folders()
                box.folderId = id
            }
            box.restoreBinding()
        })
        dialog.rejected.connect(function() { box.restoreBinding() })
    }

    // Choosing "New folder …" moves the selection to that item; afterwards
    // the shown choice follows folderId again (the new folder, 48)
    function restoreBinding() {
        currentIndex = Qt.binding(function() { return box.indexFor(box.folderId) })
    }

    label: qsTr("Folder")
    currentIndex: indexFor(folderId)
    value: {
        for (var i = 0; i < folderList.length; i++) {
            if (folderList[i].folderId === folderId) {
                return folderList[i].name
            }
        }
        return qsTr("Other podcasts")
    }

    menu: ContextMenu {
        MenuItem {
            text: qsTr("Other podcasts")
            onClicked: box.folderId = 0
        }
        Repeater {
            model: box.folderList
            MenuItem {
                text: modelData.name
                onClicked: box.folderId = modelData.folderId
            }
        }
        MenuItem {
            text: qsTr("New folder …")
            onClicked: box.newFolder()
        }
    }
}

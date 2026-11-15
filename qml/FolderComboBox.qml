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

    // (327, Thomas: while subscribing, after a new folder the folder list
    // came again without it; "Cancel" showed the folder already chosen) With
    // many folders Silica shows the choice as a page of its own, which is
    // still being closed when "New folder …" is tapped. The name dialog was
    // pushed on top of it, and after saving the old list page came back.
    // Now the dialog waits until the page with this box is on top again.
    function ownerPage() {
        var p = box.parent
        while (p && !p.hasOwnProperty("backNavigation")) {
            p = p.parent
        }
        return p
    }

    property int _waits: 0
    Timer {
        id: newFolderTimer
        interval: 50
        onTriggered: {
            var owner = box.ownerPage()
            if ((pageStack.busy || (owner && pageStack.currentPage !== owner)) && box._waits < 60) {
                box._waits += 1
                restart()
                return
            }
            box._openNameDialog()
        }
    }

    function newFolder() {
        _waits = 0
        newFolderTimer.restart()
    }

    function _openNameDialog() {
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
    // (254, usability audit #4/jargon) "Other podcasts" on its own didn't
    // say that it just means no folder.
    description: qsTr("\"Other podcasts\": no folder")
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

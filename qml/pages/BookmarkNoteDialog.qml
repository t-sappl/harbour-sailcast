// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Name and note of a bookmark (47, name since 50). An empty name shows
// "Bookmark" in the lists; an empty note removes it.
Dialog {
    id: dialog
    objectName: "bookmarkNoteDialog"

    property alias bookmarkTitle: titleField.text
    property alias note: noteArea.text
    property string timeText: ""

    Column {
        width: parent.width

        DialogHeader {
            title: dialog.timeText.length > 0 ? qsTr("Bookmark at %1").arg(dialog.timeText) : qsTr("Bookmark")
            acceptText: qsTr("Save")
        }

        TextField {
            id: titleField
            width: parent.width
            focus: true
            label: qsTr("Name")
            placeholderText: qsTr("Bookmark")
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
            EnterKey.onClicked: noteArea.focus = true
        }

        TextArea {
            id: noteArea
            width: parent.width
            label: qsTr("Note")
            placeholderText: qsTr("e.g. book tip, recipe, good argument")
        }
    }
}

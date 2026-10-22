// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Name of a new or renamed folder (45). Notes that folders stay on this
// phone: the sync (GPodder API, Nextcloud GPodder Sync) has no folders.
Dialog {
    id: dialog
    objectName: "folderNameDialog"

    property string title: qsTr("New folder")
    property string initialName: ""
    property alias folderName: nameField.text

    canAccept: nameField.text.trim().length > 0

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            title: dialog.title
            acceptText: qsTr("Save")
        }

        TextField {
            id: nameField
            width: parent.width
            text: dialog.initialName
            focus: true
            label: qsTr("Folder name")
            placeholderText: qsTr("e.g. History")
            // Without word prediction: with it, the word being typed only
            // reaches "text" when it is committed, so "Save" stayed disabled
            // until tapping elsewhere (48, same fix as in Sailfeed)
            inputMethodHints: Qt.ImhNoPredictiveText
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.enabled: dialog.canAccept
            EnterKey.onClicked: dialog.accept()
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            text: qsTr("Folders are kept on this phone only: the sync with other podcast apps knows no folders. They are part of the database backup and of OPML exports.")
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Asks for the sync password (Nextcloud: an app password). It is kept in
// Sailfish Secrets, not in the app database.
Dialog {
    id: dialog
    objectName: "passwordDialog"
    property string account: ""
    property alias password: passwordField.text
    canAccept: passwordField.text.length > 0

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            acceptText: qsTr("Synchronize")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            text: qsTr("Password for %1").arg(dialog.account)
        }

        PasswordField {
            id: passwordField
            width: parent.width
            focus: true
            label: qsTr("App password")
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.enabled: dialog.canAccept
            EnterKey.onClicked: dialog.accept()
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: qsTr("Nextcloud: Settings → Security → Create new app password. Stored encrypted with Sailfish Secrets.")
        }
    }
}

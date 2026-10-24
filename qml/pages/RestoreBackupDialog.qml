// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Confirmation before a backup replaces the database (93): what is
// replaced, that the current state is kept as "before the last restore".
Dialog {
    id: dialog
    objectName: "restoreBackupDialog"

    property string sourceTitle: ""

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            acceptText: qsTr("Restore")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeLarge
            color: Theme.highlightColor
            text: dialog.sourceTitle
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            text: qsTr("Replaces everything in Sailcast on this phone with the state of the backup: subscriptions, listening state, playlist, folders, bookmarks and settings. Downloaded files stay; passwords are not part of a backup.")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
            text: qsTr("The current state is kept on this phone as \"Before the last restore\" - restoring that one undoes this step. The next sync brings listening states that are newer on the server.")
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Deleting a folder (50): one entry in the folder menu instead of two -
// the dialog asks whether its podcasts are unsubscribed too (default: no,
// they move to "Other podcasts"). The countdown follows in the app window.
Dialog {
    id: dialog
    objectName: "folderDeleteDialog"

    property string folderName: ""
    property int podcastCount: 0
    property alias withPodcasts: podcastSwitch.checked

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            title: qsTr("Delete folder %1?").arg(dialog.folderName)
            acceptText: qsTr("Delete")
        }

        TextSwitch {
            id: podcastSwitch
            visible: dialog.podcastCount > 0
            text: qsTr("Also unsubscribe its %n podcasts", "", dialog.podcastCount)
            description: checked ? qsTr("The podcasts, their episodes, downloads and bookmarks are removed.")
                                 : qsTr("The podcasts are kept and move to \"Other podcasts\".")
        }
    }
}

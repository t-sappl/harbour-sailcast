// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../TimeFormat.js" as TimeFormat

// Deleting a folder (50): one entry in the folder menu instead of two -
// the dialog asks whether its podcasts are unsubscribed too (default: no,
// they move to "Other podcasts"). The countdown follows in the app window.
Dialog {
    id: dialog
    objectName: "folderDeleteDialog"

    property string folderName: ""
    property int podcastCount: 0
    property int folderId: 0

    // (366, forum 19 Nov) What unsubscribing them would delete
    readonly property var downloads: {
        var count = 0
        var bytes = 0
        var ids = folderId > 0 ? core.podcastIdsInFolder(folderId) : []
        for (var i = 0; i < ids.length; i++) {
            var d = core.deletableDownloads(ids[i])
            count += d.count || 0
            bytes += d.bytes || 0
        }
        return { count: count, bytes: bytes }
    }
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
            description: !checked ? qsTr("The podcasts are kept and move to \"Other podcasts\".")
                       : dialog.downloads.count > 0
                         ? qsTr("The podcasts, their episodes and bookmarks are removed, and %n downloaded episodes (%1) are deleted. To keep downloads, unsubscribe a podcast on its own first – there they can be moved to the archive.", "", dialog.downloads.count)
                           .arg(TimeFormat.size(dialog.downloads.bytes, qsTr("%1 MB"), qsTr("%1 GB")))
                         : qsTr("The podcasts, their episodes, downloads and bookmarks are removed.")
        }
    }
}

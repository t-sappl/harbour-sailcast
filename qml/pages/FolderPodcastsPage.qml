// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// Choose the podcasts of a folder at once (46; a dialog with "Accept"
// since 50 - tapping alone did not look like saving): every podcast with a
// switch, on = in this folder. Accepting moves the switched-on podcasts
// here (out of their previous folder - one folder per podcast) and the
// switched-off former members to "Other podcasts".
// Order (57): the podcasts already in the folder on top, then the
// rest; for a new (empty) folder the podcasts without a folder on top -
// both groups alphabetical.
Dialog {
    id: page
    objectName: "folderPodcastsPage"

    property int folderId: 0
    property string folderName: ""
    // podcastId -> true/false where the user changed something
    property var changes: ({})

    function isChecked(podcastId, currentFolder) {
        var key = "" + podcastId
        return changes.hasOwnProperty(key) ? changes[key] : currentFolder === folderId
    }

    function toggle(podcastId, currentFolder) {
        var copy = {}
        for (var k in changes) {
            copy[k] = changes[k]
        }
        copy["" + podcastId] = !isChecked(podcastId, currentFolder)
        changes = copy
    }

    onAccepted: {
        for (var k in changes) {
            var id = parseInt(k)
            var podcast = core.podcast(id)
            var inside = (podcast.folderId || 0) === folderId
            if (changes[k] && !inside) {
                core.setPodcastFolder(id, folderId)
            } else if (!changes[k] && inside) {
                core.setPodcastFolder(id, 0)
            }
        }
    }

    // Folder names for the hint "now in …"
    property var folderNames: {
        var names = {}
        var list = core.folders()
        for (var i = 0; i < list.length; i++) {
            names[list[i].folderId] = list[i].name
        }
        return names
    }

    PodcastListModel {
        id: podcasts
        // Evaluated when the folder is known; not re-sorted while switching
        firstFolderId: page.folderId > 0 && core.podcastIdsInFolder(page.folderId).length > 0 ? page.folderId : 0
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        // Only once the folder is known (a new folder gets its id after the
        // name dialog, see appWindow.newFolder)
        model: page.folderId > 0 ? podcasts : null

        header: DialogHeader {
            title: page.folderName
            acceptText: qsTr("Save")
        }

        delegate: TextSwitch {
            width: list.width
            text: model.title
            description: model.folderId > 0 && model.folderId !== page.folderId
                         ? qsTr("now in %1").arg(page.folderNames[model.folderId] || "") : ""
            checked: page.isChecked(model.podcastId, model.folderId)
            automaticCheck: false
            onClicked: page.toggle(model.podcastId, model.folderId)
        }

        VerticalScrollDecorator {}
    }
}

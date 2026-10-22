// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Which folders go into the OPML export (45). Nothing chosen = everything.
// selected: list of folder ids (0 = "Other podcasts").
Page {
    id: page
    objectName: "folderSelectPage"

    property var selected: []
    property var folderList: core.folders()

    function isSelected(id) {
        return selected.indexOf(id) >= 0
    }

    function toggle(id) {
        var list = selected.slice(0)
        var i = list.indexOf(id)
        if (i >= 0) {
            list.splice(i, 1)
        } else {
            list.push(id)
        }
        selected = list
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("Export everything")
                onClicked: page.selected = []
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Folders to export")
                description: page.selected.length === 0 ? qsTr("Nothing chosen: everything is exported") : ""
            }

            Repeater {
                model: page.folderList
                TextSwitch {
                    text: modelData.name
                    description: qsTr("%n podcasts", "", modelData.podcastCount)
                    checked: page.isSelected(modelData.folderId)
                    automaticCheck: false
                    onClicked: page.toggle(modelData.folderId)
                }
            }

            TextSwitch {
                text: qsTr("Other podcasts")
                description: qsTr("Podcasts without a folder")
                checked: page.isSelected(0)
                automaticCheck: false
                onClicked: page.toggle(0)
            }
        }

        VerticalScrollDecorator {}
    }
}

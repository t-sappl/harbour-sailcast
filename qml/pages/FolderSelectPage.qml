// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Which folders go into the OPML export (45). selected: list of folder ids
// (0 = "Other podcasts"); [] = everything (also folders made later),
// [-1] = nothing (export not possible).
// (248) The switches show what is exported: all on for "everything" (until
// 247 all were off then, with "All" lit - contradictory), chips "All" /
// "None" as in the other choice lists.
Page {
    id: page
    objectName: "folderSelectPage"

    property var selected: []
    property var folderList: core.folders()
    readonly property var allIds: {
        var ids = []
        for (var i = 0; i < folderList.length; i++) {
            ids.push(folderList[i].folderId)
        }
        ids.push(0)
        return ids
    }
    // The folders switched on
    property var chosen: []
    Component.onCompleted: {
        chosen = selected.length === 0 ? allIds.slice(0)
               : (selected.length === 1 && selected[0] === -1 ? [] : selected.slice(0))
    }

    function isSelected(id) {
        return chosen.indexOf(id) >= 0
    }

    function apply(list) {
        chosen = list
        selected = list.length === allIds.length ? [] : (list.length === 0 ? [-1] : list)
    }

    function toggle(id) {
        var list = chosen.slice(0)
        var i = list.indexOf(id)
        if (i >= 0) {
            list.splice(i, 1)
        } else {
            list.push(id)
        }
        apply(list)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Folders to export")
                description: page.chosen.length === 0 ? qsTr("Choose at least one folder") : ""
            }

            // (246/248) Chips instead of the pulley "Export everything"
            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: page.chosen.length === page.allIds.length
                    onClicked: page.apply(page.allIds.slice(0))
                }
                Chip {
                    text: qsTr("None")
                    on: page.chosen.length === 0
                    onClicked: page.apply([])
                }
            }
            Item { width: 1; height: Theme.paddingMedium }

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

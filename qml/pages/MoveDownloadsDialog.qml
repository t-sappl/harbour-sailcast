// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// (217) After the download location changed (globally, per podcast or in
// the lists of 212): move the existing downloads to the new place - all
// podcasts or chosen ones, each with the number and size of its downloads,
// the selected total below. Kept files ("Save in Music"), archived
// podcasts and the episode in the player stay where they are.
Dialog {
    id: page
    objectName: "moveDownloadsDialog"

    // > 0: only this podcast (from its settings)
    property int podcastId: 0
    property var entries: core.misplacedDownloads(podcastId)
    // podcastId -> false where the user switched a podcast off (all on)
    property var off: ({})

    function isChecked(id) {
        return !off.hasOwnProperty("" + id)
    }

    function toggle(id) {
        var copy = {}
        for (var k in off) {
            copy[k] = true
        }
        if (isChecked(id)) {
            copy["" + id] = true
        } else {
            delete copy["" + id]
        }
        off = copy
    }

    readonly property var selected: {
        var count = 0
        var bytes = 0
        var ids = []
        for (var i = 0; i < entries.length; i++) {
            if (isChecked(entries[i].podcastId)) {
                count += entries[i].count
                bytes += entries[i].bytes
                ids.push(entries[i].podcastId)
            }
        }
        return { count: count, bytes: bytes, ids: ids }
    }

    function sizeText(bytes) {
        return TimeFormat.size(bytes, qsTr("%1 MB"), qsTr("%1 GB"))
    }

    canAccept: selected.count > 0
    onAccepted: core.moveDownloads(selected.ids)

    SilicaListView {
        id: list
        currentIndex: -1
        anchors.fill: parent
        model: page.entries

        header: Column {
            width: list.width

            DialogHeader {
                title: qsTr("Move downloads")
                acceptText: qsTr("Move")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("The download location changed. These downloads are still in the old place – move them now? New downloads go to the new place anyway.")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        delegate: TextSwitch {
            width: list.width
            text: modelData.title
            description: qsTr("%n downloads", "", modelData.count) + " · " + page.sizeText(modelData.bytes)
                         + " → " + (modelData.toMusic ? qsTr("Music folder") : qsTr("phone"))
            checked: page.isChecked(modelData.podcastId)
            automaticCheck: false
            onClicked: page.toggle(modelData.podcastId)
        }

        footer: Column {
            width: list.width

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("Selected: %n downloads", "", page.selected.count) + " · " + page.sizeText(page.selected.bytes)
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Episodes kept with \"Save in Music\", archived podcasts and the episode playing stay where they are.")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        ViewPlaceholder {
            enabled: page.entries.length === 0
            text: qsTr("All downloads are in place")
        }

        VerticalScrollDecorator {}
    }
}

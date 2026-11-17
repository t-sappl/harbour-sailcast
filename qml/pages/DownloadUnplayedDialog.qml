// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// (274) Downloads page: download the unplayed episodes of several podcasts at
// once (forum; per podcast "Download episodes" exists). Built as "Mark
// podcasts as played": the podcasts with something to download, each with a
// switch, its number and size; chips for how many per podcast (newest 1 / 3 /
// all unplayed) and "All" / "None"; the total with the free space below.
// Nothing is selected at first and only the newest episode of each podcast is
// offered - after an import hundreds of old unplayed episodes would be
// thousands of downloads. Ended podcasts are left out. Without Wi-Fi ("only
// on Wi-Fi") the user is asked after accepting, with the size.
Dialog {
    // (338) No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "downloadUnplayedDialog"

    // 1, 3 or 0 = all unplayed
    property int newest: 1
    property var entries: core.unplayedToDownload(newest)
    // podcastId -> true where the user switched a podcast on
    property var chosen: ({})
    readonly property real freeBytes: core.downloadStats().free || -1

    function isChecked(id) {
        return chosen.hasOwnProperty("" + id)
    }

    function toggle(id) {
        var copy = {}
        for (var k in chosen) {
            copy[k] = true
        }
        if (isChecked(id)) {
            delete copy["" + id]
        } else {
            copy["" + id] = true
        }
        chosen = copy
    }

    function selectAll(all) {
        var copy = {}
        if (all) {
            for (var i = 0; i < entries.length; i++) {
                copy["" + entries[i].podcastId] = true
            }
        }
        chosen = copy
    }

    // "1 episode" also without plural forms
    function countText(n) {
        return n === 1 ? qsTr("1 episode") : qsTr("%1 episodes").arg(n)
    }

    function sizeText(bytes) {
        return TimeFormat.size(bytes, qsTr("%1 MB"), qsTr("%1 GB"))
    }

    readonly property var selected: {
        var count = 0
        var bytes = 0
        var ids = []
        var podcasts = 0
        for (var i = 0; i < entries.length; i++) {
            if (isChecked(entries[i].podcastId)) {
                podcasts++
                count += entries[i].count
                bytes += entries[i].bytes
                for (var j = 0; j < entries[i].ids.length; j++) {
                    ids.push(entries[i].ids[j])
                }
            }
        }
        return { count: count, bytes: bytes, ids: ids, podcasts: podcasts }
    }

    // A new choice of the number: the offered episodes change, the switches
    // stay where they are
    canAccept: selected.count > 0
    onAccepted: {
        var ids = selected.ids
        var bytes = selected.bytes
        appWindow.requestEpisodesDownload(ids, bytes)
    }

    SilicaListView {
        id: list
        currentIndex: -1
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        model: page.entries

        header: Column {
            width: list.width

            DialogHeader {
                title: qsTr("Download unplayed")
                acceptText: qsTr("Download")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("Downloads the unplayed episodes of the chosen podcasts that are not on the phone yet. Ended podcasts are left out. \"Newest\" counts the ones that are not on the phone yet. The downloads can be cancelled on the downloads page.")
            }

            Item { width: 1; height: Theme.paddingMedium }

            // How many per podcast
            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("Newest 1")
                    on: page.newest === 1
                    onClicked: page.newest = 1
                }
                Chip {
                    text: qsTr("Newest 3")
                    on: page.newest === 3
                    onClicked: page.newest = 3
                }
                Chip {
                    text: qsTr("All unplayed")
                    on: page.newest === 0
                    onClicked: page.newest = 0
                }
            }

            Item { width: 1; height: Theme.paddingSmall }

            // Which podcasts
            Flow {
                visible: page.entries.length > 1
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: page.selected.podcasts === page.entries.length
                    onClicked: page.selectAll(true)
                }
                Chip {
                    text: qsTr("None")
                    on: page.selected.podcasts === 0
                    onClicked: page.selectAll(false)
                }
            }

            Item { width: 1; height: Theme.paddingMedium }
        }

        delegate: TextSwitch {
            width: list.width
            text: modelData.title
            description: page.countText(modelData.count)
                         + (modelData.bytes > 0 ? " · " + page.sizeText(modelData.bytes) : "")
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
                text: {
                    var text = page.countText(page.selected.count)
                    if (page.selected.bytes > 0) {
                        text += " · " + qsTr("about %1").arg(page.sizeText(page.selected.bytes))
                    }
                    if (page.freeBytes >= 0) {
                        text += ", " + qsTr("free %1").arg(page.sizeText(page.freeBytes))
                    }
                    return text
                }
            }

            Label {
                visible: !core.downloadAllowed() && page.entries.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("No Wi-Fi connection – you are asked before the episodes load over the mobile connection.")
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        ViewPlaceholder {
            enabled: page.entries.length === 0
            text: qsTr("Nothing to download")
            hintText: qsTr("All unplayed episodes are on the phone already")
        }

        VerticalScrollDecorator {}
    }
}

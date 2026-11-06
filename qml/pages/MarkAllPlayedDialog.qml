// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// (246) Settings → General: mark the episodes of all podcasts as played
// (forum, carepack) - all, or all but the newest of each podcast (as the
// OPML import option). Archived podcasts included; synchronized as usual.
// (248) Built as "Move downloads": the podcasts with something to mark,
// each with a switch and its number, chips "All" / "None", the total below
// (246 showed only the chips on an empty page).
Dialog {
    id: page
    objectName: "markAllPlayedDialog"

    property bool keepNewest: true
    property var entries: core.markablePodcasts(keepNewest)
    // podcastId -> true where the user switched a podcast off (all on)
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

    function selectAll(on) {
        var copy = {}
        if (!on) {
            for (var i = 0; i < entries.length; i++) {
                copy["" + entries[i].podcastId] = true
            }
        }
        off = copy
    }

    // "1 episode" also without plural forms
    function countText(n) {
        return n === 1 ? qsTr("1 episode") : qsTr("%1 episodes").arg(n)
    }

    readonly property var selected: {
        var count = 0
        var ids = []
        for (var i = 0; i < entries.length; i++) {
            if (isChecked(entries[i].podcastId)) {
                count += entries[i].count
                ids.push(entries[i].podcastId)
            }
        }
        return { count: count, ids: ids }
    }

    canAccept: selected.count > 0
    onAccepted: core.markEverythingPlayed(keepNewest, selected.ids)

    SilicaListView {
        id: list
        currentIndex: -1
        anchors.fill: parent
        model: page.entries

        header: Column {
            width: list.width

            DialogHeader {
                title: qsTr("Mark as played")
                acceptText: qsTr("Mark")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("Marks the unplayed episodes of the chosen podcasts as played – episodes in progress too. The change goes to the sync server as usual.")
            }

            Item { width: 1; height: Theme.paddingMedium }

            // What to mark
            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All but the newest per podcast")
                    on: page.keepNewest
                    onClicked: page.keepNewest = true
                }
                Chip {
                    text: qsTr("All episodes")
                    on: !page.keepNewest
                    onClicked: page.keepNewest = false
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
                    on: page.selected.ids.length === page.entries.length
                    onClicked: page.selectAll(true)
                }
                Chip {
                    text: qsTr("None")
                    on: page.selected.ids.length === 0
                    onClicked: page.selectAll(false)
                }
            }

            Item { width: 1; height: Theme.paddingMedium }
        }

        delegate: TextSwitch {
            width: list.width
            text: modelData.title
            description: page.countText(modelData.count)
                         + (modelData.archived ? " · " + qsTr("ended") : "")
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
                text: qsTr("Selected: %1").arg(page.countText(page.selected.count))
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        ViewPlaceholder {
            enabled: page.entries.length === 0
            text: page.keepNewest ? qsTr("Only the newest episodes are unplayed") : qsTr("Everything is played")
        }

        VerticalScrollDecorator {}
    }
}

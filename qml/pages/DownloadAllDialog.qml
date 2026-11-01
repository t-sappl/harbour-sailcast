// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// "Folgen herunterladen" (podcast settings; 143 all at once, since 159 with
// a choice): the episodes not downloaded yet - all or only the unplayed
// ones - grouped by season (when the feed has two or more) or by month /
// year; a tap on a group header selects or clears the whole group, a tap on
// an episode only that one. At first everything shown is selected. Size as
// declared in the feed (often incomplete). Without Wi-Fi ("only on Wi-Fi")
// a note, as in the archive dialog; accepting loads them anyway.
Dialog {
    id: dialog
    objectName: "downloadAllDialog"
    property int podcastId: 0
    property string podcastTitle: ""

    readonly property var candidates: core.downloadCandidates(podcastId)
    readonly property int unplayedCount: countUnplayed()
    property bool unplayedOnly: unplayedCount > 0 && unplayedCount < candidates.length
    readonly property var shown: shownOf(candidates, unplayedOnly)
    // Episode id -> true
    property var selected: ({})

    function countUnplayed() {
        var n = 0
        for (var i = 0; i < candidates.length; i++) {
            if (!candidates[i].played) {
                n++
            }
        }
        return n
    }

    function shownOf(list, onlyUnplayed) {
        var result = []
        for (var i = 0; i < list.length; i++) {
            if (!onlyUnplayed || !list[i].played) {
                result.push(list[i])
            }
        }
        return result
    }

    function selectAll(on) {
        var map = {}
        for (var i = 0; on && i < shown.length; i++) {
            map[shown[i].id] = true
        }
        selected = map
    }

    function toggle(id) {
        var map = {}
        for (var key in selected) {
            map[key] = selected[key]
        }
        map[id] = !map[id]
        selected = map
    }

    // A whole group: all selected -> none, else all
    function groupState(section) {
        var all = true
        var any = false
        for (var i = 0; i < shown.length; i++) {
            if (shown[i].section === section) {
                if (selected[shown[i].id]) {
                    any = true
                } else {
                    all = false
                }
            }
        }
        return all ? 2 : (any ? 1 : 0)
    }

    function toggleGroup(section) {
        var on = groupState(section) !== 2
        var map = {}
        for (var key in selected) {
            map[key] = selected[key]
        }
        for (var i = 0; i < shown.length; i++) {
            if (shown[i].section === section) {
                map[shown[i].id] = on
            }
        }
        selected = map
    }

    readonly property var chosen: chosenOf(shown, selected)
    function chosenOf(list, map) {
        var ids = []
        for (var i = 0; i < list.length; i++) {
            if (map[list[i].id]) {
                ids.push(list[i].id)
            }
        }
        return ids
    }
    readonly property real chosenBytes: {
        var bytes = 0
        for (var i = 0; i < shown.length; i++) {
            if (selected[shown[i].id]) {
                bytes += shown[i].size
            }
        }
        return bytes
    }

    onUnplayedOnlyChanged: selectAll(true)
    Component.onCompleted: selectAll(true)

    canAccept: chosen.length > 0
    onAccepted: core.downloadEpisodes(chosen)

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        quickScroll: true
        model: dialog.shown
        // (176) Opens at the top with the dialog header: no current item
        // (ListView keeps item 0 in view while the header grows), the top
        // kept until the user moves the list - as FolderPickerDialog (66)
        currentIndex: -1
        property bool _keepTop: true
        onMovementStarted: _keepTop = false
        onContentHeightChanged: if (_keepTop) positionViewAtBeginning()
        onOriginYChanged: if (_keepTop) positionViewAtBeginning()
        Component.onCompleted: positionViewAtBeginning()

        header: Column {
            width: list.width
            spacing: Theme.paddingSmall

            DialogHeader {
                title: qsTr("Download episodes")
                acceptText: qsTr("Download")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: dialog.podcastTitle
            }

            // All or only the unplayed ones; select all / none
            Flow {
                visible: dialog.candidates.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("Only unplayed")
                    on: dialog.unplayedOnly
                    onClicked: dialog.unplayedOnly = true
                }
                Chip {
                    text: qsTr("All")
                    on: !dialog.unplayedOnly
                    onClicked: dialog.unplayedOnly = false
                }
                Chip {
                    text: dialog.chosen.length === dialog.shown.length ? qsTr("Select none") : qsTr("Select all")
                    onClicked: dialog.selectAll(dialog.chosen.length !== dialog.shown.length)
                }
            }

            Label {
                visible: dialog.candidates.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: {
                    var text = qsTr("%1 of %2 selected").arg(dialog.chosen.length).arg(dialog.shown.length)
                    if (dialog.chosenBytes > 0) {
                        text += " · " + qsTr("about %1").arg(TimeFormat.size(dialog.chosenBytes, qsTr("%1 MB"), qsTr("%1 GB")))
                    }
                    return text
                }
            }

            Label {
                visible: dialog.candidates.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Tap a group to select or clear all its episodes. Episodes already downloaded are not listed. The downloads are queued, a few load at a time; they can be cancelled on the downloads page.")
            }

            Label {
                visible: !core.downloadAllowed() && dialog.candidates.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("No Wi-Fi connection - the episodes would be loaded over the mobile connection.")
            }
            Item { width: 1; height: Theme.paddingSmall }
        }

        section.property: "section"
        // The group: tap selects or clears all its episodes
        section.delegate: BackgroundItem {
            id: groupItem
            width: list.width
            height: Theme.itemSizeSmall
            readonly property int state_: dialog.groupState(section)
            onClicked: dialog.toggleGroup(section)
            Label {
                anchors.right: groupMark.left
                anchors.rightMargin: Theme.paddingMedium
                anchors.verticalCenter: parent.verticalCenter
                text: section
                color: Theme.highlightColor
                font.pixelSize: Theme.fontSizeSmall
            }
            Label {
                id: groupMark
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: groupItem.state_ === 2 ? qsTr("all") : (groupItem.state_ === 1 ? qsTr("some") : qsTr("none"))
            }
        }

        delegate: TextSwitch {
            width: list.width
            automaticCheck: false
            checked: dialog.selected[modelData.id] === true
            text: modelData.title
            description: {
                var parts = []
                if (modelData.published > 0) {
                    parts.push(Qt.formatDate(new Date(modelData.published), Qt.DefaultLocaleShortDate))
                }
                if (modelData.duration > 0) {
                    parts.push(TimeFormat.length(modelData.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                }
                if (modelData.size > 0) {
                    parts.push(TimeFormat.size(modelData.size, qsTr("%1 MB"), qsTr("%1 GB")))
                }
                if (modelData.played) {
                    parts.push(qsTr("played"))
                }
                return parts.join(" · ")
            }
            onClicked: dialog.toggle(modelData.id)
        }

        ViewPlaceholder {
            enabled: dialog.candidates.length === 0
            text: qsTr("All episodes are downloaded already.")
        }

        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// "Mark as played …" (167, podcast page pulley; replaces "Mark all as
// played"): the unplayed episodes of the podcast, grouped like the podcast
// page (season or month / year). Nothing is selected at first; chips select
// all, all but the newest, or none; a tap on a group header selects or
// clears the group, a tap on an episode only that one, a long press that
// episode and all older ones. Accepting marks the selection as played.
// (170) Second mode "as unplayed": the played episodes; press and hold
// selects an episode and all newer ones (heard again from there).
Dialog {
    id: dialog
    objectName: "markPlayedDialog"
    property int podcastId: 0
    property string podcastTitle: ""
    // (170) false: unplayed ones -> played; true: played ones -> unplayed
    property bool unplayMode: false

    readonly property var shown: core.unplayedCandidates(podcastId, unplayMode)
    onUnplayModeChanged: selected = ({})
    // Episode id -> true
    property var selected: ({})

    function copySelected() {
        var map = {}
        for (var key in selected) {
            map[key] = selected[key]
        }
        return map
    }

    function selectAll(on) {
        var map = {}
        for (var i = 0; on && i < shown.length; i++) {
            map[shown[i].id] = true
        }
        selected = map
    }

    // Everything except the newest episode
    function selectAllButNewest() {
        var newest = -1
        var newestTime = -1
        for (var i = 0; i < shown.length; i++) {
            if (shown[i].published > newestTime) {
                newestTime = shown[i].published
                newest = shown[i].id
            }
        }
        var map = {}
        for (var j = 0; j < shown.length; j++) {
            if (shown[j].id !== newest) {
                map[shown[j].id] = true
            }
        }
        selected = map
    }

    // This episode and all older ones (unplay mode: all newer ones)
    function selectOlder(entry) {
        var map = copySelected()
        for (var i = 0; i < shown.length; i++) {
            if (unplayMode ? shown[i].published >= entry.published : shown[i].published <= entry.published) {
                map[shown[i].id] = true
            }
        }
        selected = map
    }

    function toggle(id) {
        var map = copySelected()
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
        var map = copySelected()
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

    canAccept: chosen.length > 0
    onAccepted: unplayMode ? core.markEpisodesUnplayed(chosen) : core.markEpisodesPlayed(chosen)

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
                title: dialog.unplayMode ? qsTr("Mark as unplayed") : qsTr("Mark as played")
                acceptText: dialog.unplayMode ? qsTr("Mark unplayed") : qsTr("Mark played")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: dialog.podcastTitle
            }

            // (170) What to mark
            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("Unplayed ones")
                    on: !dialog.unplayMode
                    onClicked: dialog.unplayMode = false
                }
                Chip {
                    text: qsTr("Played ones")
                    on: dialog.unplayMode
                    onClicked: dialog.unplayMode = true
                }
            }

            Flow {
                visible: dialog.shown.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: dialog.shown.length > 0 && dialog.chosen.length === dialog.shown.length
                    onClicked: dialog.selectAll(true)
                }
                Chip {
                    visible: dialog.shown.length > 1 && !dialog.unplayMode
                    text: qsTr("All but the newest")
                    on: dialog.shown.length > 1 && dialog.chosen.length === dialog.shown.length - 1
                    onClicked: dialog.selectAllButNewest()
                }
                Chip {
                    text: qsTr("None")
                    on: dialog.chosen.length === 0
                    onClicked: dialog.selectAll(false)
                }
            }

            Label {
                visible: dialog.shown.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("%1 of %2 selected").arg(dialog.chosen.length).arg(dialog.shown.length)
            }

            Label {
                visible: dialog.shown.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: dialog.unplayMode
                      ? qsTr("Tap an episode to select it, press and hold to select it and all newer ones. Tap a group to select or clear all its episodes.")
                      : qsTr("Tap an episode to select it, press and hold to select it and all older ones. Tap a group to select or clear all its episodes.")
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
                if (modelData.inProgress) {
                    parts.push(qsTr("started"))
                }
                return parts.join(" · ")
            }
            onClicked: dialog.toggle(modelData.id)
            onPressAndHold: dialog.selectOlder(modelData)
        }

        ViewPlaceholder {
            enabled: dialog.shown.length === 0
            text: dialog.unplayMode ? qsTr("No episode is played yet.") : qsTr("All episodes are played already.")
        }

        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// (212) One per-podcast setting for many podcasts at once, from the
// settings page (like "Podcasts in the folder", 46): every podcast with a
// switch showing its effective state. Accepting stores only the changed
// ones: a state equal to the global one becomes empty (= "As set
// globally"), otherwise onValue / offValue. Used for the download location
// ("podcast.<id>.inMusic") and "never delete" ("podcast.<id>.cleanupDays").
// (213) Optional groups: groupIds first under firstTitle, the rest under
// restTitle (never delete: those in the Music folder on top).
Dialog {
    id: page
    objectName: "podcastChoiceDialog"

    property string title: ""
    property string hint: ""
    // podcast.<id>.<settingName>
    property string settingName: ""
    property string onValue: "1"
    // Stored when switched off while the global choice is on
    property string offValue: "0"
    property bool globalOn: false
    // (213) Podcasts shown first, with section headers; empty = no groups
    property var groupIds: []
    property string firstTitle: ""
    property string restTitle: ""
    readonly property bool grouped: firstTitle.length > 0
    // podcastId -> true/false where the user changed something
    property var changes: ({})

    function key(podcastId) {
        return "podcast." + podcastId + "." + settingName
    }

    function storedOn(podcastId) {
        var value = core.setting(key(podcastId), "")
        return value === onValue || (value === "" && globalOn)
    }

    function isChecked(podcastId) {
        var k = "" + podcastId
        return changes.hasOwnProperty(k) ? changes[k] : storedOn(podcastId)
    }

    function toggle(podcastId) {
        var copy = {}
        for (var k in changes) {
            copy[k] = changes[k]
        }
        copy["" + podcastId] = !isChecked(podcastId)
        changes = copy
    }

    // (246) Chips "All" / "None"
    function setAll(on) {
        var ids = podcasts.ids()
        var copy = {}
        for (var i = 0; i < ids.length; i++) {
            copy["" + ids[i]] = on
        }
        changes = copy
    }
    readonly property int checkedCount: {
        var ids = podcasts.count >= 0 ? podcasts.ids() : []
        var n = 0
        for (var i = 0; i < ids.length; i++) {
            if (isChecked(ids[i])) {
                n++
            }
        }
        return n
    }

    onAccepted: {
        for (var k in changes) {
            var id = parseInt(k)
            if (changes[k] === storedOn(id)) {
                continue
            }
            if (changes[k] === globalOn) {
                core.setSetting(key(id), "")
            } else {
                core.setSetting(key(id), changes[k] ? onValue : offValue)
            }
        }
    }

    PodcastListModel {
        id: podcasts
        firstIds: page.groupIds
    }

    SilicaListView {
        id: list
        // Opens at the top (as FolderPodcastsPage)
        currentIndex: -1
        property bool _keepTop: true
        onMovementStarted: _keepTop = false
        onContentHeightChanged: if (_keepTop) positionViewAtBeginning()
        onOriginYChanged: if (_keepTop) positionViewAtBeginning()
        Component.onCompleted: positionViewAtBeginning()
        anchors.fill: parent
        model: podcasts

        header: Column {
            width: list.width

            DialogHeader {
                title: page.title
                acceptText: qsTr("Save")
            }

            Label {
                visible: page.hint.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: page.hint
            }

            Item { width: 1; height: Theme.paddingMedium }

            // (246) All / none, as in the mark-played dialog
            Flow {
                visible: podcasts.count > 1
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: page.checkedCount === podcasts.count
                    onClicked: page.setAll(true)
                }
                Chip {
                    text: qsTr("None")
                    on: page.checkedCount === 0
                    onClicked: page.setAll(false)
                }
            }

            Item { width: 1; height: Theme.paddingMedium }
        }

        section.property: page.grouped ? "first" : ""
        section.delegate: SectionHeader {
            text: section === "true" ? page.firstTitle : page.restTitle
        }

        delegate: TextSwitch {
            width: list.width
            text: model.title
            // Archived podcasts keep everything in their archive folder
            description: model.archived ? qsTr("ended – not changed automatically") : ""
            checked: page.isChecked(model.podcastId)
            automaticCheck: false
            onClicked: page.toggle(model.podcastId)
        }

        VerticalScrollDecorator {}
    }
}

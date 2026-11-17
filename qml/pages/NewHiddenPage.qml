// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// (340, Thomas 17 Nov) Which podcasts New episodes shows ("Show in New
// episodes" of every podcast in one place, setting home.hiddenPodcasts).
// Opened from the line "n podcasts hidden ›" / "All podcasts shown ›" of
// New episodes. A value choice, so a page: every switch
// applies at once (as Hidden from statistics, 334). The list is read once,
// so the rows keep their place; the switches follow a local copy. Shown
// again, a podcast keeps its new flags (review 340: 302 cleared them).
Page {
    // No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "newHiddenPage"

    // The count shown per podcast follows the view of New episodes
    property bool onlyNew: true
    // (340, review) Read once, with the view passed when pushed
    property var podcasts: []
    Component.onCompleted: podcasts = core.newChoicePodcasts(onlyNew)
    // podcastId -> hidden, as switched here
    property var hidden: {
        var map = {}
        for (var i = 0; i < podcasts.length; i++) {
            map["" + podcasts[i].podcastId] = podcasts[i].hidden
        }
        return map
    }
    readonly property int shownCount: {
        var n = 0
        for (var k in hidden) {
            if (!hidden[k]) {
                n++
            }
        }
        return n
    }

    function setHidden(podcastId, value) {
        var copy = {}
        for (var k in hidden) {
            copy[k] = hidden[k]
        }
        copy["" + podcastId] = value
        hidden = copy
        core.setHiddenFromNew(podcastId, value)
    }
    function setAll(value) {
        var copy = {}
        for (var k in hidden) {
            copy[k] = value
        }
        hidden = copy
        core.setAllHiddenFromNew(value)
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true

        header: Column {
            width: list.width
            PageHeader { title: qsTr("Show in New episodes") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Off: the podcast's episodes stay out of the page New episodes (both views). Playlist, downloads and the podcast page work as usual.")
            }
            Item { width: 1; height: Theme.paddingMedium }
            Flow {
                visible: page.podcasts.length > 1
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: page.shownCount === page.podcasts.length
                    onClicked: page.setAll(false)
                }
                Chip {
                    text: qsTr("None")
                    on: page.shownCount === 0
                    onClicked: page.setAll(true)
                }
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        model: page.podcasts

        delegate: TextSwitch {
            width: list.width
            text: modelData.title
            description: modelData.count > 0
                         ? (page.onlyNew ? qsTr("%n new", "", modelData.count)
                                         : qsTr("%n unplayed", "", modelData.count))
                         : ""
            checked: !page.hidden["" + modelData.podcastId]
            automaticCheck: false
            onClicked: page.setHidden(modelData.podcastId, checked)
        }

        ViewPlaceholder {
            enabled: page.podcasts.length === 0
            text: qsTr("No podcasts yet")
        }

        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// (348, Thomas 17 Nov) Share my podcasts: which podcasts of the chosen
// folders go into the web page. Like "Show in New episodes" (340): one page,
// a switch per podcast, All / None; the list is read once so the rows keep
// their place. The choice is a list of podcasts left out (`excluded`,
// setting htmlExport.excluded) - a podcast subscribed later is in.
// Statistics on the page name only the podcasts on it (333).
Page {
    // No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "htmlExportPodcastsPage"

    property var folders: []
    // podcast ids left out (numbers); set by HtmlExportPage, changed here
    property var excluded: []

    property var podcasts: []
    Component.onCompleted: podcasts = core.htmlExportPodcasts(folders)

    readonly property int sharedCount: {
        var n = 0
        for (var i = 0; i < podcasts.length; i++) {
            if (excluded.indexOf(podcasts[i].podcastId) < 0) {
                n++
            }
        }
        return n
    }

    function setShared(podcastId, on) {
        var copy = []
        for (var i = 0; i < excluded.length; i++) {
            if (excluded[i] !== podcastId) {
                copy.push(excluded[i])
            }
        }
        if (!on) {
            copy.push(podcastId)
        }
        excluded = copy
    }
    function setAll(on) {
        // Only the podcasts of this list; choices for other folders stay
        var ids = {}
        for (var i = 0; i < podcasts.length; i++) {
            ids["" + podcasts[i].podcastId] = true
        }
        var copy = []
        for (var j = 0; j < excluded.length; j++) {
            if (!ids["" + excluded[j]]) {
                copy.push(excluded[j])
            }
        }
        if (!on) {
            for (var k = 0; k < podcasts.length; k++) {
                copy.push(podcasts[k].podcastId)
            }
        }
        excluded = copy
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true

        header: Column {
            width: list.width
            PageHeader { title: qsTr("Podcasts on the page") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Off: the podcast is not on the web page, and its name is not in the statistics (the totals count all your listening). Podcasts you subscribe to later are on it.")
            }
            Item { width: 1; height: Theme.paddingMedium }
            Flow {
                visible: page.podcasts.length > 1
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: page.sharedCount === page.podcasts.length
                    onClicked: page.setAll(true)
                }
                Chip {
                    text: qsTr("None")
                    on: page.sharedCount === 0
                    onClicked: page.setAll(false)
                }
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        model: page.podcasts

        delegate: TextSwitch {
            width: list.width
            text: modelData.title
            checked: page.excluded.indexOf(modelData.podcastId) < 0
            automaticCheck: false
            onClicked: page.setShared(modelData.podcastId, !checked)
        }

        ViewPlaceholder {
            enabled: page.podcasts.length === 0
            text: qsTr("No podcasts in the chosen folders")
        }

        VerticalScrollDecorator {}
    }
}

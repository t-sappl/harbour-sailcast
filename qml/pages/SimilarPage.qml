// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// "Ähnlich wie …" (141): fyyd's similar podcasts of one podcast - from the
// header line and the pull-down menu of the podcast page, and "Alle
// anzeigen" of the section in "Add podcast". Only this podcast's address
// (and, if fyyd lists it under another one, its title and author) is sent.
// A tap opens "Add podcast" (a subscribed one its podcast page).
Page {
    id: page

    property string feedUrl: ""
    property string podcastTitle: ""
    property string podcastAuthor: ""
    // (157) Its web page (fyyd's entry is also found by it) and categories
    // (Apple names): without fyyd, the podcasts of its main category
    property string website: ""
    property var categories: []
    property string fallbackCategory: ""
    property string fallbackLabel: ""
    property bool fyydKnows: false

    // (166) Also when fyyd knows the podcast but has nothing similar
    function startFallback(known) {
        var first = (categories && categories.length > 0) ? ("" + categories[0]).split("/")[0] : ""
        if (first.length === 0) {
            loadState = known ? "done" : "unknown"
            return
        }
        fyydKnows = known
        var all = core.interests().categories || []
        var label = first
        for (var i = 0; i < all.length; i++) {
            if (all[i].name === first) {
                label = all[i].label
            }
        }
        fallbackCategory = first
        fallbackLabel = label
        loadState = "category"
        core.discoverCategory(first)
    }
    // Handed over by "Add podcast", which has loaded them already
    property var items: []
    // "" | "loading" | "done" | "unknown" | "failed"
    property string loadState: items.length > 0 ? "done" : ""
    property string error: ""
    property int subscriptionVersion: 0
    // (174) Subscribed ones last, below "Bereits abonniert" - they show the
    // list fits, but new ones belong on top
    readonly property var ordered: orderBySubscription(items, subscriptionVersion)
    readonly property int firstSubscribed: firstSubscribedIn(ordered)
    function firstSubscribedIn(list) {
        for (var i = 0; i < list.length; i++) {
            if ((list[i].url || "").length > 0 && core.isSubscribed(list[i].url)) {
                return i
            }
        }
        return -1
    }
    function orderBySubscription(list, version) {
        var fresh = []
        var known = []
        for (var i = 0; i < list.length; i++) {
            var url = list[i].url || ""
            if (url.length > 0 && core.isSubscribed(url)) {
                known.push(list[i])
            } else {
                fresh.push(list[i])
            }
        }
        return fresh.concat(known)
    }
    // (170) Compact or with descriptions (default, as on the category page)
    property bool detailed: core.setting("ui.similarDetailed", "1") === "1"

    Component.onCompleted: {
        if (loadState === "") {
            loadState = "loading"
            core.similarPodcasts(feedUrl, podcastTitle, podcastAuthor, website)
        }
    }

    Connections {
        target: core
        onSimilarReady: {
            if (page.loadState !== "loading" || feedUrl !== page.feedUrl) {
                return
            }
            page.items = items
            if (known && items.length > 0) {
                page.loadState = "done"
            } else {
                page.startFallback(known)
            }
        }
        // (157) fyyd does not know it (166: or knows nothing similar): its
        // category instead
        onCategoryReady: {
            if (page.loadState !== "category" || category !== page.fallbackCategory) {
                return
            }
            var list = []
            for (var i = 0; i < items.length; i++) {
                if (items[i].url !== page.feedUrl && items[i].title !== page.podcastTitle) {
                    list.push(items[i])
                }
            }
            page.items = list
            page.loadState = "categoryDone"
        }
        onSimilarFailed: {
            if (page.loadState === "loading") {
                page.error = error
                page.loadState = "failed"
            }
        }
        onDataChanged: page.subscriptionVersion += 1
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        currentIndex: -1
        model: page.ordered

        PullDownMenu {
            MenuItem {
                text: page.detailed ? qsTr("Compact view") : qsTr("Detailed view")
                onClicked: {
                    page.detailed = !page.detailed
                    core.setSetting("ui.similarDetailed", page.detailed ? "1" : "0")
                }
            }
        }

        header: Column {
            width: list.width
            PageHeader {
                title: qsTr("Similar podcasts")
                description: page.fallbackCategory.length > 0
                             ? qsTr("From the same category: %1").arg(page.fallbackLabel)
                             : qsTr("Like %1").arg(page.podcastTitle)
            }
            // (157) Why a category instead
            Label {
                visible: page.fallbackCategory.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: page.fyydKnows
                      ? qsTr("fyyd knows no podcasts similar to %1 - here are active podcasts of the same category.").arg(page.podcastTitle)
                      : qsTr("fyyd does not know %1 yet - here are active podcasts of the same category.").arg(page.podcastTitle)
            }
            Item { width: 1; height: Theme.paddingMedium; visible: page.fallbackCategory.length > 0 }
        }

        delegate: Column {
            width: list.width
            SectionHeader {
                visible: index === page.firstSubscribed
                text: qsTr("Already subscribed")
            }
            DiscoverRow {
                width: parent.width
                entry: modelData
                detailed: page.detailed
                version: page.subscriptionVersion
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.loadState === "loading" || page.loadState === "category"
            visible: running
        }

        ViewPlaceholder {
            enabled: page.loadState !== "loading" && page.loadState !== "category" && page.items.length === 0
            text: page.loadState === "failed" ? qsTr("fyyd cannot be reached")
                : page.loadState === "unknown" ? qsTr("fyyd does not know this podcast")
                : qsTr("No similar podcasts known")
            hintText: page.loadState === "failed" ? page.error
                    : qsTr("Similar podcasts come from fyyd users' collections.")
        }

        VerticalScrollDecorator {}
    }
}

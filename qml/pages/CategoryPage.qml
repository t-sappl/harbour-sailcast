// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// One category to browse (150): its active podcasts in the user's languages
// (Podcast Index trending of the category and fyyd's active podcasts of it,
// see AppCore::discoverCategory), compact rows as on the Discover page, 20
// more while scrolling. Sub-categories as chips on top ("Alle" = the top
// category). A tap opens "Add podcast" (▶ listens there without
// subscribing); the context menu offers "Kein Interesse".
Page {
    id: page

    property string category: ""        // Apple name, "History"
    property string label: ""
    // The shown one: the category or one of its sub-categories
    property string current: category
    property string currentLabel: label
    property var items: []
    property string state_: ""          // "" | "loading" | "done"
    property var hiddenUrls: []
    property int subscriptionVersion: 0
    readonly property int pageSize: 20
    // (152) Detailed rows with two lines of the description - default here
    property bool detailed: core.setting("ui.categoryDetailed", "1") === "1"

    property var info: core.interests()
    readonly property var subs: subsOf(info)
    readonly property string languagesText: {
        var names = []
        var languages = info.languages || []
        for (var i = 0; i < languages.length; i++) {
            names.push((info.languageNames || {})[languages[i]] || languages[i])
        }
        return names.join(", ")
    }

    function subsOf(data) {
        var list = []
        var all = data.categories || []
        for (var i = 0; i < all.length; i++) {
            if (all[i].parent === category && all[i].level !== 3) {
                list.push(all[i])
            }
        }
        list.sort(function(a, b) { return a.label.localeCompare(b.label) })
        return list
    }

    function load(name, text) {
        current = name
        currentLabel = text
        state_ = "loading"
        items = []
        listModel.clear()
        core.discoverCategory(name)
    }

    function fill(count) {
        while (listModel.count < Math.min(count, items.length)) {
            var i = listModel.count === 0 ? 0 : listModel.get(listModel.count - 1).i + 1
            if (i >= items.length) {
                break
            }
            listModel.append({ i: i, url: items[i].url })
        }
    }

    Component.onCompleted: load(category, label)

    ListModel { id: listModel }

    Connections {
        target: core
        onCategoryReady: {
            if (category !== page.current) {
                return
            }
            page.items = items
            page.state_ = "done"
            listModel.clear()
            page.fill(page.pageSize)
        }
        onSuggestionHidden: {
            page.hiddenUrls = page.hiddenUrls.concat([url])
            hideTimer.restart()
        }
        onDataChanged: page.subscriptionVersion += 1
    }

    // "Kein Interesse": removed once the context menu has closed
    Timer {
        id: hideTimer
        interval: 400
        onTriggered: {
            var removed = 0
            for (var n = listModel.count - 1; n >= 0; n--) {
                if (page.hiddenUrls.indexOf(listModel.get(n).url) >= 0) {
                    listModel.remove(n)
                    removed++
                }
            }
            page.hiddenUrls = []
            page.fill(listModel.count + removed)
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        currentIndex: -1
        model: listModel

        PullDownMenu {
            MenuItem {
                text: page.detailed ? qsTr("Compact view") : qsTr("Detailed view")
                onClicked: {
                    page.detailed = !page.detailed
                    core.setSetting("ui.categoryDetailed", page.detailed ? "1" : "0")
                }
            }
        }

        onContentYChanged: {
            if (contentHeight > 0 && contentY + height > contentHeight - Theme.itemSizeMedium * 5) {
                page.fill(listModel.count + page.pageSize)
            }
        }

        header: Column {
            width: list.width
            PageHeader {
                title: page.currentLabel
                description: page.current !== page.category
                             ? qsTr("%1 · in %2").arg(page.label).arg(page.languagesText)
                             : qsTr("Active podcasts in %1").arg(page.languagesText)
            }
            // Sub-categories (fyyd's tree): "Alle" = the category itself
            Flow {
                visible: page.subs.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip {
                    text: qsTr("All")
                    on: page.current === page.category
                    onClicked: page.load(page.category, page.label)
                }
                Repeater {
                    model: page.subs
                    Chip {
                        text: modelData.label
                        on: page.current === modelData.name
                        onClicked: page.load(modelData.name, modelData.label)
                    }
                }
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        delegate: DiscoverRow {
            entry: page.items[model.i] || ({})
            canHide: true
            detailed: page.detailed
            version: page.subscriptionVersion
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.state_ === "loading"
            visible: running
        }

        ViewPlaceholder {
            enabled: page.state_ === "done" && listModel.count === 0
            text: qsTr("No active podcasts found")
            hintText: qsTr("Podcast Index and fyyd know none in your languages right now - or they cannot be reached.")
        }

        VerticalScrollDecorator {}
    }
}

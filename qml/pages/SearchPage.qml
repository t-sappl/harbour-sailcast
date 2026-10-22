// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Podcast search in the Podcast Index directory (31). Search field at the
// top; results with cover, title, author, number of episodes and language.
// A tap opens "Add podcast" with the feed address - it is checked and
// subscribed there as usual. Already subscribed podcasts are marked.
// Without a built-in API key (podcastindex.pri) the page says so.
Page {
    id: page
    objectName: "searchPage"

    property var results: []
    property string searchedTerm: ""
    property bool searching: false
    property string error: ""
    // Text of the search field: the field lives in the list header (its
    // own component), so its id cannot be reached from here (39)
    property string searchText: ""
    // "Add podcast" since 51 (search and feed address in one place): a typed
    // or copied web address is checked as a feed instead of searched
    readonly property bool textIsAddress: looksLikeUrl(searchText)
    readonly property string clipboardAddress: Clipboard.hasText && looksLikeUrl(Clipboard.text) ? Clipboard.text.trim() : ""

    function looksLikeUrl(text) {
        var t = (text || "").trim()
        return t.length > 3 && t.indexOf(" ") < 0 && t.indexOf("\n") < 0
                && (/^(https?|feed):/i.test(t) || /^[a-z0-9-]+(\.[a-z0-9-]+)+(\/|$)/i.test(t))
    }

    function openAddress(address) {
        pageStack.push(Qt.resolvedUrl("AddPodcastDialog.qml"), { initialUrl: address.trim() })
    }

    function search() {
        var term = page.searchText.trim()
        if (term.length < 2 || !core.searchAvailable || looksLikeUrl(term)) {
            return
        }
        searchedTerm = term
        searching = true
        error = ""
        core.searchPodcasts(term)
    }

    Connections {
        target: core
        onSearchFinished: {
            if (term !== page.searchedTerm) {
                return
            }
            page.searching = false
            page.results = results
        }
        onSearchFailed: {
            if (term !== page.searchedTerm) {
                return
            }
            page.searching = false
            page.results = []
            page.error = error
        }
        // A podcast subscribed from here: mark it in the list
        onDataChanged: {
            var list = page.results
            var changed = false
            for (var i = 0; i < list.length; i++) {
                var subscribed = core.isSubscribed(list[i].url)
                if (subscribed !== list[i].subscribed) {
                    list[i].subscribed = subscribed
                    changed = true
                }
            }
            if (changed) {
                page.results = []
                page.results = list
            }
        }
    }

    // Searches once typing has paused
    Timer {
        id: searchTimer
        interval: 800
        onTriggered: page.search()
    }

    // Header and search field outside the list (73, as Sailwave: a field
    // in the list header lost the keyboard and jumped whenever the
    // results - the list's model - were replaced)
    Column {
        id: topColumn
        width: parent.width - appWindow.playerPanelWidth

        PageHeader {
            title: qsTr("Add podcast")
            description: core.searchAvailable ? qsTr("Search (Podcast Index) or feed address") : qsTr("Feed address")
        }

        SearchField {
            id: searchField
            width: parent.width
            placeholderText: core.searchAvailable ? qsTr("Title, topic, author or address") : qsTr("Feed address")
            focus: true
            inputMethodHints: Qt.ImhNoPredictiveText
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: {
                searchTimer.stop()
                if (page.textIsAddress) {
                    page.openAddress(text)
                } else {
                    page.search()
                }
                focus = false
            }
            onTextChanged: {
                page.searchText = text
                // An emptied field shows the explanation again, not the
                // last error or results (59: both on top of each other)
                if (text.trim().length === 0) {
                    searchTimer.stop()
                    page.searchedTerm = ""
                    page.error = ""
                    page.results = []
                    page.searching = false
                } else {
                    searchTimer.restart()
                }
            }
        }

        // A web address: check it as a feed
        BackgroundItem {
            visible: page.textIsAddress
            width: parent.width
            onClicked: page.openAddress(page.searchText)
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                text: qsTr("Check feed address: %1").arg(core.maskUrl(page.searchText.trim()))
                color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }

        // An address in the clipboard (as in Sailfeed)
        BackgroundItem {
            visible: !page.textIsAddress && page.searchText.length === 0 && page.clipboardAddress.length > 0
            width: parent.width
            onClicked: page.openAddress(page.clipboardAddress)
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                text: qsTr("Address from the clipboard: %1").arg(core.maskUrl(page.clipboardAddress))
                color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
            }
        }

        BusyIndicator {
            anchors.horizontalCenter: parent.horizontalCenter
            size: BusyIndicatorSize.Medium
            running: page.searching
            visible: running
        }

        // How this page works, with examples - while the field is empty (52)
        Column {
            visible: page.searchText.trim().length === 0
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            Item { width: 1; height: Theme.paddingMedium }

            Label {
                visible: core.searchAvailable
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Search by title, topic or author, e.g. \"Geschichte\", \"motorcycle\" or \"Kurvendiskussion\". Tap a result to see details and the latest episodes, then subscribe.")
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Or enter the address of a feed or of the podcast's website, e.g. \"cre.fm\" or \"https://example.com/feed.xml\" - it is checked before subscribing.")
            }
            Label {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("An address copied before (e.g. from the browser) is offered right here.")
            }
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.topMargin: topColumn.height
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: page.results
        currentIndex: -1

        delegate: BackgroundItem {
            id: row
            width: list.width
            height: Math.max(Theme.itemSizeLarge, textColumn.height + 2 * Theme.paddingMedium)
            onClicked: pageStack.push(Qt.resolvedUrl("AddPodcastDialog.qml"), { initialUrl: modelData.url, info: modelData })

            CoverImage {
                id: cover
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.itemSizeMedium
                height: width
                source: modelData.imageUrl
                fallbackText: modelData.title
            }

            Column {
                id: textColumn
                anchors {
                    left: cover.right
                    leftMargin: Theme.paddingMedium
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: modelData.title
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeSmall
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    visible: text.length > 0
                    width: parent.width
                    text: modelData.author
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
                Label {
                    width: parent.width
                    text: {
                        var parts = [qsTr("%n episodes", "", modelData.episodes)]
                        if (modelData.language.length > 0) {
                            parts.push(modelData.language)
                        }
                        if (modelData.subscribed) {
                            parts.push(qsTr("subscribed"))
                        }
                        return parts.join(" · ")
                    }
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: modelData.subscribed ? Theme.highlightColor : Theme.secondaryColor
                }
            }
        }

        ViewPlaceholder {
            enabled: !page.searching && page.results.length === 0 && !page.textIsAddress
                     && page.searchText.trim().length > 0
                     && (!core.searchAvailable || page.error.length > 0 || page.searchedTerm.length > 0)
            text: !core.searchAvailable ? qsTr("Search not available - enter a feed address")
                                        : (page.error.length > 0 ? qsTr("Search failed") : qsTr("Nothing found"))
            hintText: !core.searchAvailable ? qsTr("This build has no Podcast Index key (podcastindex.pri)")
                                            : page.error
        }

        VerticalScrollDecorator {}
    }
}

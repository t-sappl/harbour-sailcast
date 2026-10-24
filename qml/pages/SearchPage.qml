// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Podcast search in the Podcast Index directory (31). Search field at the
// top; results with cover, title, author, number of episodes and language.
// A tap opens "Add podcast" with the feed address - it is checked and
// subscribed there as usual. Already subscribed podcasts are marked.
// Without a built-in API key (podcastindex.pri) the page says so.
// An address (also a bare domain such as "cre.fm") is checked right away
// (84): a feed found there is offered at the top; if there is none, the
// name of the site ("cre") is searched in Podcast Index instead - for
// typed, pasted and shared text alike.
// Since 99 again only for adding (85-98 it also searched the own data, with
// a switch since 86 - mixed up): the search on the device is its own page
// (LocalSearchPage), which leads here with its term. A result that is
// already subscribed opens its podcast page.
Page {
    id: page
    objectName: "searchPage"

    property var results: []
    // The list: Podcast Index results ({kind: "index", data})
    readonly property var rows: buildRows(results)

    function buildRows(index) {
        var list = []
        for (var j = 0; j < index.length; j++) {
            list.push({ kind: "index", data: index[j] })
        }
        return list
    }
    property string searchedTerm: ""
    property bool searching: false
    property string error: ""
    // Text of the search field: the field lives in the list header (its
    // own component), so its id cannot be reached from here (39)
    property string searchText: ""
    // Text handed over by the sharing menu (84) - put into the field
    property string initialText: ""
    Component.onCompleted: {
        if (initialText.length > 0) {
            searchField.text = initialText.trim()
            searchField.focus = false
            searchTimer.stop()
            page.checkAddressOrSearch()
        }
    }

    // Check of a typed address (84): "" | "checking" | "ok" | "login" |
    // "failed"; probeResult as from core.checkFeed
    property string probeState: ""
    property string probedText: ""
    property var probeResult: ({})
    // Name searched instead when no feed was found ("cre" for "cre.fm")
    property string fallbackTerm: ""
    // Further names to try when that search finds nothing (103): the page
    // title, the podcast name in a platform address, the site name
    property var fallbackTerms: []

    function siteName(address) {
        var host = address.trim().replace(/^[a-z][a-z0-9+.-]*:(\/\/)?/i, "")
        host = host.split("/")[0].split("?")[0].split(":")[0].toLowerCase()
        var labels = host.split(".")
        if (labels.length > 0 && labels[0] === "www") {
            labels.shift()
        }
        // The label before the top-level domain: "lagedernation" for
        // "lagedernation.org", "example" for "podcast.example.com"
        return labels.length >= 2 ? labels[labels.length - 2] : (labels[0] || "")
    }

    // Podcast platforms (103): their site name says nothing about the
    // podcast ("spotify"), the podcast's name is in the address
    readonly property var platforms: ["spotify", "apple", "youtube", "youtu", "soundcloud", "deezer", "pocketcasts",
        "castbox", "podbean", "overcast", "castro", "google", "amazon", "audible", "podcastaddict", "player",
        "podchaser", "listennotes", "podtail", "fyyd", "audionow", "podimo"]

    function isPlatform(address) {
        return platforms.indexOf(siteName(address)) >= 0
    }

    // Apple Podcasts link: its id ("…/id1234567"), else ""
    function appleId(address) {
        var m = /^(https?:\/\/)?(podcasts|itunes)\.apple\.com\/.*[\/?&]id=?(\d{5,})/i.exec(address.trim())
        return m ? m[3] : ""
    }

    // The podcast name in a platform address (103): the part after
    // "profile", "show", "podcast", "channel" ... - "freebsd" for
    // creators.spotify.com/pod/profile/freebsd/, "lage der nation" for
    // podcasts.apple.com/at/podcast/lage-der-nation/id1234
    function platformName(address) {
        var path = address.trim().replace(/^[a-z][a-z0-9+.-]*:(\/\/)?/i, "").split("?")[0].split("#")[0]
        var parts = path.split("/").slice(1)
        var keys = ["profile", "show", "shows", "podcast", "podcasts", "channel", "c", "user", "s"]
        for (var i = 0; i < parts.length; i++) {
            // "@name" (YouTube) or the part after a key word
            var handle = parts[i].charAt(0) === "@"
            if (handle || (i < parts.length - 1 && keys.indexOf(parts[i].toLowerCase()) >= 0)) {
                var name = handle ? parts[i] : parts[i + 1]
                try {
                    name = decodeURIComponent(name)
                } catch (e) {
                }
                name = name.replace(/^@/, "").replace(/[-_+]+/g, " ").trim()
                // Not an id ("id1234", "4rOoJ6Egrf8K2IrywzwOMk")
                if (name.length > 1 && !/^id\d+$/i.test(name) && !/^\d+$/.test(name)
                        && !(name.length >= 12 && /\d/.test(name) && name.indexOf(" ") < 0)) {
                    return name
                }
            }
        }
        return ""
    }

    // The page title without the platform's addition (103): "Lage der
    // Nation | Podcast on Spotify" -> "Lage der Nation"
    function cleanTitle(title, address) {
        var t = (title || "").trim()
        t = t.replace(/\s+[|\u2013\u2014-]\s+[^|\u2013\u2014]*(spotify|apple podcasts|youtube|soundcloud|deezer|pocket casts|castbox|podbean|podcast addict|player\.fm)[^|]*$/i, "")
        t = t.replace(/^(listen to|h\u00f6r dir)\s+/i, "").replace(/\s+(on|auf|bei) (spotify|apple podcasts)$/i, "")
        if (t.indexOf(" | ") > 0) {
            t = t.split(" | ")[0]
        }
        t = t.trim()
        // A bare domain ("news.ORF.at") or the site's own name finds nothing better
        if (t.length < 2 || t.length > 80 || (t.indexOf(" ") < 0 && t.indexOf(".") > 0)
                || t.toLowerCase() === siteName(address)) {
            return ""
        }
        return t
    }

    // What to search for an address without a podcast feed, best first
    function fallbackTermsFor(address, result) {
        var list = []
        var candidates = [cleanTitle(result.pageTitle, address)]
        if (isPlatform(address)) {
            candidates.push(platformName(address))
        } else {
            candidates.push(siteName(address))
        }
        for (var i = 0; i < candidates.length; i++) {
            var term = candidates[i]
            var known = false
            for (var j = 0; j < list.length; j++) {
                known = known || list[j].toLowerCase() === term.toLowerCase()
            }
            if (term.length > 1 && !known) {
                list.push(term)
            }
        }
        return list
    }

    // Searches the first term; the next ones when it finds nothing
    function startFallback(terms) {
        fallbackTerms = terms.slice(1)
        fallbackTerm = terms.length > 0 ? terms[0] : ""
        if (fallbackTerm.length > 0) {
            search(fallbackTerm)
        }
    }

    function checkAddressOrSearch() {
        var text = page.searchText.trim()
        var apple = appleId(text)
        if (apple.length > 0 && core.searchAvailable) {
            // Apple Podcasts (103): looked up by its id, no page to check
            probeState = "apple"
            probedText = text
            probeResult = ({})
            var terms = ["itunes:" + apple]
            if (platformName(text).length > 1) {
                terms.push(platformName(text))
            }
            startFallback(terms)
        } else if (looksLikeUrl(text)) {
            probeState = "checking"
            probedText = text
            probeResult = ({})
            fallbackTerm = ""
            core.checkFeed(text)
        } else {
            probeState = ""
            search()
        }
    }
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
        // The dialog checks again; this page no longer waits for its check
        if (probeState === "checking") {
            probeState = ""
        }
        pageStack.push(Qt.resolvedUrl("AddPodcastDialog.qml"), { initialUrl: address.trim() })
    }

    // termOverride: the site name after a failed address check (84)
    function search(termOverride) {
        var term = termOverride !== undefined ? termOverride : page.searchText.trim()
        if (termOverride === undefined) {
            if (looksLikeUrl(term)) {
                return
            }
        }
        if (term.length < 2 || !core.searchAvailable) {
            return
        }
        searchedTerm = term
        searching = true
        error = ""
        core.searchPodcasts(term)
    }

    Connections {
        target: core
        // Result of the address check (84)
        onFeedChecked: {
            if (page.probeState !== "checking" || result.requested !== page.probedText
                    || page.searchText.trim() !== page.probedText) {
                return
            }
            page.probeResult = result
            // A web page without a feed with audio or video episodes (a blog
            // or news feed only, 102): "no podcast on this page"
            if (result.noPodcast) {
                page.probeState = "empty"
                page.startFallback(page.fallbackTermsFor(page.probedText, result))
            } else if (result.ok || result.needsLogin) {
                page.probeState = result.ok ? "ok" : "login"
            } else {
                page.probeState = "failed"
                page.startFallback(page.fallbackTermsFor(page.probedText, result))
            }
        }
        onSearchFinished: {
            if (term !== page.searchedTerm) {
                return
            }
            // Nothing for this name: the next one (103)
            if (results.length === 0 && term === page.fallbackTerm && page.fallbackTerms.length > 0) {
                page.startFallback(page.fallbackTerms)
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
        onTriggered: page.checkAddressOrSearch()
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
                    page.openAddress(page.probeState === "ok" ? page.probeResult.url : text)
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
                    page.probeState = ""
                } else {
                    // A new text: the old check and its fallback no longer apply
                    page.probeState = ""
                    page.fallbackTerm = ""
                    page.fallbackTerms = []
                    if (page.looksLikeUrl(text) && page.searchedTerm.length > 0) {
                        page.results = []
                        page.searchedTerm = ""
                    }
                    searchTimer.restart()
                }
            }
        }

        // A web address: checked right away (84); tap opens "Add podcast".
        // While checking, without a podcast or with a login: one line
        BackgroundItem {
            id: addressRow
            visible: page.textIsAddress && page.probeState !== "ok"
            width: parent.width
            height: Math.max(Theme.itemSizeSmall, addressColumn.height + 2 * Theme.paddingSmall)
            // An Apple Podcasts link has no feed to check (103)
            enabled: page.probeState !== "apple"
            onClicked: page.openAddress(page.probeState === "login" ? (page.probeResult.url || page.searchText)
                                                                    : page.searchText)
            readonly property bool noPodcast: page.probeState === "failed" || page.probeState === "empty"
                                              || page.probeState === "apple"
            Column {
                id: addressColumn
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin - Theme.iconSizeSmall
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    text: page.probeState === "login" ? qsTr("Feed with login: %1").arg(core.maskUrl(page.probeResult.url || page.searchText.trim()))
                        : page.probeState === "checking" ? qsTr("Checking %1 …").arg(core.maskUrl(page.searchText.trim()))
                        : page.probeState === "failed" ? qsTr("No feed at %1").arg(core.maskUrl(page.searchText.trim()))
                        : page.probeState === "empty" ? qsTr("No podcast found on this page")
                        : page.probeState === "apple" ? qsTr("Apple Podcasts link")
                        : qsTr("Check feed address: %1").arg(core.maskUrl(page.searchText.trim()))
                    color: addressRow.noPodcast ? Theme.secondaryHighlightColor
                                                : (addressRow.highlighted ? Theme.highlightColor : Theme.primaryColor)
                }
                Label {
                    visible: text.length > 0
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: {
                        var parts = []
                        if (page.probeState === "empty") {
                            parts.push(core.maskUrl(page.searchText.trim()) + ": "
                                       + (page.probeResult.discovered && !page.probeResult.ok && (page.probeResult.error || "").length > 0
                                          ? page.probeResult.error
                                          : qsTr("no feed with audio or video episodes.")))
                        } else if (page.probeState === "failed" && (page.probeResult.error || "").length > 0) {
                            parts.push(page.probeResult.error)
                        }
                        if (page.fallbackTerm.indexOf("itunes:") === 0) {
                            parts.push(qsTr("Its podcast in Podcast Index below."))
                        } else if (addressRow.noPodcast && page.fallbackTerm.length > 1 && core.searchAvailable) {
                            parts.push(qsTr("Search results for \"%1\" below - tap to check the address again").arg(page.fallbackTerm))
                        }
                        return parts.join(" ")
                    }
                }
            }
            BusyIndicator {
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                size: BusyIndicatorSize.ExtraSmall
                running: page.probeState === "checking"
                visible: running
            }
        }

        // A podcast found at the address (102): a card like a search result,
        // with its cover, highlighted - before it was a plain line that was
        // easy to overlook
        SectionHeader {
            visible: foundCard.visible
            text: qsTr("Podcast at this address")
        }
        BackgroundItem {
            id: foundCard
            visible: page.textIsAddress && page.probeState === "ok"
            width: parent.width
            height: Math.max(Theme.itemSizeExtraLarge, foundColumn.height + 2 * Theme.paddingMedium)
            highlighted: down
            onClicked: {
                var id = page.probeResult.subscribed ? core.podcastIdForUrl(page.probeResult.url) : -1
                if (id > 0) {
                    pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: id })
                } else {
                    page.openAddress(page.probeResult.url || page.searchText)
                }
            }

            Rectangle {
                anchors.fill: parent
                color: Theme.rgba(Theme.highlightBackgroundColor, 0.15)
            }

            CoverImage {
                id: foundCover
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.itemSizeLarge
                height: width
                source: page.probeResult.coverUrl || ""
                fallbackText: page.probeResult.title || ""
            }

            Column {
                id: foundColumn
                anchors {
                    left: foundCover.right
                    leftMargin: Theme.paddingLarge
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                Label {
                    width: parent.width
                    text: page.probeResult.title || page.probeResult.url || ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    color: Theme.highlightColor
                }
                Label {
                    visible: text.length > 0
                    width: parent.width
                    text: page.probeResult.author || ""
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: foundCard.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
                Label {
                    width: parent.width
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: page.probeResult.subscribed ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: qsTr("%n episodes", "", page.probeResult.episodes || 0)
                          + (page.probeResult.subscribed ? " · " + qsTr("subscribed") : " · " + qsTr("tap to add"))
                }
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
        model: page.rows
        currentIndex: -1

        // Components inside the delegate: they see its data (rowItem.entry)
        delegate: Item {
            id: rowItem
            width: list.width
            height: rowLoader.height
            property var entry: modelData.data

            Loader {
                id: rowLoader
                width: parent.width
                sourceComponent: indexRow
            }

            Component {
                id: indexRow
                BackgroundItem {
                    id: row
                    width: list.width
                    height: Math.max(Theme.itemSizeLarge, textColumn.height + 2 * Theme.paddingMedium)
                    // Subscribed already: its podcast page (99), else "Add podcast"
                    onClicked: {
                        var id = rowItem.entry.subscribed ? core.podcastIdForUrl(rowItem.entry.url) : -1
                        if (id > 0) {
                            pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: id })
                        } else {
                            pageStack.push(Qt.resolvedUrl("AddPodcastDialog.qml"), { initialUrl: rowItem.entry.url, info: rowItem.entry })
                        }
                    }

                    CoverImage {
                        id: cover
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        height: width
                        source: rowItem.entry.imageUrl
                        fallbackText: rowItem.entry.title
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
                            text: rowItem.entry.title
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeSmall
                            color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            visible: text.length > 0
                            width: parent.width
                            text: rowItem.entry.author
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                        Label {
                            width: parent.width
                            text: {
                                var parts = [qsTr("%n episodes", "", rowItem.entry.episodes)]
                                if (rowItem.entry.language.length > 0) {
                                    parts.push(rowItem.entry.language)
                                }
                                if (rowItem.entry.subscribed) {
                                    parts.push(qsTr("subscribed"))
                                }
                                return parts.join(" · ")
                            }
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: rowItem.entry.subscribed ? Theme.highlightColor : Theme.secondaryColor
                        }
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: !page.searching && page.rows.length === 0 && page.searchText.trim().length > 0
                     && (!page.textIsAddress || ((page.probeState === "failed" || page.probeState === "empty" || page.probeState === "apple") && page.searchedTerm.length > 0))
                     && (!core.searchAvailable || page.error.length > 0 || page.searchedTerm.length > 0)
            text: !core.searchAvailable ? qsTr("Search not available - enter a feed address")
                : (page.error.length > 0 ? qsTr("Search failed") : qsTr("Nothing found"))
            hintText: !core.searchAvailable ? qsTr("This build has no Podcast Index key (podcastindex.pri)")
                : page.error
        }

        VerticalScrollDecorator {}
    }
}

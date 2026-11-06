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
// Since 137 also the Discover page (one page for both, decided 28 Oct): while
// the field is empty the sections "Für dich" (with "Kein Interesse"), "Im
// Trend" (Podcast Index) and "Gerade aktiv" (fyyd) are shown, 5 each with
// "Alle anzeigen"; typing shows the search as before.
Page {
    id: page
    objectName: "searchPage"

    property var results: []
    // The list: Podcast Index results ({kind: "index", data})
    readonly property var rows: buildRows(results)
    // An empty field shows Discover (137): since 140 two tabs, "Für dich"
    // and "Im Trend", each one long list (ListModel, loaded 20 at a time
    // while scrolling, as in Sailwave) - a JS array as model scrolled to the
    // top whenever it changed (139 used a column; too long a page)
    readonly property bool discovering: searchText.trim().length === 0

    // Discover: "" | "loading" | "done"
    property var forYou: []
    property var trending: []
    property string forYouState: ""
    property string trendingState: ""
    // "Neu" (149): first episodes of podcasts new on fyyd; their podcasts
    // arrive one by one (fyyd id -> podcast map, {} = unknown)
    property var newItems: []
    property var newPodcasts: ({})
    property var newRequested: ({})
    property string newState: ""
    property string newError: ""
    // ▶ in "Neu": the guid (193: or the Podnews show's feed) being loaded
    // for "Anhören ohne Abo"
    property string newPlaying: ""
    // (193) ▶ of a Podnews show: the trailer's length, to find it in the feed
    property int newTrailerSeconds: -1
    // (164) "Neu" only with German among the languages, and not switched off
    // in the settings (read when Discover is shown); (193) English too -
    // Podnews' "New Podcasts"; with both, switchable as "Im Trend"
    property bool newTabSetting: core.setting("ui.discoverNewTab", "1") === "1"
    readonly property var newLanguages: {
        var list = []
        for (var i = 0; i < trendLanguages.length; i++) {
            if (trendLanguages[i] === "de" || trendLanguages[i] === "en") {
                list.push(trendLanguages[i])
            }
        }
        return list
    }
    property string newLanguage: ""
    readonly property bool showNewTab: newTabSetting && newLanguages.length > 0
    onShowNewTabChanged: {
        if (!showNewTab && tab === "new") {
            tab = "forYou"
        }
    }
    property string tab: "forYou"
    property var hiddenUrls: []
    // "Im Trend" (139: Podcast Index and fyyd in one list), language switchable
    property var trendLanguages: []
    // (175) "Empfehlungen aus meinen Abos" switched off: offered at the end
    // of Für dich, so it is not forgotten
    property bool fromSubscriptions: true
    property string trendLanguage: ""
    property var languageNames: ({})
    // "Angepasst an: …" - the interests in one line, tap: Meine Interessen
    property string adjustedText: ""
    property int seenRevision: -1
    // Raised on subscriptions: the rows show "abonniert"
    property int subscriptionVersion: 0
    // State of the shown tab (149: three tabs)
    readonly property string tabState: tab === "forYou" ? forYouState : (tab === "new" ? newState : trendingState)
    readonly property bool discoverEmpty: forYouState === "done" && trendingState === "done"
                                          && forYou.length === 0 && trending.length === 0
    // Ring page (139, setting "Entdecken als Ringseite"): Discover starts
    // when the page is first shown, not when it is built at app start
    property bool ringMember: false
    readonly property int pageSize: 20
    property bool detailed: core.setting("ui.discoverDetailed", "0") === "1"
    // First start (146): nothing listened to or subscribed, no level and no
    // keyword - "Für dich" asks "Was interessiert dich?" with the 12 most
    // popular categories; a tap toggles "mehr"
    property bool onboarding: false
    // (227) Once shown, the card stays while this page is open - also when
    // the choice already counts as "customized" (back from the languages
    // page), so the list below can follow every tap
    property bool _onboardingSession: false
    // (231) Left through the card's own chips (languages, more options):
    // the card stays; any other way out ends it - Discover is a ring page
    // living the whole session (229), the card stayed until a restart
    property bool _viaCard: false
    // (234) A chip tapped or the languages changed while the card was shown
    property bool _changedInSession: false
    // (232) The interests have been read once - before that "onboarding"
    // is not known yet and the "My interests" box (and its hint) must not
    // show: the hint was queued at creation and used up during the first
    // start
    property bool _interestsRead: false
    property var popularTiles: []
    property var chosen: ({})
    property string languagesText: ""

    function popularOf(info) {
        var list = []
        var all = info.categories || []
        for (var i = 0; i < all.length; i++) {
            if (all[i].parent.length === 0 && all[i].popular >= 0) {
                list.push(all[i])
            }
        }
        list.sort(function(a, b) { return a.popular - b.popular })
        return list
    }

    function toggleChosen(name) {
        var map = {}
        for (var key in chosen) {
            map[key] = chosen[key]
        }
        map[name] = !map[name]
        chosen = map
        _changedInSession = true
        core.setInterest(name, map[name] ? 1 : 0)
        // (227) Live: "Für dich" follows the choice a moment after the last
        // tap (instead of the button "Vorschläge anzeigen")
        liveRefresh.restart()
    }

    Timer {
        id: liveRefresh
        interval: 1200
        onTriggered: {
            page.seenRevision = core.interestsRevision
            core.refreshForYou()
        }
    }

    function readInterests() {
        var info = core.interests()
        fromSubscriptions = info.fromSubscriptions === true
        trendLanguages = info.languages || []
        languageNames = info.languageNames || {}
        if (trendLanguages.indexOf(trendLanguage) < 0) {
            trendLanguage = trendLanguages.length > 0 ? trendLanguages[0] : ""
        }
        if (newLanguages.indexOf(newLanguage) < 0) {
            newLanguage = newLanguages.length > 0 ? newLanguages[0] : ""
            if (newState !== "") {
                newState = ""
                startNew()
            }
        }
        // Order (145): the languages, then what is wanted - "mehr"
        // categories, keywords - then "weniger", then "nie" (two lines only)
        var moreParts = [], lessParts = [], neverParts = []
        var categories = info.categories || []
        for (var i = 0; i < categories.length; i++) {
            var c = categories[i]
            if (c.level === 1) {
                moreParts.push(c.label + " \u2191")
            } else if (c.level === 2) {
                lessParts.push(c.label + " \u2193")
            } else if (c.level === 3) {
                neverParts.push(qsTr("without %1").arg(c.label))
            }
        }
        var more = info.more || []
        for (var j = 0; j < more.length; j++) {
            moreParts.push(more[j].word)
        }
        var neverList = info.never || []
        for (var k = 0; k < neverList.length; k++) {
            neverParts.push(qsTr("without %1").arg(neverList[k].word))
        }
        var parts = moreParts.concat(lessParts, neverParts)
        var names = []
        for (var l = 0; l < trendLanguages.length; l++) {
            names.push(languageNames[trendLanguages[l]] || trendLanguages[l])
        }
        // Below "Meine Interessen ›" (146: the title says what it is)
        adjustedText = parts.length > 0 ? [names.join(", ")].concat(parts).join(" \u00b7 ")
                                         : qsTr("%1 · nothing set yet").arg(names.join(", "))
        seenRevision = core.interestsRevision
        if (!info.hasListening && !info.customized
                && core.setting("discover.onboarded", "0") !== "1") {
            _onboardingSession = true
        }
        onboarding = _onboardingSession
        _interestsRead = true
        popularTiles = popularOf(info)
        var map = {}
        for (var m = 0; m < categories.length; m++) {
            if (categories[m].level === 1) {
                map[categories[m].name] = true
            }
        }
        chosen = map
        languagesText = names.join(", ")
    }

    function startDiscover() {
        if (forYouState !== "") {
            return
        }
        readInterests()
        forYouState = "loading"
        trendingState = "loading"
        core.discover()
    }

    // (193) "Neu" in the other language (German: fyyd, English: Podnews);
    // (195) chosen by name
    function chooseNewLanguage(language) {
        newLanguage = language
        // A list still loading for the other language is dropped (its
        // answer is ignored)
        newState = ""
        startNew()
    }

    function chooseTrendLanguage(language) {
        trendLanguage = language
        reloadTrending()
    }

    // (234) Reloading with a list on screen: the list stays until the new
    // one is there, a line says it is updating (it looked as if nothing
    // happened); without a list as before (big spinner)
    property bool forYouRefreshing: false
    property bool trendRefreshing: false
    function reloadTrending() {
        if (trendModel.count > 0) {
            trendRefreshing = true
        } else {
            trendingState = "loading"
        }
        core.discoverTrending(trendLanguage)
    }

    // Fills the list model up to "count" entries of the array (by index)
    function fill(model, array, count) {
        while (model.count < Math.min(count, array.length)) {
            var i = model.count === 0 ? 0 : model.get(model.count - 1).i + 1
            if (i >= array.length) {
                break
            }
            model.append({ i: i, url: array[i].url })
        }
    }

    // "Neu" (149): the next rows, and their podcasts (asked once each).
    // newNext: the next index into newItems - rows of unwanted or unknown
    // podcasts are left out (removed when their podcast arrives)
    property int newNext: 0
    function fillNew(count) {
        while (newModel.count < count && newNext < newItems.length) {
            var i = newNext
            newNext = i + 1
            var item = newItems[i]
            var known = newPodcasts[item.key]
            if (known !== undefined && known.wanted !== true) {
                continue
            }
            // (193) A Podnews show is known by its feed from the start
            newModel.append({ i: i, url: known ? known.url : (item.trailer === true ? item.key : "") })
            if (known === undefined && !newRequested[item.key]) {
                var asked = {}
                for (var key in newRequested) {
                    asked[key] = newRequested[key]
                }
                asked[item.key] = true
                newRequested = asked
                core.newPodcastInfo(item.key)
            }
        }
    }

    function playNew(episode, podcast) {
        if (episode.trailer === true) {
            // (193) The show's feed: then its trailer and first episode
            newPlaying = episode.key
            newTrailerSeconds = episode.duration || 0
            core.listenWithoutSubscribing(podcast.url || episode.key, podcast.title || episode.title || "", "")
            return
        }
        newPlaying = episode.guid
        newTrailerSeconds = -1
        core.listenWithoutSubscribing(podcast.url, podcast.title || "", episode.guid)
    }

    function startNew() {
        if (newState === "loading") {
            return
        }
        newState = "loading"
        newError = ""
        newModel.clear()
        newNext = 0
        newItems = []
        newRequested = ({})
        core.discoverNew(newLanguage)
    }

    function loadMore() {
        if (tab === "new") {
            fillNew(newModel.count + pageSize)
        } else if (tab === "forYou") {
            fill(forYouModel, forYou, forYouModel.count + pageSize)
        } else {
            fill(trendModel, trending, trendModel.count + pageSize)
        }
    }

    onStatusChanged: {
        // (235) The trend language before anything is read again - the card
        // re-evaluation below already switched it, and the check at the end
        // saw no change (Trending did not reload after "change ›")
        var trendBefore = trendLanguage
        if (status === PageStatus.Inactive && !_viaCard) {
            _onboardingSession = false
            // (234) Something was changed while the card was shown: the
            // first start is done for good (the card came back otherwise -
            // a language alone does not count as "customized")
            if (onboarding && _changedInSession) {
                core.setSetting("discover.onboarded", "1")
            }
        }
        if (status === PageStatus.Active) {
            // (234) Back from the languages / more options pages with changes
            if (onboarding && core.interestsRevision !== seenRevision) {
                _changedInSession = true
            }
            // (231) Back from elsewhere: the card again only while nothing
            // is chosen (readInterests decides) - (234) without taking the
            // "changed" check below away (it reloads For you / Trending)
            if (!_viaCard && onboarding && !_onboardingSession) {
                var seen = seenRevision
                readInterests()
                seenRevision = seen
            }
            _viaCard = false
            newTabSetting = core.setting("ui.discoverNewTab", "1") === "1"
            // (229) Discover is a ring page by default - how to take it out
            if (ringMember) {
                // (239) Users coming from 1.1 (they stored the old player
                // bar hint "playerBar", 1.2 uses "playerBar12"): say it is new
                appWindow.showHint("discoverRing",
                                   core.setting("hint.playerBar", "0") === "1"
                                   ? qsTr("New in 1.2: Discover – suggestions, trends and new podcasts. A swipe page; to take it out: Settings → General")
                                   : qsTr("Discover is one of the swipe pages – to take it out: Settings → General"), page)
            }
        }
        if (status !== PageStatus.Active || !discovering) {
            return
        }
        if (forYouState === "" ) {
            startDiscover()
        } else if (core.interestsRevision !== seenRevision) {
            // Back from "Meine Interessen" with changes: once, now (140)
            readInterests()
            core.refreshForYou()
            // "Neu": the marks and filters follow the interests (149)
            if (newState !== "") {
                startNew()
            }
            if (trendLanguage !== trendBefore) {
                reloadTrending()
            }
        }
    }

    ListModel { id: forYouModel }
    ListModel { id: trendModel }
    ListModel { id: newModel }

    // "Kein Interesse": removed once the context menu has closed
    Timer {
        id: hideTimer
        interval: 400
        onTriggered: {
            for (var n = forYouModel.count - 1; n >= 0; n--) {
                if (page.hiddenUrls.indexOf(forYouModel.get(n).url) >= 0) {
                    forYouModel.remove(n)
                }
            }
            // "Neu" too (149)
            var removedNew = 0
            for (var m = newModel.count - 1; m >= 0; m--) {
                if (page.hiddenUrls.indexOf(newModel.get(m).url) >= 0) {
                    newModel.remove(m)
                    removedNew++
                }
            }
            page.hiddenUrls = []
            page.fill(forYouModel, page.forYou, forYouModel.count + 1)
            if (removedNew > 0) {
                page.fillNew(newModel.count + removedNew)
            }
        }
    }

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
    // Text of the search field (39); 142: restored - lost in 140's rewrite
    property string searchText: ""
    // Text handed over by the sharing menu (84) - put into the field
    property string initialText: ""

    Component.onCompleted: {
        if (initialText.length > 0) {
            searchField.text = initialText.trim()
            searchField.focus = false
            searchTimer.stop()
            page.checkAddressOrSearch()
        } else {
            // Discover: the keyboard only when the user taps the field
            searchField.focus = false
            if (!ringMember) {
                page.startDiscover()
            }
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
        // A shared episode link (148): the dialog gets the page address, it
        // finds the episode there (and the time in the link)
        if (probeState === "ok" && probeResult.shared && address === probeResult.url) {
            address = probeResult.requested || address
        }
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
        // Discover (137)
        onForYouStarted: {
            if (forYouModel.count > 0) {
                page.forYouRefreshing = true
            } else {
                page.forYouState = "loading"
            }
        }
        onForYouReady: {
            page.forYouRefreshing = false
            page.forYou = items
            page.forYouState = "done"
            forYouModel.clear()
            page.fill(forYouModel, items, page.pageSize)
        }
        // The category tree arrives later on the very first start
        onInterestsChanged: {
            page.fromSubscriptions = core.interests().fromSubscriptions === true
            if (page.onboarding && page.popularTiles.length === 0) {
                page.popularTiles = page.popularOf(core.interests())
            }
        }
        onTrendingReady: {
            if (language === page.trendLanguage || page.trendLanguages.length === 0) {
                page.trendRefreshing = false
                page.trending = items
                page.trendingState = "done"
                trendModel.clear()
                page.fill(trendModel, items, page.pageSize)
            }
        }
        onSuggestionHidden: {
            page.hiddenUrls = page.hiddenUrls.concat([url])
            hideTimer.restart()
        }
        // "Neu" (149)
        onNewReady: {
            // (193) An answer for the other language (switched meanwhile)
            if (language !== (page.newLanguage === "en" ? "en" : "de")) {
                return
            }
            page.newItems = items
            page.newState = "done"
            newModel.clear()
            page.newNext = 0
            page.fillNew(page.pageSize)
        }
        onNewFailed: {
            if (page.newState === "loading" && language === (page.newLanguage === "en" ? "en" : "de")) {
                page.newState = "failed"
                page.newError = error
            }
        }
        onNewPodcastReady: {
            // (198) The signal's "key" kept apart from the loop variable -
            // 193 named both "key": every podcast landed under the last key
            // of the map (Neu without categories, filters and "Passt zu")
            var wantedKey = key
            var map = {}
            for (var known in page.newPodcasts) {
                map[known] = page.newPodcasts[known]
            }
            map[wantedKey] = podcast
            page.newPodcasts = map
            // Its rows: the address for "Kein Interesse", or gone when the
            // podcast is unwanted or unknown to fyyd
            var removed = 0
            for (var n = newModel.count - 1; n >= 0; n--) {
                var item = page.newItems[newModel.get(n).i]
                if (item && item.key === wantedKey) {
                    if (podcast.wanted === true) {
                        newModel.setProperty(n, "url", podcast.url)
                    } else {
                        newModel.remove(n)
                        removed++
                    }
                }
            }
            if (removed > 0) {
                page.fillNew(newModel.count + removed)
            }
        }
        onPreviewReady: {
            if (page.newPlaying === "") {
                return
            }
            page.newPlaying = ""
            if (page.newTrailerSeconds >= 0) {
                // (193) A Podnews show: its trailer, then its first episode
                var found = core.trailerAndFirst(podcastId, page.newTrailerSeconds)
                page.newTrailerSeconds = -1
                if (found.trailer > 0) {
                    appWindow.player.playThen(found.trailer, found.first)
                } else if (found.first > 0) {
                    appWindow.player.playOrToggle(found.first, false)
                } else {
                    pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: podcastId })
                }
            } else if (episodeId > 0) {
                appWindow.player.playOrToggle(episodeId, false)
            } else {
                pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: podcastId })
            }
        }
        onPreviewFailed: {
            if (page.newPlaying !== "") {
                page.newPlaying = ""
                page.newTrailerSeconds = -1
                appWindow.showMessage(qsTr("Cannot load the episodes: %1").arg(error))
            }
        }
        // A podcast subscribed from here: mark it in the list
        onDataChanged: {
            page.subscriptionVersion += 1
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
            title: qsTr("Discover")
            // (256) No description line any more: the search field below
            // says the same, and a second grey line under the title was
            // mixed up with the neighbours line (Thomas on the device)
        }
        // (255) Neighbours of the ring, below the header
        RingNeighbours { kind: "discover"; ringMember: page.ringMember }

        SearchField {
            id: searchField
            width: parent.width
            // (253, usability audit #2) shorter and says plainly that a
            // feed address works too, not just a search term
            placeholderText: core.searchAvailable ? qsTr("Name, feed, episode link") : qsTr("Feed address")
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
                    page.startDiscover()
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

        // "Kategorien durchstöbern ›" (150, variant A of 28 Oct)
        // (192) With the view switch on the right: the pull-down menu of the
        // list (its only entry) drew the bright pulley line below the tabs
        Item {
            visible: page.discovering
            width: parent.width
            height: Theme.itemSizeExtraSmall
            BackgroundItem {
                id: categoriesLine
                width: categoriesLabel.implicitWidth + 2 * Theme.horizontalPageMargin
                height: parent.height
                onClicked: pageStack.push(Qt.resolvedUrl("CategoriesPage.qml"))
                Label {
                    id: categoriesLabel
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Browse categories") + " \u203a"
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }
            BackgroundItem {
                anchors.right: parent.right
                width: viewLabel.implicitWidth + 2 * Theme.horizontalPageMargin
                height: parent.height
                onClicked: {
                    page.detailed = !page.detailed
                    core.setSetting("ui.discoverDetailed", page.detailed ? "1" : "0")
                }
                Label {
                    id: viewLabel
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: page.detailed ? qsTr("Compact") : qsTr("Detailed")
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryColor
                }
            }
        }

        // Discover (140): tabs "Für dich" | "Im Trend" - two buttons (a
        // Silica tab view is not used: not sure it is there and allowed on
        // every Sailfish OS version)
        Row {
            visible: page.discovering
            width: parent.width
            Repeater {
                model: page.showNewTab
                       ? [{ key: "forYou", text: qsTr("For you") }, { key: "trending", text: qsTr("Trending") },
                          { key: "new", text: qsTr("New") }]
                       : [{ key: "forYou", text: qsTr("For you") }, { key: "trending", text: qsTr("Trending") }]
                // By id (145): the label and the line are not direct children
                // of the BackgroundItem, "parent.current" was undefined -
                // both tabs were underlined
                BackgroundItem {
                    id: tabItem
                    width: (page.width - appWindow.playerPanelWidth) / (page.showNewTab ? 3 : 2)
                    height: Theme.itemSizeSmall
                    readonly property bool current: page.tab === modelData.key
                    onClicked: {
                        page.tab = modelData.key
                        discoverList.positionViewAtBeginning()
                        // "Neu" loads when first shown (149)
                        if (modelData.key === "new" && page.newState === "") {
                            page.startNew()
                        }
                    }
                    Label {
                        anchors.centerIn: parent
                        text: modelData.text
                        color: tabItem.current || tabItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
                    }
                    Rectangle {
                        anchors.bottom: parent.bottom
                        anchors.horizontalCenter: parent.horizontalCenter
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        height: Theme.paddingSmall / 2
                        color: Theme.highlightColor
                        visible: tabItem.current
                    }
                }
            }
        }

        // "Meine Interessen ›" with what "Für dich" depends on (140; since 146
        // a tinted box with two lines - the plain line was easy to miss).
        // Not on the first start: the list header asks then.
        BackgroundItem {
            id: interestsCard
            visible: page._interestsRead && page.discovering && page.tab === "forYou" && !page.onboarding
            // (227) After the first start the choice moved into this box -
            // easy to overlook that it can be changed
            function hint() {
                if (visible) {
                    appWindow.showHint("interests", qsTr("Tap \"My interests\" to change what \"For you\" suggests"), page)
                }
            }
            onVisibleChanged: hint()
            Component.onCompleted: hint()
            width: parent.width
            height: cardColumn.height + 2 * Theme.paddingMedium + Theme.paddingSmall
            onClicked: pageStack.push(Qt.resolvedUrl("InterestsPage.qml"))
            // (192) The box on the page margin like the list below (it
            // reached further out), the text indented inside it
            Rectangle {
                x: Theme.horizontalPageMargin
                y: Theme.paddingSmall
                width: parent.width - 2 * x
                height: parent.height - Theme.paddingSmall
                radius: Theme.paddingMedium
                color: Theme.rgba(Theme.highlightBackgroundColor, interestsCard.highlighted ? 0.35 : 0.15)
            }
            Column {
                id: cardColumn
                x: Theme.horizontalPageMargin + Theme.paddingMedium
                y: Theme.paddingMedium + Theme.paddingSmall
                width: parent.width - 2 * x
                Label {
                    width: parent.width
                    text: qsTr("My interests") + " \u203a"
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: page.adjustedText
                }
            }
        }

        // (193) "Neu" in the other language (195: languages side by side)
        LanguageLine {
            visible: page.discovering && page.tab === "new" && page.newLanguages.length > 0
            languages: page.newLanguages
            names: page.languageNames
            current: page.newLanguage
            onChosen: page.chooseNewLanguage(language)
        }

        // "Im Trend" in another language (195: all of them to choose)
        LanguageLine {
            visible: page.discovering && page.tab === "trending" && page.trendLanguages.length > 0
            languages: page.trendLanguages
            names: page.languageNames
            current: page.trendLanguage
            onChosen: page.chooseTrendLanguage(language)
        }

        // (234) Updating while the old list stays on screen
        Item {
            id: refreshRow
            visible: page.discovering && ((page.tab === "forYou" && page.forYouRefreshing)
                                          || (page.tab === "trending" && page.trendRefreshing))
            width: parent.width
            height: visible ? Theme.itemSizeExtraSmall : 0
            Row {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                spacing: Theme.paddingMedium
                BusyIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    size: BusyIndicatorSize.ExtraSmall
                    running: refreshRow.visible
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Updating the suggestions …")
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
                var id = page.probeResult.subscribed && !page.probeResult.shared
                        ? core.podcastIdForUrl(page.probeResult.url) : -1
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
                // (148) The shared episode of that page
                Label {
                    visible: !!page.probeResult.shared
                    width: parent.width
                    text: page.probeResult.shared ? qsTr("Shared episode: %1").arg(page.probeResult.shared.title) : ""
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
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

        // How this page works, with examples - while the field is empty (52);
        // since 137 only when Discover has nothing to show (offline)
        Column {
            visible: page.searchText.trim().length === 0 && page.discoverEmpty
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
        visible: !page.discovering

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

    // "Neu" (149): what the list is, and where it comes from
    Component {
        id: newHeader
        // In an Item: the list places its header at x 0 (138)
        Item {
            width: discoverList.width
            height: newHint.height + Theme.paddingMedium
            Label {
                id: newHint
                x: Theme.horizontalPageMargin
                y: Theme.paddingSmall
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: page.newLanguage === "en"
                      ? qsTr("Trailers of new and noteworthy shows – chosen by Podnews. ▶ plays the trailer, then the first episode, without subscribing.")
                      : qsTr("First episodes of German podcasts new on fyyd – chosen by Hörsuppe. ▶ listens without subscribing.")
            }
        }
    }

    // First start (146): "Was interessiert dich?" above the list - the
    // trend suggestions stay visible below
    Component {
        id: onboardingCard
        Item {
            width: discoverList.width
            // (226) The card encloses the column: padding Medium above,
            // Large below the button, then a gap to the list (until 225 the
            // card was paddingMedium - paddingSmall too short and the
            // button stuck out)
            height: onboardingCardBackground.y + onboardingCardBackground.height + Theme.paddingLarge
            // (226) On the page margin like the list and the "My interests"
            // box (192), the content indented inside it
            Rectangle {
                id: onboardingCardBackground
                x: Theme.horizontalPageMargin
                y: Theme.paddingSmall
                width: parent.width - 2 * x
                height: onboardingColumn.height + Theme.paddingMedium + Theme.paddingLarge
                radius: Theme.paddingMedium
                color: Theme.rgba(Theme.highlightBackgroundColor, 0.15)
            }
            Column {
                id: onboardingColumn
                x: Theme.horizontalPageMargin + Theme.paddingMedium
                y: onboardingCardBackground.y + Theme.paddingMedium
                width: parent.width - 2 * x
                spacing: Theme.paddingMedium
                Label {
                    width: parent.width
                    text: qsTr("What are you interested in?")
                    color: Theme.highlightColor
                }
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Tap what you want to hear more of - \"For you\" follows it.")
                }
                Flow {
                    width: parent.width
                    spacing: Theme.paddingSmall
                    Repeater {
                        model: page.popularTiles
                        Chip {
                            text: modelData.label
                            on: page.chosen[modelData.name] === true
                            onClicked: page.toggleChosen(modelData.name)
                        }
                    }
                }
                Label {
                    visible: page.popularTiles.length === 0
                    width: parent.width
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Categories are loading")
                }
                // Languages and the full page
                Flow {
                    width: parent.width
                    spacing: Theme.paddingSmall
                    Chip {
                        text: qsTr("Languages: %1").arg(page.languagesText) + " \u203a"
                        onClicked: {
                            page._viaCard = true
                            pageStack.push(Qt.resolvedUrl("LanguagesPage.qml"))
                        }
                    }
                    Chip {
                        text: qsTr("More options") + " \u203a"
                        onClicked: {
                            page._viaCard = true
                            pageStack.push(Qt.resolvedUrl("InterestsPage.qml"))
                        }
                    }
                }
                // (227) No button "Vorschläge anzeigen" any more: the list below
                // follows each tap (the line under the title says so)
            }
        }
    }

    // Discover (140): the list of the chosen tab
    SilicaListView {
        id: discoverList
        anchors.fill: parent
        anchors.topMargin: topColumn.height
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        visible: page.discovering && !page.discoverEmpty
        currentIndex: -1
        model: page.tab === "forYou" ? forYouModel : (page.tab === "new" ? newModel : trendModel)

        // (152) Rows with descriptions - default compact here; since 153
        // also on "Neu"; since 192 switched on the categories line (no
        // pull-down menu)
        header: page.tab === "forYou" && page.onboarding ? onboardingCard
              : (page.tab === "new" ? newHeader : null)
        footer: page.tab === "forYou" && !page.onboarding && !page.fromSubscriptions
                && forYouModel.count > 0 ? subscriptionsHint : null

        // (175) Below Für dich while "Empfehlungen aus meinen Abos" is off
        Component {
            id: subscriptionsHint
            BackgroundItem {
                width: discoverList.width
                height: hintLabel.height + 2 * Theme.paddingLarge
                onClicked: pageStack.push(Qt.resolvedUrl("InterestsPage.qml"))
                Label {
                    id: hintLabel
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: qsTr("More podcasts similar to your favourites: switch on \"Recommendations from my subscriptions\" in My interests") + " \u203a"
                }
            }
        }

        // Near the end: the next 20
        onContentYChanged: {
            if (contentHeight > 0 && contentY + height > contentHeight - Theme.itemSizeMedium * 5) {
                page.loadMore()
            }
        }

        // "Neu" has its own rows (149); components inside the delegate see
        // its index
        delegate: Item {
            id: discoverDelegate
            width: discoverList.width
            height: rowLoader.height
            readonly property int itemIndex: model.i
            Loader {
                id: rowLoader
                width: parent.width
                sourceComponent: page.tab === "new" ? newRowComponent : plainRowComponent
            }
            Component {
                id: plainRowComponent
                DiscoverRow {
                    entry: (page.tab === "forYou" ? page.forYou : page.trending)[discoverDelegate.itemIndex] || ({})
                    canHide: page.tab === "forYou"
                    detailed: page.detailed
                    version: page.subscriptionVersion
                }
            }
            Component {
                id: newRowComponent
                NewEpisodeRow {
                    episode: page.newItems[discoverDelegate.itemIndex] || ({})
                    podcast: page.newPodcasts[(page.newItems[discoverDelegate.itemIndex] || {}).key] || ({})
                    version: page.subscriptionVersion
                    loadingGuid: page.newPlaying
                    detailed: page.detailed
                    onPlay: page.playNew(episode, podcast)
                }
            }
        }

        BusyIndicator {
            anchors.centerIn: parent
            size: BusyIndicatorSize.Large
            running: page.discovering && page.tabState === "loading"
            visible: running
        }

        ViewPlaceholder {
            enabled: discoverList.count === 0 && (page.tabState === "done" || page.tabState === "failed")
            text: page.tab === "forYou" ? qsTr("No suggestions yet")
                : page.tab === "new" ? (page.newState === "failed" ? (page.newLanguage === "en" ? qsTr("Podnews cannot be reached")
                                                                                                 : qsTr("fyyd cannot be reached"))
                                                      : qsTr("Nothing new right now"))
                : qsTr("Nothing trending")
            hintText: page.tab === "forYou" ? qsTr("They grow with what you listen to - or set your interests above.")
                    : page.tab === "new" ? (page.newState === "failed" ? page.newError
                                            : page.newLanguage === "en" ? qsTr("Trailers of new and noteworthy shows, chosen by Podnews.")
                                                                        : qsTr("First episodes of German podcasts new on fyyd, chosen by Hörsuppe."))
                    : qsTr("Nothing found in this language right now.")
        }

        VerticalScrollDecorator {}
    }
}

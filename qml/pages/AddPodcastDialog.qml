// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// Subscribe by feed URL (search: v2). Since 25:
// - a web address in the clipboard is filled in as a suggestion (as in
//   Sailfeed)
// - the address is checked before subscribing (core.checkFeed: loaded and
//   parsed, nothing stored): title, cover and number of episodes are shown,
//   and only a working feed can be subscribed - no dead entries in the list.
//   A website that names its feed is accepted, its feed is subscribed.
//   Feeds that need a login can be subscribed; the login is set on the
//   podcast page afterwards.
Dialog {
    id: dialog
    objectName: "addPodcastDialog"

    property bool fromClipboard: false
    // Set by the search page: this address instead of the clipboard
    property string initialUrl: ""
    // Set by the search page: the Podcast Index entry (description,
    // categories, website, last update, funding - 44)
    property var info: ({})
    readonly property bool hasInfo: (info.url || "").length > 0
    // "", "checking", "ok", "login", "error"
    property string checkState: ""
    property var result: ({})
    // "Ähnliche Podcasts" of the checked feed (141): "" | "loading" | "done"
    property var similar: []
    readonly property var similarNew: notSubscribed(similar, subscriptionVersion)
    function notSubscribed(list, version) {
        var result = []
        for (var i = 0; i < list.length; i++) {
            var url = list[i].url || ""
            if (url.length === 0 || !core.isSubscribed(url)) {
                result.push(list[i])
            }
        }
        return result
    }
    property string similarState: ""
    property string similarUrl: ""
    // (157) fyyd does not know the podcast: its category instead
    property bool similarUnknown: false
    property int subscriptionVersion: 0
    // "Anhören ohne Abo" (147): the guid being loaded ("" = none), "page"
    // while loading for "Alle Folgen ansehen"
    property string previewing: ""
    property string previewError: ""
    // (148) Start of the shared episode, from the time in the link
    property int previewStart: 0

    function preview(guid, start) {
        previewError = ""
        previewStart = start || 0
        previewing = guid
        core.listenWithoutSubscribing(result.url, result.title || "", guid === "page" ? "" : guid)
    }
    readonly property bool alreadySubscribed: result.subscribed === true

    // Not for a feed without audio episodes (61: e.g. the news feed found on orf.at)
    readonly property bool noAudio: checkState === "ok" && (result.episodes || 0) === 0
    canAccept: (checkState === "ok" || checkState === "login") && !alreadySubscribed && !noAudio

    onAccepted: core.subscribe(result.url, folderBox.folderId)

    function looksLikeUrl(text) {
        var t = (text || "").trim()
        return t.length > 3 && t.indexOf(" ") < 0 && t.indexOf("\n") < 0
                && (/^(https?|feed):/i.test(t) || /^[a-z0-9-]+(\.[a-z0-9-]+)+(\/|$)/i.test(t))
    }

    function startCheck() {
        var text = urlField.text.trim()
        result = ({})
        if (!looksLikeUrl(text)) {
            checkState = ""
            return
        }
        checkState = "checking"
        core.checkFeed(text)
    }

    Component.onCompleted: {
        if (initialUrl.length > 0) {
            urlField.text = initialUrl
            startCheck()
        } else if (Clipboard.hasText && looksLikeUrl(Clipboard.text)) {
            urlField.text = Clipboard.text.trim()
            fromClipboard = true
            startCheck()
        }
    }

    Connections {
        target: core
        onFeedChecked: {
            // An answer for an older text
            if (result.requested !== urlField.text.trim()) {
                return
            }
            dialog.result = result
            dialog.checkState = result.ok ? "ok" : (result.needsLogin ? "login" : "error")
            // Similar podcasts of a working feed (141)
            if (result.ok && (result.url || "").length > 0 && result.url !== dialog.similarUrl) {
                dialog.similarUrl = result.url
                dialog.similar = []
                dialog.similarUnknown = false
                dialog.similarState = "loading"
                core.similarPodcasts(result.url, result.title || "", result.author || "", result.link || "")
            }
        }
        onSimilarReady: {
            if (dialog.similarState === "loading" && feedUrl === dialog.similarUrl) {
                dialog.similar = items
                // (166) Also when fyyd knows it but has nothing similar
                dialog.similarUnknown = !known || items.length === 0
                dialog.similarState = "done"
            }
        }
        onSimilarFailed: {
            if (dialog.similarState === "loading") {
                dialog.similarState = "done"
            }
        }
        onDataChanged: dialog.subscriptionVersion += 1
        // (147) The podcast is stored provisionally: play or show it
        onPreviewReady: {
            var what = dialog.previewing
            dialog.previewing = ""
            if (what === "") {
                return
            }
            if (what === "page" || episodeId <= 0) {
                pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: podcastId })
            } else if (dialog.previewStart > 0) {
                appWindow.player.playFrom(episodeId, dialog.previewStart, false)
            } else {
                appWindow.player.playOrToggle(episodeId, false)
            }
        }
        onPreviewFailed: {
            if (dialog.previewing !== "") {
                dialog.previewing = ""
                dialog.previewError = error
            }
        }
    }

    // Checks once typing has paused
    Timer {
        id: checkTimer
        interval: 800
        onTriggered: dialog.startCheck()
    }

    SilicaFlickable {
        anchors.fill: parent
        // (154) The player bar is shown here (▶ without subscribing)
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        contentHeight: contentColumn.height

        Column {
            id: contentColumn
            width: parent.width
            spacing: Theme.paddingMedium

            DialogHeader {
                acceptText: qsTr("Subscribe")
            }

            TextField {
                id: urlField
                width: parent.width
                focus: !dialog.fromClipboard && dialog.initialUrl.length === 0
                label: dialog.fromClipboard ? qsTr("Feed URL (from the clipboard)") : qsTr("Feed URL")
                placeholderText: qsTr("https://example.com/feed.xml")
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: dialog.canAccept ? "image://theme/icon-m-enter-accept" : "image://theme/icon-m-enter-next"
                EnterKey.onClicked: {
                    if (dialog.canAccept) {
                        dialog.accept()
                    } else {
                        checkTimer.stop()
                        dialog.startCheck()
                    }
                }
                onTextChanged: {
                    if (dialog.fromClipboard && text.trim() !== Clipboard.text.trim()) {
                        dialog.fromClipboard = false
                    }
                    dialog.checkState = ""
                    checkTimer.restart()
                }
            }

            // Checking
            Row {
                visible: dialog.checkState === "checking"
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                BusyIndicator {
                    size: BusyIndicatorSize.Small
                    running: parent.visible
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Checking the feed …")
                    color: Theme.secondaryHighlightColor
                }
            }

            // The feed found
            Row {
                visible: dialog.checkState === "ok" || (dialog.checkState === "login" && (dialog.result.title || "").length > 0)
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingLarge

                CoverImage {
                    id: cover
                    width: Theme.itemSizeExtraLarge
                    height: width
                    source: dialog.result.coverUrl || ""
                    fallbackText: dialog.result.title || ""
                }

                Column {
                    width: parent.width - cover.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.paddingSmall
                    Label {
                        width: parent.width
                        text: dialog.result.title || ""
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        color: Theme.highlightColor
                    }
                    Label {
                        visible: text.length > 0
                        width: parent.width
                        text: dialog.result.author || ""
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        width: parent.width
                        text: qsTr("%n episodes", "", dialog.result.episodes || 0)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    // (245) Video podcast
                    Row {
                        visible: dialog.result.video === true
                        spacing: Theme.paddingSmall
                        VideoIcon {
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.iconSizeExtraSmall
                            color: Theme.secondaryHighlightColor
                        }
                        Label {
                            text: qsTr("Video podcast")
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.secondaryHighlightColor
                        }
                    }
                }
            }

            // Folder of the new podcast (45), only once there are folders or
        // the user wants one ("New folder …")
        FolderComboBox {
            id: folderBox
            visible: dialog.checkState === "ok" || dialog.checkState === "login"
        }

        // Notes and problems
            Label {
                visible: text.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: {
                    if (dialog.alreadySubscribed) {
                        return qsTr("Already subscribed")
                    }
                    if (dialog.checkState === "error" || dialog.checkState === "login") {
                        return dialog.result.error || ""
                    }
                    if (dialog.noAudio) {
                        return dialog.result.discovered === true
                                ? qsTr("The feed found on this website has no episodes with audio - it is not a podcast")
                                : qsTr("The feed has no episodes with audio - it is not a podcast")
                    }
                    if (dialog.checkState === "ok" && dialog.result.discovered === true) {
                        return qsTr("Feed of this website: %1").arg(core.maskUrl(dialog.result.url))
                    }
                    return ""
                }
            }
    
            // (237) The description at the top (under cover and folder): what
            // it is about comes before how it sounds - 4 lines, "More ⌄"
            // shows all of it. From Podcast Index when opened from the
            // search, else the feed's own (before: at the very end, after
            // the episodes and statistics)
            Column {
                id: topDescription
                property bool expanded: false
                readonly property string text: ((dialog.hasInfo ? dialog.info.description : dialog.result.description) || "")
                                               .replace(/<[^>]*>/g, "").trim()
                visible: text.length > 0
                width: parent.width
                Label {
                    id: topDescriptionLabel
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    maximumLineCount: topDescription.expanded ? 1000 : 4
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    textFormat: Text.PlainText
                    text: topDescription.text
                }
                BackgroundItem {
                    visible: topDescriptionLabel.truncated || topDescription.expanded
                    width: parent.width
                    height: Theme.itemSizeExtraSmall
                    onClicked: topDescription.expanded = !topDescription.expanded
                    Label {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                        text: topDescription.expanded ? qsTr("Less ⌃") : qsTr("More ⌄")
                    }
                }
            }

            // A shared episode link (148): that episode first, ▶ plays it -
            // without subscribing, or from the subscription - from the time
            // in the link
            Column {
                visible: dialog.checkState === "ok" && !!dialog.result.shared
                width: parent.width

                SectionHeader { text: qsTr("Shared episode") }

                Item {
                    id: sharedRow
                    width: parent.width
                    height: Math.max(sharedColumn.height, sharedPlay.height) + Theme.paddingSmall
                    readonly property var entry: dialog.result.shared || ({})
                    readonly property bool loading: dialog.previewing.length > 0
                                                    && dialog.previewing === (entry.guid || "")
                    Column {
                        id: sharedColumn
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: sharedPlay.x - x - Theme.paddingMedium
                        Label {
                            width: parent.width
                            text: sharedRow.entry.title || ""
                            wrapMode: Text.Wrap
                            maximumLineCount: 3
                            elide: Text.ElideRight
                            color: Theme.highlightColor
                        }
                        Label {
                            width: parent.width
                            text: {
                                var parts = []
                                if ((sharedRow.entry.published || 0) > 0) {
                                    parts.push(Qt.formatDate(new Date(sharedRow.entry.published * 1000), Qt.DefaultLocaleShortDate))
                                }
                                if ((sharedRow.entry.duration || 0) > 0) {
                                    parts.push(TimeFormat.length(sharedRow.entry.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                                }
                                if ((sharedRow.entry.start || 0) > 0) {
                                    parts.push(qsTr("from %1").arg(TimeFormat.clock(sharedRow.entry.start)))
                                }
                                return parts.join(" · ")
                            }
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryHighlightColor
                        }
                    }
                    RowPlayButton {
                        id: sharedPlay
                        visible: (sharedRow.entry.guid || "").length > 0 && !sharedRow.loading
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.horizontalPageMargin - sharedPlay.inset
                        anchors.verticalCenter: parent.verticalCenter
                        icon.source: "image://theme/icon-m-play"
                        enabled: dialog.previewing === ""
                        onClicked: dialog.preview(sharedRow.entry.guid, sharedRow.entry.start || 0)
                    }
                    BusyIndicator {
                        anchors.centerIn: sharedPlay
                        size: BusyIndicatorSize.Small
                        running: sharedRow.loading
                        visible: running
                    }
                }
            }

            // The newest episodes (50) - what the podcast is about
            Column {
                visible: (dialog.checkState === "ok") && (dialog.result.latest || []).length > 0
                width: parent.width

                SectionHeader { text: qsTr("Latest episodes") }

                // "Anhören ohne Abo" (147): play an episode right away - the
                // podcast is kept hidden until it is subscribed (30 days)
                Label {
                    visible: !dialog.alreadySubscribed && dialog.checkState === "ok"
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Tap ▶ to listen without subscribing.")
                }

                Repeater {
                    model: dialog.result.latest || []
                    Item {
                        id: latestRow
                        width: parent.width
                        height: Math.max(latestColumn.height, playButton.height) + Theme.paddingSmall
                        readonly property bool loading: dialog.previewing.length > 0
                                                        && dialog.previewing === (modelData.guid || "")
                        Column {
                            id: latestColumn
                            x: Theme.horizontalPageMargin
                            anchors.verticalCenter: parent.verticalCenter
                            width: playButton.x - x - Theme.paddingMedium
                            Label {
                                width: parent.width
                                text: modelData.title
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                font.pixelSize: Theme.fontSizeSmall
                                color: Theme.highlightColor
                            }
                            Label {
                                width: parent.width
                                text: {
                                    var parts = []
                                    if (modelData.published > 0) {
                                        parts.push(Qt.formatDate(new Date(modelData.published * 1000), Qt.DefaultLocaleShortDate))
                                    }
                                    if (modelData.duration > 0) {
                                        parts.push(TimeFormat.length(modelData.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                                    }
                                    return parts.join(" · ")
                                }
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: Theme.secondaryHighlightColor
                            }
                        }
                        RowPlayButton {
                            id: playButton
                            visible: (modelData.guid || "").length > 0 && !latestRow.loading
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.horizontalPageMargin - playButton.inset
                            anchors.verticalCenter: parent.verticalCenter
                            icon.source: "image://theme/icon-m-play"
                            enabled: dialog.previewing === ""
                            onClicked: dialog.preview(modelData.guid, 0)
                        }
                        BusyIndicator {
                            anchors.centerIn: playButton
                            size: BusyIndicatorSize.Small
                            running: latestRow.loading
                            visible: running
                        }
                    }
                }

                // All episodes without subscribing (147): the podcast page
                BackgroundItem {
                    visible: !dialog.alreadySubscribed && dialog.checkState === "ok"
                    width: parent.width
                    enabled: dialog.previewing === ""
                    onClicked: dialog.preview("page", 0)
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin - Theme.iconSizeSmall
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("All episodes (%1)").arg(dialog.result.episodes || 0) + " \u203a"
                        color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    }
                    BusyIndicator {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        size: BusyIndicatorSize.ExtraSmall
                        running: dialog.previewing === "page"
                        visible: running
                    }
                }

                Label {
                    visible: dialog.previewError.length > 0
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.errorColor
                    text: qsTr("Cannot load the episodes: %1").arg(dialog.previewError)
                }

                // Without the search: the feed's own description; (202) a
                // tap shows all of it (it stopped after 8 lines)
                Label {
                    id: feedDescription
                    property bool expanded: false
                    // (237) Shown at the top now
                    visible: false
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    maximumLineCount: expanded ? 1000 : 8
                    elide: Text.ElideRight
                    MouseArea {
                        anchors.fill: parent
                        onClicked: feedDescription.expanded = !feedDescription.expanded
                    }
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    textFormat: Text.PlainText
                    text: (dialog.result.description || "").replace(/<[^>]*>/g, "").trim()
                }
            }

            // (163) How often it appears and when next - as on the podcast
            // page (162), from the checked feed
            Column {
                visible: dialog.checkState === "ok" && (dialog.result.rhythm || "").length > 0
                width: parent.width
                SectionHeader { text: qsTr("Statistics") }
                StatTiles {
                    tiles: {
                        var r = dialog.result
                        var tiles = [{ value: "" + (r.episodes || 0), label: qsTr("Episodes") },
                                     { value: r.rhythm || "", label: qsTr("publishing rhythm") }]
                        if ((r.next || 0) > 0) {
                            tiles.push({ value: Qt.formatDate(new Date(r.next), qsTr("d MMM")),
                                         label: qsTr("expected next episode") })
                        }
                        return tiles
                    }
                }
                Item { width: 1; height: Theme.paddingMedium }
            }

            // Details from Podcast Index (only when opened from the search)
            Column {
                visible: dialog.hasInfo
                width: parent.width
                spacing: Theme.paddingSmall

                SectionHeader { text: qsTr("About this podcast") }

                // (237) The description moved to the top of the dialog

                DetailItem {
                    // German names, a top category only once (158)
                    readonly property var labels: core.categoryLabels(dialog.info.categories || [])
                    visible: labels.length > 0
                    label: qsTr("Categories")
                    value: labels.join(", ")
                }
                DetailItem {
                    visible: value.length > 0
                    label: qsTr("Language")
                    value: (dialog.info.language || "").length > 0 ? core.languageName(dialog.info.language) : ""
                }
                DetailItem {
                    visible: (dialog.info.episodes || 0) > 0
                    label: qsTr("Episodes")
                    value: "" + (dialog.info.episodes || 0)
                }
                DetailItem {
                    visible: (dialog.info.lastUpdate || 0) > 0
                    label: qsTr("Last update")
                    value: (dialog.info.lastUpdate || 0) > 0
                           ? Qt.formatDate(new Date(dialog.info.lastUpdate * 1000), Qt.DefaultLocaleShortDate) : ""
                }
                DetailItem {
                    visible: value.length > 0 && value !== (dialog.info.author || "")
                    label: qsTr("Owner")
                    value: dialog.info.ownerName || ""
                }
                DetailItem {
                    visible: dialog.info.explicit === true
                    label: qsTr("Explicit")
                    value: qsTr("yes")
                }

                // (163) As on the podcast page: side by side, with space
                Item { width: 1; height: Theme.paddingMedium }
                Flow {
                    id: dialogLinkButtons
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingMedium
                    ActionPill {
                        kind: "secondary"
                        visible: (dialog.info.link || "").length > 0
                        text: qsTr("Website")
                        onClicked: Qt.openUrlExternally(dialog.info.link)
                    }
                    ActionPill {
                        kind: "secondary"
                        visible: (dialog.info.fundingUrl || "").length > 0
                        text: (dialog.info.fundingMessage || "").length > 0 && dialog.info.fundingMessage.length < 30
                              ? dialog.info.fundingMessage : qsTr("Support the podcast")
                        onClicked: Qt.openUrlExternally(dialog.info.fundingUrl)
                    }
                }
                Item { width: 1; height: Theme.paddingMedium }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Source: Podcast Index")
                }
                Item { width: 1; height: Theme.paddingLarge }
            }

            // "Ähnliche Podcasts" (141): 5, "Alle anzeigen" ► the similar page
            Column {
                width: parent.width
                visible: dialog.similarState === "loading" || dialog.similar.length > 0
                         || (dialog.similarUnknown && (dialog.result.categories || []).length > 0)
                SectionHeader { text: qsTr("Similar podcasts") }
                // (157) Not on fyyd: the same category on the similar page
                BackgroundItem {
                    visible: dialog.similarUnknown && (dialog.result.categories || []).length > 0
                    width: parent.width
                    onClicked: pageStack.push(Qt.resolvedUrl("SimilarPage.qml"), {
                                                  feedUrl: dialog.similarUrl,
                                                  podcastTitle: dialog.result.title || "",
                                                  podcastAuthor: dialog.result.author || "",
                                                  website: dialog.result.link || "",
                                                  categories: dialog.result.categories || [] })
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        truncationMode: TruncationMode.Fade
                        text: qsTr("From the same category") + " \u203a"
                        color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    }
                }
                BusyIndicator {
                    anchors.horizontalCenter: parent.horizontalCenter
                    size: BusyIndicatorSize.Small
                    running: dialog.similarState === "loading"
                    visible: running
                }
                // (174) Only ones not subscribed yet - "Show all" has all
                Repeater {
                    model: dialog.similarNew.slice(0, 5)
                    DiscoverRow {
                        width: parent.width
                        entry: modelData
                        version: dialog.subscriptionVersion
                    }
                }
                BackgroundItem {
                    visible: dialog.similar.length > Math.min(5, dialog.similarNew.length)
                    width: parent.width
                    onClicked: pageStack.push(Qt.resolvedUrl("SimilarPage.qml"), {
                                                  feedUrl: dialog.similarUrl,
                                                  podcastTitle: dialog.result.title || "",
                                                  podcastAuthor: dialog.result.author || "",
                                                  website: dialog.result.link || "",
                                                  categories: dialog.result.categories || [],
                                                  items: dialog.similar })
                    Label {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        horizontalAlignment: Text.AlignRight
                        text: qsTr("Show all (%1)").arg(dialog.similar.length)
                        color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    }
                }
                Item { width: 1; height: Theme.paddingLarge }
            }
        }

        VerticalScrollDecorator {}
    }
}

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
    
            // The newest episodes (50) - what the podcast is about
            Column {
                visible: (dialog.checkState === "ok") && (dialog.result.latest || []).length > 0
                width: parent.width

                SectionHeader { text: qsTr("Latest episodes") }

                Repeater {
                    model: dialog.result.latest || []
                    Item {
                        width: parent.width
                        height: latestColumn.height + Theme.paddingSmall
                        Column {
                            id: latestColumn
                            x: Theme.horizontalPageMargin
                            width: parent.width - 2 * Theme.horizontalPageMargin
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
                    }
                }

                // Without the search: the feed's own description
                Label {
                    visible: !dialog.hasInfo && (dialog.result.description || "").length > 0
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    maximumLineCount: 8
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    textFormat: Text.PlainText
                    text: (dialog.result.description || "").replace(/<[^>]*>/g, "").trim()
                }
            }

            // Details from Podcast Index (only when opened from the search)
            Column {
                visible: dialog.hasInfo
                width: parent.width
                spacing: Theme.paddingSmall

                SectionHeader { text: qsTr("About this podcast") }

                Label {
                    visible: text.length > 0
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                    textFormat: Text.PlainText
                    text: (dialog.info.description || "").replace(/<[^>]*>/g, "").trim()
                }

                DetailItem {
                    visible: (dialog.info.categories || []).length > 0
                    label: qsTr("Categories")
                    value: (dialog.info.categories || []).join(", ")
                }
                DetailItem {
                    visible: value.length > 0
                    label: qsTr("Language")
                    value: dialog.info.language || ""
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

                Button {
                    visible: (dialog.info.link || "").length > 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Website")
                    onClicked: Qt.openUrlExternally(dialog.info.link)
                }
                Button {
                    visible: (dialog.info.fundingUrl || "").length > 0
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: (dialog.info.fundingMessage || "").length > 0 && dialog.info.fundingMessage.length < 30
                          ? dialog.info.fundingMessage : qsTr("Support the podcast")
                    onClicked: Qt.openUrlExternally(dialog.info.fundingUrl)
                }
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
        }

        VerticalScrollDecorator {}
    }
}

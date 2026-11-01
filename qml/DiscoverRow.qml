// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// One podcast on the Discover page (137, compact since 140 - two lines: the
// title, then the reason ("Ähnlich wie His2Go") or the author; "abonniert"
// when subscribed). A tap opens "Add podcast" with the details (a
// subscribed one its podcast page). With canHide the context menu offers
// "Kein Interesse" - it calls the core directly (no callback kept in the
// menu, 26); the page removes the row on core.suggestionHidden.
ListItem {
    id: row
    property var entry: ({})
    property bool canHide: false
    // Raised by the page when subscriptions change (the entry itself does
    // not tell)
    property int version: 0
    // (152) Detailed: a larger cover, the title on up to two lines and two
    // lines of the description - for browsing a category
    property bool detailed: false
    readonly property string plainDescription: detailed
            ? (entry.description || "").replace(/<[^>]*>/g, " ").replace(/&nbsp;/g, " ").replace(/&amp;/g, "&")
              .replace(/\s+/g, " ").trim()
            : ""
    readonly property bool subscribed: version >= 0 && (entry.url || "").length > 0 && core.isSubscribed(entry.url)

    width: parent ? parent.width : 0
    contentHeight: detailed ? Math.max(cover.height, textColumn.height) + 2 * Theme.paddingMedium
                            : Theme.itemSizeMedium
    menu: canHide && !subscribed ? hideMenu : null

    onClicked: {
        var id = subscribed ? core.podcastIdForUrl(entry.url) : -1
        if (id > 0) {
            pageStack.push(Qt.resolvedUrl("pages/EpisodesPage.qml"), { podcastId: id })
        } else {
            pageStack.push(Qt.resolvedUrl("pages/AddPodcastDialog.qml"), { initialUrl: entry.url, info: entry })
        }
    }

    Component {
        id: hideMenu
        ContextMenu {
            MenuItem {
                text: qsTr("Not interested")
                onClicked: core.hideSuggestion(row.entry.url, row.entry.title, row.entry.author || "")
            }
        }
    }

    CoverImage {
        id: cover
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: row.detailed ? Theme.itemSizeExtraLarge * 0.8 : Theme.itemSizeMedium - 2 * Theme.paddingSmall
        height: width
        source: row.entry.imageUrl || ""
        fallbackText: row.entry.title || ""
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
            text: row.entry.title || ""
            truncationMode: row.detailed ? TruncationMode.None : TruncationMode.Fade
            wrapMode: row.detailed ? Text.Wrap : Text.NoWrap
            maximumLineCount: row.detailed ? 2 : 1
            elide: row.detailed ? Text.ElideRight : Text.ElideNone
            font.pixelSize: Theme.fontSizeSmall
            color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            width: parent.width
            text: {
                var parts = []
                if ((row.entry.reason || "").length > 0) {
                    parts.push(row.entry.reason)
                } else if ((row.entry.author || "").length > 0) {
                    // (197) Im Trend: the main category in front
                    if ((row.entry.category || "").length > 0) {
                        parts.push(row.entry.category)
                    }
                    parts.push(row.entry.author)
                } else if ((row.entry.episodes || 0) > 0) {
                    parts.push(qsTr("%n episodes", "", row.entry.episodes))
                }
                if (row.subscribed) {
                    parts.push(qsTr("subscribed"))
                }
                return parts.join(" · ")
            }
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: (row.entry.reason || "").length > 0 || row.subscribed
                   ? (row.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor)
                   : (row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor)
        }
        Label {
            visible: row.detailed && text.length > 0
            width: parent.width
            text: row.plainDescription
            wrapMode: Text.Wrap
            maximumLineCount: 2
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeExtraSmall
            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }
}

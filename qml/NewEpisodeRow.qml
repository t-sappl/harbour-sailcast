// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// One row of "Neu" on the Discover page (149): the first episode of a
// podcast new on fyyd (Hörsuppe's curation "Nullnummern"). Two lines: the
// podcast (loaded separately - the curation names only its fyyd id), then
// the episode with its date and length, or "Passt zu: Geschichte". ▶ plays
// the episode without subscribing; a tap opens "Add podcast" (a subscribed
// podcast its page); the context menu offers "Kein Interesse".
// (193) In English a show of Podnews' "New Podcasts" (episode.trailer):
// known from the start (Podnews names title and feed), "Trailer · 2 min";
// ▶ plays the trailer, then the first episode.
ListItem {
    id: row
    property var episode: ({})
    property var podcast: ({})
    property int version: 0
    // The guid (193: or the show's feed) being loaded on the page ("" = none)
    property string loadingGuid: ""
    readonly property bool trailer: episode.trailer === true
    readonly property string playKey: trailer ? (episode.key || "") : (episode.guid || "")
    // (153) Detailed: a larger cover and two lines of the episode's
    // description (else the podcast's), as the other Discover rows (152)
    property bool detailed: false
    readonly property string plainDescription: {
        if (!detailed) {
            return ""
        }
        var text = (episode.description || "").length > 0 ? episode.description : (podcast.description || "")
        return text.replace(/<[^>]*>/g, " ").replace(/&nbsp;/g, " ").replace(/&amp;/g, "&").replace(/\s+/g, " ").trim()
    }
    signal play()

    readonly property bool known: (podcast.url || "").length > 0 || (trailer && (episode.key || "").length > 0)
    readonly property string feedUrl: podcast.url || (trailer ? episode.key || "" : "")
    readonly property string showTitle: podcast.title || episode.title || ""
    readonly property bool subscribed: version >= 0 && known && core.isSubscribed(feedUrl)

    width: parent ? parent.width : 0
    contentHeight: detailed ? Math.max(cover.height, textColumn.height) + 2 * Theme.paddingMedium
                            : Theme.itemSizeMedium
    enabled: known
    menu: known && !subscribed ? hideMenu : null

    onClicked: {
        var id = subscribed ? core.podcastIdForUrl(feedUrl) : -1
        if (id > 0) {
            pageStack.push(Qt.resolvedUrl("pages/EpisodesPage.qml"), { podcastId: id })
        } else {
            pageStack.push(Qt.resolvedUrl("pages/AddPodcastDialog.qml"), { initialUrl: feedUrl })
        }
    }

    Component {
        id: hideMenu
        ContextMenu {
            MenuItem {
                text: qsTr("Not interested")
                onClicked: core.hideSuggestion(row.feedUrl, row.showTitle, row.podcast.author || row.episode.author || "")
            }
        }
    }

    CoverImage {
        id: cover
        x: Theme.horizontalPageMargin
        anchors.verticalCenter: parent.verticalCenter
        width: row.detailed ? Theme.itemSizeExtraLarge * 0.8 : Theme.itemSizeMedium - 2 * Theme.paddingSmall
        height: width
        // The podcast's cover, else the episode's picture
        source: row.podcast.imageUrl || row.episode.imageUrl || ""
        fallbackText: row.showTitle || row.episode.episodeTitle || ""
    }

    Column {
        id: textColumn
        anchors {
            left: cover.right
            leftMargin: Theme.paddingMedium
            right: playButton.left
            rightMargin: Theme.paddingSmall
            verticalCenter: parent.verticalCenter
        }
        Label {
            width: parent.width
            text: row.known ? row.showTitle : (row.episode.episodeTitle || "")
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
                if ((row.podcast.match || "").length > 0) {
                    parts.push(qsTr("Fits: %1").arg(row.podcast.match))
                } else if ((row.podcast.category || "").length > 0) {
                    // (197) Otherwise its main category
                    parts.push(row.podcast.category)
                }
                if (row.trailer) {
                    var length = TimeFormat.length(row.episode.duration, qsTr("%1 h %2 min"), qsTr("%1 min"))
                    parts.push(length.length > 0 ? qsTr("Trailer, %1").arg(length) : qsTr("Trailer"))
                } else {
                    if (row.known) {
                        parts.push(row.episode.episodeTitle || "")
                    }
                    if ((row.episode.duration || 0) > 0) {
                        parts.push(TimeFormat.length(row.episode.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                    }
                }
                if (row.subscribed) {
                    parts.push(qsTr("subscribed"))
                }
                return parts.join(" · ")
            }
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeExtraSmall
            color: (row.podcast.match || "").length > 0
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

    RowPlayButton {
        id: playButton
        anchors.right: parent.right
        anchors.rightMargin: Theme.horizontalPageMargin - playButton.inset
        anchors.verticalCenter: parent.verticalCenter
        icon.source: "image://theme/icon-m-play"
        visible: row.known && row.playKey.length > 0 && row.loadingGuid !== row.playKey
        enabled: row.loadingGuid === ""
        onClicked: row.play()
    }
    BusyIndicator {
        anchors.centerIn: playButton
        size: BusyIndicatorSize.Small
        running: row.loadingGuid.length > 0 && row.loadingGuid === row.playKey
        visible: running
    }
}

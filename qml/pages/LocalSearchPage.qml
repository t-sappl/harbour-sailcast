// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// Search on the device (85; its own page since 99 - "Suchen" in the pulleys
// of New episodes and Playlist): the own podcasts (title, author), episode
// titles over all podcasts and bookmarks (name, note, episode), as sections
// in one list (sections in a list header would grow upwards on Qt 5.6, 24).
// Show notes are not searched. The last row leads to "Add podcast" with the
// same term (Podcast Index) - adding new podcasts is that page's job.
Page {
    id: page
    objectName: "localSearchPage"

    property string searchText: ""
    // Own data found: {podcasts, episodes, bookmarks}
    property var localResults: ({})
    property string localTerm: ""

    // [{kind, data}] - "section" (data = title), "podcast", "episode",
    // "bookmark", "more" (the row to the Podcast Index search)
    readonly property var rows: buildRows(localResults, localTerm)

    function buildRows(local, term) {
        var list = []
        var parts = [["podcast", local.podcasts, qsTr("My podcasts")],
                     ["episode", local.episodes, qsTr("Episodes")],
                     ["bookmark", local.bookmarks, qsTr("Bookmarks")]]
        for (var i = 0; i < parts.length; i++) {
            var items = parts[i][1] || []
            if (items.length > 0) {
                list.push({ kind: "section", data: parts[i][2] })
                for (var j = 0; j < items.length; j++) {
                    list.push({ kind: parts[i][0], data: items[j] })
                }
            }
        }
        if (term.length > 0) {
            list.push({ kind: "more", data: term })
        }
        return list
    }

    function search() {
        var term = searchText.trim()
        localResults = term.length >= 2 ? core.searchLocal(term) : ({})
        localTerm = term.length >= 2 ? term : ""
    }

    // Searches once typing has paused (the own data answer at once)
    Timer {
        id: searchTimer
        interval: 400
        onTriggered: page.search()
    }

    Column {
        id: topColumn
        width: parent.width - appWindow.playerPanelWidth

        PageHeader {
            title: qsTr("Search")
            description: qsTr("Podcasts, episodes and bookmarks on the device")
        }

        SearchField {
            id: searchField
            width: parent.width
            placeholderText: qsTr("Podcast, episode or bookmark")
            focus: true
            inputMethodHints: Qt.ImhNoPredictiveText
            EnterKey.iconSource: "image://theme/icon-m-enter-close"
            EnterKey.onClicked: {
                searchTimer.stop()
                page.search()
                focus = false
            }
            onTextChanged: {
                page.searchText = text
                searchTimer.restart()
            }
        }

        // How this page works - while the field is empty
        Label {
            visible: page.searchText.trim().length === 0
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
            text: qsTr("Finds your podcasts (title, author), episode titles and bookmarks (name, note). Show notes are not searched. New podcasts: \"Search / add podcast\" on the Podcasts page.")
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
                sourceComponent: modelData.kind === "section" ? sectionRow
                               : modelData.kind === "podcast" ? podcastRow
                               : modelData.kind === "episode" ? episodeRow
                               : modelData.kind === "bookmark" ? bookmarkRow
                               : moreRow
            }

            Component {
                id: sectionRow
                SectionHeader {
                    width: list.width
                    text: rowItem.entry
                }
            }

            // Own podcast (85) -> podcast page
            Component {
                id: podcastRow
                BackgroundItem {
                    id: podcastItem
                    width: list.width
                    height: Theme.itemSizeLarge
                    onClicked: pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: rowItem.entry.podcastId })
                    CoverImage {
                        id: podcastCover
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        height: width
                        source: rowItem.entry.coverUrl
                        fallbackText: rowItem.entry.title
                    }
                    Column {
                        anchors {
                            left: podcastCover.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            text: rowItem.entry.title
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeSmall
                            color: podcastItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            visible: text.length > 0
                            width: parent.width
                            text: rowItem.entry.author
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: podcastItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                    }
                }
            }

            // Own episode (85) -> episode page
            Component {
                id: episodeRow
                BackgroundItem {
                    id: episodeItem
                    width: list.width
                    height: Math.max(Theme.itemSizeLarge, episodeColumn.height + 2 * Theme.paddingMedium)
                    onClicked: pageStack.push(Qt.resolvedUrl("EpisodePage.qml"), { episodeId: rowItem.entry.episodeId })
                    CoverImage {
                        id: episodeCover
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        height: width
                        source: rowItem.entry.imageUrl
                        fallbackText: rowItem.entry.podcastTitle
                    }
                    Column {
                        id: episodeColumn
                        anchors {
                            left: episodeCover.right
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
                            color: episodeItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: episodeItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                            text: {
                                var parts = [rowItem.entry.podcastTitle, TimeFormat.day(rowItem.entry.published, qsTr("today"), qsTr("yesterday"))]
                                if (rowItem.entry.played) {
                                    parts.push(qsTr("played"))
                                }
                                return parts.join(" · ")
                            }
                        }
                    }
                }
            }

            // Bookmark (85) -> plays from there, as on the bookmarks page
            Component {
                id: bookmarkRow
                BackgroundItem {
                    id: bookmarkItem
                    width: list.width
                    height: Math.max(Theme.itemSizeMedium, bookmarkColumn.height + 2 * Theme.paddingMedium)
                    onClicked: appWindow.player.playFrom(rowItem.entry.episodeId, rowItem.entry.position)
                    Column {
                        id: bookmarkColumn
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeSmall
                            color: bookmarkItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                            text: "\u2605 " + ((rowItem.entry.title || "").length > 0 ? rowItem.entry.title : qsTr("Bookmark"))
                                  + " · " + TimeFormat.clock(rowItem.entry.position)
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: bookmarkItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                            text: rowItem.entry.episodeTitle + " · " + rowItem.entry.podcastTitle
                        }
                        Label {
                            visible: (rowItem.entry.note || "").length > 0
                            width: parent.width
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryHighlightColor
                            text: rowItem.entry.note || ""
                        }
                    }
                }
            }

            // To the Podcast Index search with the same term (99)
            Component {
                id: moreRow
                BackgroundItem {
                    id: moreItem
                    width: list.width
                    height: Theme.itemSizeMedium
                    onClicked: pageStack.push(Qt.resolvedUrl("SearchPage.qml"), { initialText: rowItem.entry })
                    Image {
                        id: moreIcon
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        source: "image://theme/icon-m-search?" + (moreItem.highlighted ? Theme.highlightColor : Theme.primaryColor)
                    }
                    Label {
                        anchors {
                            left: moreIcon.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        truncationMode: TruncationMode.Fade
                        text: qsTr("Search for a new podcast: \"%1\"").arg(rowItem.entry)
                        color: moreItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }
        }

        // Only "search for a new podcast" left: nothing found here
        ViewPlaceholder {
            enabled: page.localTerm.length > 0 && page.rows.length <= 1
            text: qsTr("Nothing found on the device")
            hintText: qsTr("Search for a new podcast below")
            verticalOffset: -Theme.itemSizeLarge
        }

        VerticalScrollDecorator {}
    }
}

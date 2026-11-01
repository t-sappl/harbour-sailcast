// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../TimeFormat.js" as TimeFormat
import harbour.sailcast 1.0
import "../"

// The episodes of one podcast, newest first, with the podcast's cover and
// description at the top. "Only unplayed" is remembered.
Page {
    id: page
    objectName: "episodesPage"
    property int podcastId: -1
    property var podcast: core.podcast(podcastId)

    function openSimilar() {
        pageStack.push(Qt.resolvedUrl("SimilarPage.qml"), {
                           feedUrl: page.podcast.url || "",
                           podcastTitle: page.podcast.feedTitle || page.podcast.title || "",
                           podcastAuthor: page.podcast.author || "",
                           website: page.podcast.link || "",
                           categories: page.podcast.categories || [] })
    }
    property bool unplayedOnly: core.setting("ui.unplayedOnly", "0") === "1"
    // Oldest first for serials (53, podcast settings) and the search in the
    // episode titles (53; the field sits in the list header, so its text
    // goes through this property - see 39)
    property bool oldestFirst: core.setting("podcast." + podcastId + ".oldestFirst", "0") === "1"
    property bool searching: false
    property string searchText: ""
    // Opening the page clears the "new episodes" marker on the podcast (50)
    Component.onCompleted: core.markPodcastSeen(podcastId)

    readonly property bool archiving: core.archivingPodcastId === podcastId

    Connections {
        target: core
        onDataChanged: page.podcast = core.podcast(page.podcastId)
    }

    // Expanding the description must not move the page: a growing list
    // header grows upwards on Qt 5.6 (the rows below stay in place), so the
    // top of the page jumped away (23). The header's top is kept where it
    // was on screen while the header changes its height.
    property bool descriptionExpanded: false
    // (158) A long list (more than 50 episodes, not while searching):
    // sections (seasons or months/years), quick scroll, the first 50 and
    // "Ältere Folgen anzeigen"
    readonly property bool longList: (podcast.episodeCount || 0) > 50 && !searching
    property bool showAll: false
    property real _keepTopAt: -1

    function toggleDescription() {
        _keepTopAt = list.headerItem ? list.headerItem.mapToItem(list, 0, 0).y : -1
        keepTopTimer.restart()
        descriptionExpanded = !descriptionExpanded
    }

    function _restoreTop() {
        if (_keepTopAt < 0 || !list.headerItem) {
            return
        }
        var delta = list.headerItem.mapToItem(list, 0, 0).y - _keepTopAt
        if (delta !== 0) {
            list.contentY = Math.max(list.originY, list.contentY + delta)
        }
    }

    // After the list has laid out the changed header
    Timer {
        id: restoreTopTimer
        interval: 30
        onTriggered: page._restoreTop()
    }

    Timer {
        id: keepTopTimer
        interval: 500
        onTriggered: page._keepTopAt = -1
    }

    EpisodeListModel {
        id: episodes
        podcastId: page.podcastId
        filter: page.unplayedOnly ? EpisodeListModel.Unplayed : EpisodeListModel.All
        oldestFirst: page.oldestFirst
        search: page.searching ? page.searchText : ""
        sections: page.longList
        limit: page.longList && !page.showAll ? 50 : -1
    }

    // The sort order may have changed on the podcast settings page
    onStatusChanged: {
        if (status === PageStatus.Active) {
            oldestFirst = core.setting("podcast." + podcastId + ".oldestFirst", "0") === "1"
        }
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: episodes
        // (158) Long lists: grab on the right edge to jump
        quickScroll: page.longList
        footer: Column {
            width: list.width
            BackgroundItem {
                width: list.width
                height: episodes.hasMore ? Theme.itemSizeMedium : 0
                visible: episodes.hasMore
                onClicked: page.showAll = true
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignHCenter
                    text: (page.oldestFirst ? qsTr("Show newer episodes (%1)") : qsTr("Show older episodes (%1)")).arg(Math.max(0,
                              (page.unplayedOnly ? (page.podcast.unplayed || 0) : (page.podcast.episodeCount || 0)) - episodes.count))
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }
            // (233) No episodes: a line below the header instead of a
            // placeholder in the middle (it overlapped the expanded
            // header); says why when the feed could not be loaded
            // No "padding" on Text in Qt 5.6: a spacer instead
            Item {
                visible: episodes.count === 0
                width: 1
                height: Theme.paddingLarge * 2
            }
            Label {
                visible: episodes.count === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                color: Theme.secondaryHighlightColor
                text: page.unplayedOnly ? qsTr("No unplayed episodes – pull down to show all episodes")
                    : ((page.podcast.lastError || "").length > 0
                       ? qsTr("The feed could not be loaded (see above) – pull down → Update to try again")
                       : (core.updating ? qsTr("Loading the episodes …")
                                        : qsTr("No episodes – pull down → Update")))
            }
        }
        onOriginYChanged: page._restoreTop()
        onContentHeightChanged: page._restoreTop()
        // No current item (83): ListView keeps item 0 in view while the
        // header grows, which pushed the page header out of view
        currentIndex: -1

        // Entries with a countdown at the top (60): the farthest
        // pull, so a long pulley is not stopped on them by mistake
        PullDownMenu {
            busy: core.updating || page.archiving
            // (143) Only what is used often; unsubscribe, login, support,
            // save in Music and "download all" are on the podcast settings
            // page, "similar podcasts" is the line next to the cover
            // (165) The whole podcast into the playlist, oldest first
            MenuItem {
                visible: (page.podcast.unplayed || 0) > 0
                text: qsTr("Add unplayed to the playlist")
                onClicked: core.queuePodcast(page.podcastId)
            }
            MenuItem {
                // (155) Not for a podcast listened to without subscribing
                // (170) Also for marking played ones as unplayed; opens with
                // the played ones when nothing is unplayed
                visible: (page.podcast.episodeCount || 0) > 0 && page.podcast.provisional !== true
                // (167) A choice of episodes (all, all but the newest, single
                // ones, older ones) instead of all at once with a countdown
                text: qsTr("Mark as played / unplayed …")
                onClicked: pageStack.push(Qt.resolvedUrl("MarkPlayedDialog.qml"),
                                          { podcastId: page.podcastId, podcastTitle: page.podcast.title || "",
                                            unplayMode: (page.podcast.unplayed || 0) === 0 })
            }
            MenuItem {
                // (155) Its settings (unsubscribe, downloads, archive …) only
                // for a subscription - "Abonnieren" instead
                visible: page.podcast.provisional !== true
                text: qsTr("Podcast settings")
                onClicked: pageStack.push(Qt.resolvedUrl("PodcastSettingsPage.qml"),
                                          { podcastId: page.podcastId, podcastTitle: page.podcast.title || "" })
            }
            MenuItem {
                text: page.searching ? qsTr("End search") : qsTr("Search episodes")
                onClicked: {
                    page.searching = !page.searching
                    if (!page.searching) {
                        page.searchText = ""
                    }
                }
            }
            MenuItem {
                text: page.unplayedOnly ? qsTr("Show all episodes") : qsTr("Show only unplayed")
                onClicked: {
                    page.unplayedOnly = !page.unplayedOnly
                    core.setSetting("ui.unplayedOnly", page.unplayedOnly ? "1" : "0")
                }
            }
            MenuItem {
                text: qsTr("Update")
                enabled: !core.updating
                onClicked: core.updatePodcast(page.podcastId)
            }
            // Listened to without subscribing (147)
            MenuItem {
                visible: page.podcast.provisional === true
                text: qsTr("Subscribe")
                onClicked: appWindow.subscribeWithFolder(page.podcast.url || "", page.podcast.title || "")
            }
        }

        header: Column {
            width: list.width
            spacing: Theme.paddingMedium
            onHeightChanged: restoreTopTimer.restart()

            PageHeader {
                title: page.podcast.title || ""
                description: page.archiving ? qsTr("Archiving …") : (page.podcast.author || "")
            }

            // Search in the episode titles (53)
            SearchField {
                visible: page.searching
                width: parent.width
                placeholderText: qsTr("Search episodes")
                inputMethodHints: Qt.ImhNoPredictiveText
                focus: page.searching
                onTextChanged: page.searchText = text
                onVisibleChanged: if (!visible) text = ""
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            Row {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingLarge

                CoverImage {
                    id: cover
                    width: Theme.itemSizeExtraLarge
                    height: width
                    source: page.podcast.coverUrl || ""
                    fallbackText: page.podcast.title || ""
                }

                Column {
                    width: parent.width - cover.width - parent.spacing
                    spacing: Theme.paddingSmall
                    Label {
                        width: parent.width
                        text: qsTr("%n unplayed", "", page.podcast.unplayed || 0) + " · "
                              + qsTr("%n episodes", "", page.podcast.episodeCount || 0)
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                    }
                    Label {
                        visible: (page.podcast.lastError || "").length > 0
                        width: parent.width
                        text: page.podcast.lastError || ""
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.errorColor
                    }
                    Label {
                        visible: page.podcast.archived === true
                        width: parent.width
                        wrapMode: Text.Wrap
                        text: qsTr("Archived - not updated any more")
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        visible: (page.podcast.archiveFolder || "").length > 0
                        width: parent.width
                        text: qsTr("Archive folder: %1").arg(page.podcast.archiveFolder || "")
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        visible: (page.podcast.authUser || "").length > 0
                        width: parent.width
                        text: qsTr("Login: %1").arg(page.podcast.authUser || "")
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryHighlightColor
                    }
                    // "Ähnliche Podcasts ›" (141, in the free space beside the
                    // cover - the header does not grow, nothing jumps); since
                    // 144 at the bottom of the cover (Thomas: looks better
                    // further down) - the spacer fills what is left, the
                    // line moves below an error message if there is no room
                    Item {
                        width: 1
                        height: Math.max(0, cover.height - y - parent.spacing - similarItem.height)
                    }
                    BackgroundItem {
                        id: similarItem
                        width: parent.width
                        height: similarLabel.height + 2 * Theme.paddingSmall
                        onClicked: page.openSimilar()
                        Label {
                            id: similarLabel
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Similar podcasts") + " \u203a"
                            font.pixelSize: Theme.fontSizeSmall
                            color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                        }
                    }
                }
            }

            // Not subscribed (147, "Anhören ohne Abo"): say so, offer it
            Column {
                visible: page.podcast.provisional === true
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Not subscribed - you can listen to the episodes. Without a subscription the podcast disappears after 30 days without listening.")
                }
                Button {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: qsTr("Subscribe")
                    onClicked: appWindow.subscribeWithFolder(page.podcast.url || "", page.podcast.title || "")
                }
                Item { width: 1; height: Theme.paddingSmall }
            }

            // Description, expandable
            Label {
                id: description
                property bool expanded: page.descriptionExpanded
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                visible: text.length > 0
                text: page.podcast.description || ""
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                maximumLineCount: expanded ? 1000 : 3
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                linkColor: Theme.highlightColor
                onLinkActivated: Qt.openUrlExternally(link)
                MouseArea {
                    anchors.fill: parent
                    onClicked: page.toggleDescription()
                }
            }

            // (237) Visible that there is more: statistics and "About this
            // podcast" open with the description (tapping it was not obvious)
            BackgroundItem {
                visible: (page.podcast.description || "").length > 0
                width: parent.width
                height: Theme.itemSizeExtraSmall
                onClicked: page.toggleDescription()
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: page.descriptionExpanded ? qsTr("Less ⌃") : qsTr("More: statistics and info ⌄")
                }
            }

            // "Über diesen Podcast" (158): what "Add podcast" shows, when the
            // description is opened (or there is none); since 162 with
            // statistics on top and a calmer feed address and buttons
            Column {
                id: aboutColumn
                visible: page.descriptionExpanded || (page.podcast.description || "").length === 0
                width: parent.width
                readonly property var stats: visible ? core.podcastStats(page.podcastId) : ({})

                SectionHeader { text: qsTr("Statistics") }
                StatTiles {
                    tiles: {
                        var s = aboutColumn.stats
                        var tiles = []
                        var hours = (s.listened || 0) / 3600
                        tiles.push({ value: hours >= 10 ? qsTr("%1 h").arg(Math.round(hours))
                                                       : qsTr("%1 h").arg(Number(hours).toLocaleString(Qt.locale(), "f", 1)),
                                     label: qsTr("listened") })
                        tiles.push({ value: "" + (s.started || 0), label: qsTr("episodes started") })
                        tiles.push({ value: (s.bytes || 0) > 0 ? TimeFormat.size(s.bytes, qsTr("%1 MB"), qsTr("%1 GB")) : "0",
                                     label: qsTr("%n downloads", "", s.downloads || 0) })
                        if ((s.rhythm || "").length > 0) {
                            tiles.push({ value: s.rhythm, label: qsTr("publishing rhythm") })
                        }
                        if ((s.next || 0) > 0) {
                            tiles.push({ value: Qt.formatDate(new Date(s.next), qsTr("d MMM")),
                                         label: qsTr("expected next episode") })
                        }
                        return tiles
                    }
                }

                SectionHeader { text: qsTr("About this podcast") }
                DetailItem {
                    readonly property var labels: core.categoryLabels(page.podcast.categories || [])
                    visible: labels.length > 0
                    label: qsTr("Categories")
                    value: labels.join(", ")
                }
                DetailItem {
                    visible: (page.podcast.language || "").length > 0
                    label: qsTr("Language")
                    value: visible ? core.languageName(page.podcast.language) : ""
                }
                DetailItem {
                    label: qsTr("Episodes")
                    value: "" + (page.podcast.episodeCount || 0)
                }
                DetailItem {
                    visible: (page.podcast.newest || 0) > 0
                    label: qsTr("Newest episode")
                    value: visible ? Qt.formatDate(new Date(page.podcast.newest), Qt.DefaultLocaleShortDate) : ""
                }

                // The feed address on its own lines (162: in a DetailItem it
                // was squeezed into the right half)
                Item { width: 1; height: Theme.paddingMedium }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Feed address")
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.WrapAnywhere
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.highlightColor
                    text: core.maskUrl(page.podcast.url || "")
                }

                Item { width: 1; height: Theme.paddingLarge }
                // (192) Centred, equally wide (one alone: half width)
                Row {
                    id: linkButtons
                    anchors.horizontalCenter: parent.horizontalCenter
                    spacing: Theme.paddingMedium
                    readonly property real cell: (parent.width - 2 * Theme.horizontalPageMargin - spacing) / 2
                    Button {
                        width: linkButtons.cell
                        visible: (page.podcast.link || "").length > 0
                        text: qsTr("Website")
                        onClicked: Qt.openUrlExternally(page.podcast.link)
                    }
                    Button {
                        width: linkButtons.cell
                        visible: (page.podcast.funding || []).length > 0
                        text: qsTr("Support")
                        onClicked: Qt.openUrlExternally(page.podcast.funding[0].url)
                    }
                }
                Item { width: 1; height: Theme.paddingLarge * 2 }
            }

            Item { width: 1; height: Theme.paddingSmall }
        }

        // (161) The group header is part of the first row of its group, not
        // a ListView section: a context menu on a section header did not
        // move the rows below it (they lay under the open menu, 159)
        delegate: Column {
            id: rowColumn
            width: list.width
            ListItem {
                id: groupItem
                visible: page.longList && model.sectionStart === true
                width: parent.width
                contentHeight: Theme.itemSizeExtraSmall
                menu: page.podcast.provisional === true ? null : groupMenu
                onClicked: if (menu) openMenu()
                // (194) A plain section header, as in Sailfish's own lists: the
                // arrow of 192 promised folding, but a tap opens the menu
                // (" …" before it read as cut-off text)
                SectionHeader {
                    id: groupHeader
                    anchors.verticalCenter: parent.verticalCenter
                    text: model.section || ""
                }
                Component {
                    id: groupMenu
                    ContextMenu {
                        MenuItem {
                            text: qsTr("Download this group")
                            onClicked: appWindow.requestSectionDownload(page.podcastId, model.section)
                        }
                        MenuItem {
                            text: qsTr("Add unplayed to the playlist")
                            onClicked: core.queueSection(page.podcastId, model.section)
                        }
                        MenuItem {
                            text: qsTr("Mark this group as played")
                            onClicked: appWindow.remorseMarkSectionPlayed(page.podcastId, model.section)
                        }
                    }
                }
            }
            EpisodeDelegate {
                width: parent.width
                showPodcastTitle: false
            }
        }


        VerticalScrollDecorator {}
    }
}

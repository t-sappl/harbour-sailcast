// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// The episodes of one podcast, newest first, with the podcast's cover and
// description at the top. "Only unplayed" is remembered.
Page {
    id: page
    objectName: "episodesPage"
    property int podcastId: -1
    property var podcast: core.podcast(podcastId)
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
    }

    // The sort order may have changed on the podcast settings page
    onStatusChanged: {
        if (status === PageStatus.Active) {
            oldestFirst = core.setting("podcast." + podcastId + ".oldestFirst", "0") === "1"
        }
    }

    RemorsePopup {
        id: remorse
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: episodes
        onOriginYChanged: page._restoreTop()
        onContentHeightChanged: page._restoreTop()

        // Entries with a countdown at the top (60): the farthest
        // pull, so a long pulley is not stopped on them by mistake
        PullDownMenu {
            busy: core.updating || page.archiving
            MenuItem {
                text: qsTr("Unsubscribe")
                onClicked: {
                    var id = page.podcastId
                    remorse.execute(qsTr("Unsubscribing"), function() {
                        // Back first, the page and its list must not be
                        // in the way while the podcast disappears
                        pageStack.pop()
                        appWindow.unsubscribe(id)
                    })
                }
            }
            MenuItem {
                visible: (page.podcast.unplayed || 0) > 0
                text: qsTr("Mark all as played")
                // With a countdown (53) - a mass action
                onClicked: appWindow.remorseMarkAllPlayed(page.podcastId, page.podcast.title || "")
            }
            // Only offered when the feed has a login or asked for one
            MenuItem {
                visible: (page.podcast.authUser || "").length > 0
                         || /HTTP 40[13]/.test(page.podcast.lastError || "")
                text: (page.podcast.authUser || "").length > 0 ? qsTr("Change login") : qsTr("Login")
                onClicked: {
                    var dialog = pageStack.push(Qt.resolvedUrl("FeedLoginDialog.qml"),
                                                { podcastId: page.podcastId, podcastTitle: page.podcast.title || "",
                                                  user: page.podcast.authUser || "" })
                    dialog.accepted.connect(function() {
                        appWindow.setFeedLogin(page.podcastId, dialog.user.trim(), dialog.password)
                        core.updatePodcast(page.podcastId)
                    })
                }
            }
            MenuItem {
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
                }
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

            Item { width: 1; height: Theme.paddingSmall }
        }

        delegate: EpisodeDelegate {
            showPodcastTitle: false
        }

        ViewPlaceholder {
            enabled: episodes.count === 0
            text: page.unplayedOnly ? qsTr("No unplayed episodes") : qsTr("No episodes")
            hintText: page.unplayedOnly ? qsTr("Pull down to show all episodes") : qsTr("Pull down to update")
        }

        VerticalScrollDecorator {}
    }
}

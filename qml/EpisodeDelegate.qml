// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// One episode row in all lists (start page, podcast, playlist):
//   [image]  Title (up to 2 lines)                        [play]
//            Podcast · date
//            ======-----  33 min left · 21 chapters · [downloaded]
// The image is the episode's own image, else the podcast cover. Tap: episode
// page, play button: play/pause directly, long press: context menu.
// Uses the roles of EpisodeListModel. The episode playing right now shows
// its live progress.
//
// dragHandle (playlist): three lines on the left; dragging them sorts the
// list (as the favourites in Sailwave) - the page does the moving through
// dragStarted/dragMoved/dragEnded (y in the coordinates of the list view).
ListItem {
    id: item

    property bool showPodcastTitle: true
    // In the playlist: "Remove from playlist" instead of "Add"
    property bool inPlaylist: false
    property bool dragHandle: false
    property bool dragged: false
    // Started from here, the episode plays in the playlist context (the
    // next playlist episode follows, see PlayerController) - playlist page
    property bool playlistContext: false
    // False: no context menu (rows in a list header, see PlaylistPage)
    property bool menuEnabled: true

    signal dragStarted(real viewY)
    signal dragMoved(real viewY)
    signal dragEnded()

    readonly property bool isCurrent: appWindow.player.episodeId === model.episodeId
    readonly property bool isPlaying: isCurrent && appWindow.player.playing

    // Live values for the episode playing now, else the stored ones
    readonly property bool liveProgress: isCurrent && appWindow.player.duration > 0 && appWindow.player.position > 0
    readonly property bool showProgress: liveProgress || model.inProgress
    readonly property real progress: liveProgress ? Math.min(1, appWindow.player.position / appWindow.player.duration)
                                                  : model.progress
    readonly property int remaining: liveProgress ? appWindow.player.duration - appWindow.player.position
                                                  : model.remaining

    highlighted: down || menuOpen || dragged

    contentHeight: topArea.height + (chaptersExpanded ? chapterColumn.height : 0)

    onClicked: pageStack.push(Qt.resolvedUrl("pages/EpisodePage.qml"),
                              { episodeId: model.episodeId, fromPlaylist: item.playlistContext })

    menu: menuEnabled ? contextMenu : null

    property Component contextMenu: Component {
        ContextMenu {
            MenuItem {
                text: item.isPlaying ? qsTr("Pause") : qsTr("Play")
                onClicked: appWindow.player.playOrToggle(model.episodeId, item.playlistContext)
            }
            MenuItem {
                visible: !item.inPlaylist
                text: qsTr("Play next")
                onClicked: core.queueAdd(model.episodeId, true)
            }
            MenuItem {
                visible: !item.inPlaylist && !model.queued
                text: qsTr("Add to end of playlist")
                onClicked: core.queueAdd(model.episodeId, false)
            }
            MenuItem {
                visible: model.queued
                text: qsTr("Remove from playlist")
                onClicked: core.queueRemove(model.episodeId)
            }
            MenuItem {
                visible: !model.downloaded && !model.downloading
                text: qsTr("Download")
                onClicked: appWindow.requestDownload(model.episodeId)
            }
            MenuItem {
                visible: model.downloading
                text: qsTr("Cancel download")
                onClicked: core.cancelDownload(model.episodeId)
            }
            MenuItem {
                visible: model.downloaded
                text: qsTr("Delete download")
                // The countdown and its action live in the app window: a
                // function created in this menu loses its context when the
                // menu is destroyed - the countdown ran, the row went blank,
                // but nothing was deleted (25)
                onClicked: appWindow.remorseDeleteDownload(model.episodeId)
            }
            // Started episodes offer both: finish them or start over (22)
            MenuItem {
                visible: !model.played
                text: qsTr("Mark as played")
                onClicked: {
                    if (item.isCurrent) {
                        appWindow.player.pause()
                    }
                    core.markPlayed(model.episodeId)
                }
            }
            // Catching up in the middle of a podcast (53); countdown in the
            // app window (not in this menu)
            MenuItem {
                text: qsTr("Mark older ones as played")
                onClicked: appWindow.remorseMarkOlderPlayed(model.episodeId)
            }
            MenuItem {
                visible: model.played || model.inProgress
                text: qsTr("Mark as unplayed")
                onClicked: {
                    if (item.isCurrent) {
                        appWindow.player.pause()
                        appWindow.player.seek(0)
                    }
                    core.markUnplayed(model.episodeId)
                }
            }
            // (83) Opened from the window, not from this menu (25)
            MenuItem {
                text: qsTr("Share")
                onClicked: appWindow.shareEpisode(model.episodeId, 0)
            }
        }
    }

    // Drag handle: only here a drag starts, so the list can still be
    // scrolled everywhere else
    MouseArea {
        id: handle
        visible: item.dragHandle
        width: Theme.itemSizeExtraSmall
        height: parent.height
        preventStealing: true

        Column {
            anchors.centerIn: parent
            spacing: Theme.paddingSmall
            Repeater {
                model: 3
                Rectangle {
                    width: Theme.iconSizeSmall * 0.8
                    height: Math.max(2, Math.round(Theme.paddingSmall / 2))
                    radius: height / 2
                    color: handle.pressed ? Theme.highlightColor : Theme.secondaryColor
                }
            }
        }

        onPressed: item.dragStarted(mapToItem(item.ListView.view, mouse.x, mouse.y).y)
        onPositionChanged: item.dragMoved(mapToItem(item.ListView.view, mouse.x, mouse.y).y)
        onReleased: item.dragEnded()
        onCanceled: item.dragEnded()
    }

    property bool chaptersExpanded: false
    // Inside the chapter Repeater "model" is the chapter - keep the episode
    readonly property int rowEpisodeId: model.episodeId
    property var chapterList: []
    readonly property int currentChapter: isCurrent ? appWindow.player.chapterIndex : -1

    function toggleChapters() {
        if (!chaptersExpanded) {
            chapterList = core.episode(model.episodeId).chapters || []
        }
        chaptersExpanded = !chaptersExpanded
    }

    Item {
        id: topArea
        width: parent.width
        height: Math.max(Theme.itemSizeLarge, textColumn.height + 2 * Theme.paddingMedium)

        CoverImage {
            id: art
            x: item.dragHandle ? handle.width : Theme.horizontalPageMargin
            anchors.verticalCenter: parent.verticalCenter
            width: Theme.itemSizeMedium
            height: width
            source: model.imageUrl
            fallbackText: model.podcastTitle
            opacity: model.played && !item.isCurrent ? 0.6 : 1.0

            // The episode has bookmarks (50): small star bottom right
            Rectangle {
                visible: model.bookmarkCount > 0
                anchors {
                    right: parent.right
                    bottom: parent.bottom
                    margins: Theme.paddingSmall / 2
                }
                width: Theme.iconSizeSmall
                height: width
                radius: width / 2
                color: Theme.rgba("black", 0.55)
                Image {
                    anchors.centerIn: parent
                    width: Theme.iconSizeExtraSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    source: "image://theme/icon-m-favorite-selected?" + Theme.highlightColor
                }
            }
        }

        Column {
            id: textColumn
            anchors {
                left: art.right
                leftMargin: Theme.paddingMedium
                right: playButton.left
                verticalCenter: parent.verticalCenter
            }
            spacing: Theme.paddingSmall / 2

            Label {
                width: parent.width
                text: model.title
                wrapMode: Text.Wrap
                maximumLineCount: 2
                elide: Text.ElideRight
                font.pixelSize: Theme.fontSizeSmall
                font.bold: model.isNew && !model.played
                color: item.highlighted || item.isCurrent ? Theme.highlightColor
                                                          : (model.played ? Theme.secondaryColor : Theme.primaryColor)
            }

            // Podcast name only (33): the date moved into the status line,
            // a long podcast name faded it out
            Label {
                visible: item.showPodcastTitle
                width: parent.width
                text: model.podcastTitle
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeExtraSmall
                color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
            }

            Row {
                width: parent.width
                spacing: Theme.paddingSmall
                height: statusLabel.height

                // Listening progress (only once started)
                Rectangle {
                    id: progressTrack
                    visible: item.showProgress
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    height: Math.max(2, Theme.paddingSmall / 2)
                    radius: height / 2
                    color: Theme.rgba(Theme.secondaryColor, 0.3)
                    Rectangle {
                        width: parent.width * item.progress
                        height: parent.height
                        radius: parent.radius
                        color: Theme.highlightColor
                    }
                }

                Label {
                    id: statusLabel
                    text: {
                        var parts = [TimeFormat.day(model.published, qsTr("today"), qsTr("yesterday"))]
                        // Season / episode number from the feed (83)
                        if (model.episodeNumber > 0) {
                            parts.unshift(model.season > 0 ? qsTr("S%1 E%2").arg(model.season).arg(model.episodeNumber)
                                                           : qsTr("#%1").arg(model.episodeNumber))
                        }
                        if (model.played) {
                            parts.push(qsTr("played"))
                        } else if (item.showProgress && item.remaining > 0) {
                            parts.push(qsTr("%1 left").arg(TimeFormat.length(item.remaining, qsTr("%1 h %2 min"), qsTr("%1 min"))))
                        } else if (model.duration > 0) {
                            parts.push(TimeFormat.length(model.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                        }
                        if (model.queued && !item.inPlaylist) {
                            parts.push(qsTr("in playlist"))
                        }
                        return parts.join(" · ")
                    }
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                    truncationMode: TruncationMode.Fade
                    width: Math.min(implicitWidth, parent.width - (progressTrack.visible ? progressTrack.width + parent.spacing : 0)
                                    - (downloadIcon.visible ? downloadIcon.width + parent.spacing : 0))
                }

                Image {
                    id: downloadIcon
                    visible: model.downloaded
                    anchors.verticalCenter: parent.verticalCenter
                    source: "image://theme/icon-s-cloud-download?" + Theme.secondaryHighlightColor
                    width: Theme.iconSizeExtraSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                }
            }

            // Own line "› 21 Kapitel" (28): tap unfolds the chapters in the
            // list. Its own MouseArea, so the tap does not open the page.
            MouseArea {
                id: chapterTrigger
                visible: model.chapterCount > 0
                width: chevron.width + chapterLabel.width + Theme.paddingLarge
                height: Math.max(chapterLabel.height, Theme.iconSizeExtraSmall) + Theme.paddingSmall
                onClicked: item.toggleChapters()
                // First row with chapters: how to unfold them (52)
                Component.onCompleted: {
                    if (visible) {
                        appWindow.showHint("chapters", qsTr("Tap \"› N chapters\" to unfold the chapters in the list"), item)
                    }
                }

                Image {
                    id: chevron
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSizeExtraSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    source: "image://theme/icon-m-right?" + (chapterTrigger.pressed ? Theme.highlightColor : Theme.secondaryHighlightColor)
                    rotation: item.chaptersExpanded ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: 150 } }
                }
                Label {
                    id: chapterLabel
                    anchors {
                        left: chevron.right
                        verticalCenter: parent.verticalCenter
                    }
                    text: qsTr("%n chapters", "", model.chapterCount)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: chapterTrigger.pressed ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }
        }

        IconButton {
            id: playButton
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium
                verticalCenter: parent.verticalCenter
            }
            icon.source: item.isPlaying ? "image://theme/icon-m-pause" : "image://theme/icon-m-play"
            highlighted: item.isCurrent || down
            onClicked: appWindow.player.playOrToggle(model.episodeId, item.playlistContext)
        }
    }

    // Line on the left: the chapters belong to the episode above
    Rectangle {
        visible: item.chaptersExpanded
        anchors {
            top: chapterColumn.top
            bottom: chapterColumn.bottom
            bottomMargin: Theme.paddingMedium
        }
        x: chapterColumn.x - Theme.paddingMedium
        width: Math.max(1, Math.round(Theme.paddingSmall / 3))
        color: Theme.rgba(Theme.secondaryHighlightColor, 0.5)
    }

    // The chapters, unfolded under the episode (indented below the text)
    Column {
        id: chapterColumn
        visible: item.chaptersExpanded
        anchors.top: topArea.bottom
        x: textColumn.x + Theme.paddingLarge
        width: parent.width - x - Theme.horizontalPageMargin

        Repeater {
            model: item.chaptersExpanded ? item.chapterList : []
            delegate: BackgroundItem {
                readonly property bool current: index === item.currentChapter
                width: chapterColumn.width
                // Long titles wrap (30)
                height: Math.max(Theme.itemSizeExtraSmall, chapterTitle.height + 2 * Theme.paddingSmall)
                highlighted: down || current
                onClicked: appWindow.player.playFrom(item.rowEpisodeId, modelData.start / 1000, item.playlistContext)

                Label {
                    id: chapterTime
                    anchors.baseline: chapterTitle.baseline
                    width: Theme.itemSizeSmall
                    text: TimeFormat.clock(modelData.start / 1000)
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryColor
                }
                Label {
                    id: chapterTitle
                    anchors {
                        left: chapterTime.right
                        leftMargin: Theme.paddingSmall
                        right: parent.right
                        verticalCenter: parent.verticalCenter
                    }
                    text: modelData.title
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }
        }

        Item { width: 1; height: Theme.paddingSmall }
    }

    // Download running: thin bar along the bottom edge
    Rectangle {
        visible: model.downloading
        anchors.bottom: parent.bottom
        x: art.x
        height: Math.max(2, Theme.paddingSmall / 2)
        width: (parent.width - 2 * Theme.horizontalPageMargin) * Math.max(0.02, model.downloadProgress)
        color: Theme.highlightColor
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// One episode row in all lists (start page, podcast, playlist):
//   [image]  Title (up to 2 lines)                  [≡+]  [play]
//            Podcast · date
//            ======-----  33 min left · 21 chapters · [downloaded]
// The image is the episode's own image, else the podcast cover. Tap: episode
// page, play button: play/pause directly, ≡+ (254): add to / take out of
// the playlist, long press: context menu.
// Uses the roles of EpisodeListModel. The episode playing right now shows
// its live progress.
//
// dragHandle (playlist): three lines on the left; dragging them sorts the
// list (as the favourites in Sailwave) - the page does the moving through
// dragStarted/dragMoved/dragEnded (y in the coordinates of the list view).
ListItem {
    id: item

    property bool showPodcastTitle: true
    // (259, Thomas: made no sense in the playlist) "Remove from New episodes"
    // only on the New episodes page itself
    property bool newEpisodesList: false
    // (302) New episodes: the row was set aside / left the view and is kept
    // until the view changes - dimmed, with "Undo"
    property bool setAside: false
    // (345, Thomas + Gemini 17 Nov) New episodes with the download filter: the
    // row was downloaded (or its download deleted) while the page is open and
    // stays until it is left - the left part dimmed, "· downloaded" instead
    // of the cloud; the buttons stay bright (318)
    // (345, review) not while it is being downloaded again
    readonly property bool leftFilter: model.leftFilter === true && model.downloading !== true
    // (304) "Set aside" only in the view "New" (in "All unplayed" it had
    // no visible effect - Gemini 13 Nov)
    property bool newView: false
    // (303) Downloads page: the real size of the file at the right end of
    // the status line (no cloud symbol there)
    // (309) Real file size on the right of the status line. (310) Everywhere,
    // the playlist included: a size means "on the phone", none means stream
    property bool showFileSize: true
    // (334) The "downloaded" symbol; off on the Downloads page (all are)
    property bool showDownloadIcon: true
    // (306) Grouped by podcast: the cover is in the group header, the row
    // goes without (text indented by textIndent); video and bookmarks then
    // show as small symbols in the status line
    property bool showArt: true
    property real textIndent: 0
    // In the playlist: "Remove from playlist" instead of "Add"
    property bool inPlaylist: false
    property bool dragHandle: false
    property bool dragged: false
    // Started from here, the episode plays in the playlist context (the
    // next playlist episode follows, see PlayerController) - playlist page
    property bool playlistContext: false
    // False: no context menu (rows in a list header, see PlaylistPage)
    property bool menuEnabled: true
    // (313) Selection mode (Downloads page): "Select" in the context menu
    // there; in the mode a tap on the row marks it, a long press marks the
    // range from the last marked row, the quick buttons make room for a
    // circle. The page keeps the marked ids.
    property bool selectable: false
    property bool selectMode: false
    property bool selected: false
    signal selectStart()
    signal selectToggled()
    // (320) "Mark older ones as played" in the row menu (see there)
    property bool showMarkOlder: false

    signal selectRange()
    signal selectPodcast()

    signal dragStarted(real viewY)
    signal dragMoved(real viewY)
    signal dragEnded()

    readonly property bool isCurrent: appWindow.player.episodeId === model.episodeId
    // (323) The file size of the row (empty: none shown)
    readonly property string fileSizeText: showFileSize && model.fileBytes > 0
        ? TimeFormat.size(model.fileBytes, qsTr("%1 MB"), qsTr("%1 GB")) : ""
    readonly property bool isPlaying: isCurrent && appWindow.player.playing

    // Live values for the episode playing now, else the stored ones
    readonly property bool liveProgress: isCurrent && appWindow.player.duration > 0 && appWindow.player.position > 0
    readonly property bool showProgress: liveProgress || model.inProgress
    readonly property real progress: liveProgress ? Math.min(1, appWindow.player.position / appWindow.player.duration)
                                                  : model.progress
    readonly property int remaining: liveProgress ? appWindow.player.duration - appWindow.player.position
                                                  : model.remaining

    highlighted: down || menuOpen || dragged || (selectMode && selected)

    contentHeight: topArea.height + (chaptersExpanded ? chapterColumn.height : 0)

    onClicked: {
        if (item.selectMode) {
            item.selectToggled()
            return
        }
        pageStack.push(Qt.resolvedUrl("pages/EpisodePage.qml"),
                       { episodeId: model.episodeId, fromPlaylist: item.playlistContext })
    }
    // (313) In the selection mode the long press is the range, not a menu
    onPressAndHold: if (item.selectMode) item.selectRange()

    menu: (menuEnabled && !selectMode) ? contextMenu : null

    property Component contextMenu: Component {
        // (348) The items live in EpisodeMenu.qml (same menu in the search)
        EpisodeMenu {
            ep: model
            setAsideOffered: item.newEpisodesList && item.newView
            inPlaylist: item.inPlaylist
            selectable: item.selectable
            showMarkOlder: item.showMarkOlder
            showPodcastTitle: item.showPodcastTitle
            onSelectStart: item.selectStart()
            onSelectPodcast: item.selectPodcast()
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

        // (278) Like the playlist button's symbol: three bars; pinned, the
        // third is shortened and the pin sits in the gap (a little lower
        // and to the right so that it keeps clear of the middle bar)
        Column {
            id: handleBars
            anchors.centerIn: parent
            spacing: Theme.paddingSmall
            readonly property real barWidth: Theme.iconSizeSmall * 0.8
            Repeater {
                model: 3
                Rectangle {
                    width: index === 2 && (item.rowPinned || item.rowAuto) ? handleBars.barWidth * 0.44 : handleBars.barWidth
                    height: Math.max(2, Math.round(Theme.paddingSmall / 2))
                    radius: height / 2
                    color: handle.pressed || item.rowPinned || item.rowAuto ? Theme.highlightColor : Theme.secondaryColor
                }
            }
        }

        // The pin: images/icon-pin.svg is cut close around the drawing (68 x 86),
        // rendered at the size shown (sourceSize) so that it stays sharp and
        // fills the free corner at the bottom right
        HighlightImage {
            visible: item.rowPinned
            source: Qt.resolvedUrl("images/icon-pin.svg")
            color: Theme.highlightColor
            width: Math.round(handleBars.barWidth * 15 / 34)
            height: Math.round(handleBars.barWidth * 19 / 34)
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            x: handleBars.x + Math.round(handleBars.barWidth * 19 / 34)
            y: handleBars.y + Math.round(handleBars.barWidth * 19 / 34)
        }

        // (286) Circle arrow in the same corner: the episode was put here by
        // Auto-Add (images/icon-autoadd-arrow.svg, cut close, 20 x 18)
        HighlightImage {
            visible: item.rowAuto
            source: Qt.resolvedUrl("images/icon-autoadd-arrow.svg")
            color: Theme.highlightColor
            width: Math.round(handleBars.barWidth * 0.46)
            height: Math.round(width * 18 / 20)
            sourceSize.width: width
            sourceSize.height: height
            fillMode: Image.PreserveAspectFit
            x: handleBars.x + Math.round(handleBars.barWidth * 0.52)
            y: handleBars.y + handleBars.height - Math.round(Math.max(2, Math.round(Theme.paddingSmall / 2)) / 2) - Math.round(height / 2)
        }

        onPressed: item.dragStarted(mapToItem(item.ListView.view, mouse.x, mouse.y).y)
        onPositionChanged: item.dragMoved(mapToItem(item.ListView.view, mouse.x, mouse.y).y)
        onReleased: item.dragEnded()
        onCanceled: item.dragEnded()
    }

    property bool chaptersExpanded: false
    // (278) Pinned playlist episode (inside a Repeater "model" is not the row)
    readonly property bool rowPinned: model.pinned === true
    // (286) Put into the playlist by Auto-Add (never together with a pin)
    readonly property bool rowAuto: model.autoAdded === true && model.pinned !== true
    // Inside the chapter Repeater "model" is the chapter - keep the episode
    readonly property int rowEpisodeId: model.episodeId
    property var chapterList: []
    readonly property int currentChapter: isCurrent ? appWindow.player.chapterIndex : -1

    // (304) The episode number as a number of its own among the first
    // characters of the title ("S12E08" matches episode 8 of season 12)
    function numberInTitle() {
        var head = ("" + (model.title || "")).substring(0, 18)
        var found = head.match(/\d+/g) || []
        for (var i = 0; i < found.length; i++) {
            if (parseInt(found[i], 10) === model.episodeNumber) {
                return true
            }
        }
        return false
    }

    function toggleChapters() {
        if (!chaptersExpanded) {
            chapterList = core.episode(model.episodeId).chapters || []
        }
        chaptersExpanded = !chaptersExpanded
    }

    Item {
        id: topArea
        width: parent.width
        height: Math.max(item.showArt ? Theme.itemSizeLarge : 0, textColumn.height + 2 * Theme.paddingMedium)

        CoverImage {
            id: art
            x: item.dragHandle ? handle.width : Theme.horizontalPageMargin
            anchors.verticalCenter: parent.verticalCenter
            visible: item.showArt
            width: item.showArt ? Theme.itemSizeMedium : item.textIndent
            height: item.showArt ? width : 1
            source: item.showArt ? model.imageUrl : ""
            fallbackText: model.podcastTitle
            // (293) Played much weaker than before (0.6), so unplayed stands out
            opacity: (model.played && !item.isCurrent) || item.setAside || item.leftFilter ? 0.35 : 1.0

            // (251) Video episode (or one with a video version): camera
            // bottom left on a dark spot as on the Podcasts page - behind
            // the other symbols of the status line it was hardly seen
            Rectangle {
                visible: model.video === true
                anchors {
                    left: parent.left
                    bottom: parent.bottom
                    margins: Theme.paddingSmall / 2
                }
                width: Math.round(parent.width * 0.38)
                height: width
                radius: width / 2
                color: Qt.rgba(0, 0, 0, 0.55)
                VideoIcon {
                    anchors.centerIn: parent
                    width: Math.round(parent.width * 0.62)
                    color: "white"
                }
            }

            // The episode has bookmarks (50): small bookmark ribbon bottom right (star until 254)
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
                BookmarkIcon {
                    anchors.centerIn: parent
                    width: Theme.iconSizeExtraSmall
                    filled: true
                    color: Theme.highlightColor
                }
            }
        }

        Column {
            id: textColumn
            anchors {
                left: art.right
                leftMargin: item.showArt ? Theme.paddingMedium : 0
                right: item.selectMode ? selectCircle.left : (undoButton.visible ? undoButton.left : (downloadingButton.visible ? downloadingButton.left : (queueButton.visible ? queueButton.left : playButton.left)))
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
                font.bold: model.isNew && !model.played && !item.setAside
                // (293) Played: weak and not bold; unplayed: full colour
                opacity: (model.played && !item.isCurrent && !item.highlighted) || item.setAside || item.leftFilter ? 0.6 : 1.0
                color: item.highlighted || item.isCurrent ? Theme.highlightColor
                                                          : (model.played ? Theme.secondaryColor : Theme.primaryColor)
            }

            // Podcast name only (33): the date moved into the status line,
            // a long podcast name faded it out
            Item {
                visible: item.showPodcastTitle
                width: parent.width
                height: podcastLabel.height
                opacity: item.leftFilter ? 0.6 : 1.0

                Label {
                    id: podcastLabel
                    width: parent.width - (sizeLabelTop.visible ? sizeLabelTop.width + Theme.paddingMedium : 0)
                                        - (cloudTop.visible ? cloudTop.width + Theme.paddingSmall : 0)
                    // (156) Listened to without subscribing: say so
                    text: model.provisional ? model.podcastTitle + " · " + qsTr("not subscribed") : model.podcastTitle
                    // (288) Dots instead of a hard cut at the buttons
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }

                // (323, blind test) Large font: date and time were cut off next
                // to the file size - then the size sits at the end of this line
                // (345, Gemini 17 Nov) The cloud goes with the size (it stayed
                // in the status line, apart from it)
                Image {
                    id: cloudTop
                    visible: statusItem.sizeMoved && statusItem.downloadIconWanted
                    anchors { right: sizeLabelTop.left; rightMargin: Theme.paddingSmall; verticalCenter: sizeLabelTop.verticalCenter }
                    source: "image://theme/icon-s-cloud-download?" + Theme.secondaryHighlightColor
                    width: Theme.iconSizeExtraSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                }
                Label {
                    id: sizeLabelTop
                    visible: statusItem.sizeMoved
                    anchors.right: parent.right
                    text: item.fileSizeText
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
            }

            // (303) Two parts: status on the left, file size fixed on the right
            Item {
                id: statusItem
                width: parent.width
                height: statusLabel.height
                opacity: item.leftFilter ? 0.6 : 1.0
                // (345) The cloud: "downloaded" (not on Downloads); a row that
                // left the download filter says it in words instead
                readonly property bool downloadIconWanted: item.showDownloadIcon && model.downloaded === true && !item.leftFilter
                // (345, review) The symbols beside a two-line status sit on its
                // last line, not between the lines
                readonly property real lastLineCenter: statusLabel.height - statusLabel.height / Math.max(1, statusLabel.lineCount) / 2

                // (323) From the natural widths (not the shown ones: no loop)
                readonly property bool sizeMoved: item.showPodcastTitle && item.fileSizeText !== ""
                    && statusLabel.implicitWidth
                       + (rowVideo.visible ? rowVideo.width + Theme.paddingSmall : 0)
                       + (rowBookmark.visible ? rowBookmark.width + Theme.paddingSmall : 0)
                       + (playedIcon.visible ? playedIcon.width + Theme.paddingSmall : 0)
                       + (downloadIconWanted ? Theme.iconSizeExtraSmall + Theme.paddingSmall : 0)
                       + sizeLabel.implicitWidth + Theme.paddingMedium > width

                Label {
                    id: sizeLabel
                    visible: item.fileSizeText !== "" && !statusItem.sizeMoved
                    anchors.right: parent.right
                    // (345, review) On the last line of a two-line status
                    anchors.bottom: parent.bottom
                    text: item.fileSizeText
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }

                Row {
                    width: parent.width - (sizeLabel.visible ? sizeLabel.width + Theme.paddingMedium : 0)
                    spacing: Theme.paddingSmall
                    height: statusLabel.height

                    Label {
                        id: statusLabel
                        text: {
                            var parts = [TimeFormat.day(model.published, qsTr("today"), qsTr("yesterday"))]
                            if (item.setAside) {
                                parts.push(qsTr("set aside"))
                            }
                            // (345) Left the download filter while the page is open
                            if (item.leftFilter) {
                                parts.push(model.downloaded === true ? qsTr("downloaded") : qsTr("download deleted"))
                            }
                            // Season / episode number from the feed (83)
                            // (304) Not when the title starts with the number already
                        // ("#122 …", "S12E08: …", "Folge 122") - read twice otherwise
                        if (model.episodeNumber > 0 && !item.numberInTitle()) {
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
                            // (191) "in playlist" is a symbol now (queueIcon) - the
                            // word was cut off next to date, time and download
                            // (333, code review) No empty date: it began with " · "
                            return parts.filter(function(p) { return p !== "" }).join(" · ")
                        }
                        font.pixelSize: Theme.fontSizeExtraSmall
                        // (299) Started: the time left in the accent colour (the
                        // bar in the line is gone, the ring around the play
                        // button shows the progress)
                        color: item.highlighted ? Theme.secondaryHighlightColor
                               : (item.showProgress && !model.played && item.remaining > 0 ? Theme.highlightColor : Theme.secondaryColor)
                        // (345, blind test 9 Nov: "1 h 48 min le") Two lines
                        // when there is no room, instead of fading out the end
                        wrapMode: Text.Wrap
                        maximumLineCount: 2
                        elide: Text.ElideRight
                        width: Math.min(implicitWidth, parent.width
                                        - (rowVideo.visible ? rowVideo.width + parent.spacing : 0)
                                        - (rowBookmark.visible ? rowBookmark.width + parent.spacing : 0)
                                        - (playedIcon.visible ? playedIcon.width + parent.spacing : 0)
                                        - (downloadIcon.visible ? downloadIcon.width + parent.spacing : 0))
                    }

                    // (191-253 a small playlist symbol here when queued; since
                    // 254 the playlist button beside ▶ shows that with its check
                    // mark)

                    // (299) Check mark after "played" (a theme symbol, not a
                    // character: the font may lack it)
                    // (306) Without the cover: video and bookmarks here
                    VideoIcon {
                        id: rowVideo
                        visible: !item.showArt && model.video === true
                        y: statusItem.lastLineCenter - height / 2
                        width: Theme.iconSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                    BookmarkIcon {
                        id: rowBookmark
                        visible: !item.showArt && model.bookmarkCount > 0
                        y: statusItem.lastLineCenter - height / 2
                        width: Theme.iconSizeExtraSmall
                        filled: true
                        color: Theme.highlightColor
                    }

                    Image {
                        id: playedIcon
                        visible: model.played === true
                        y: statusItem.lastLineCenter - height / 2
                        source: "image://theme/icon-s-accept?" + Theme.secondaryColor
                        width: Theme.iconSizeExtraSmall
                        height: width
                        sourceSize.width: width
                        sourceSize.height: height
                    }

                    // (303) Not on New episodes (the quick button says it) and
                    // not on Downloads (everything there is downloaded)
                    // (334, forum 16 Nov) Back next to the size: the size alone
                    // was not seen as "downloaded" on New episodes
                    Image {
                        id: downloadIcon
                        visible: statusItem.downloadIconWanted && !statusItem.sizeMoved
                        y: statusItem.lastLineCenter - height / 2
                        source: "image://theme/icon-s-cloud-download?" + Theme.secondaryHighlightColor
                        width: Theme.iconSizeExtraSmall
                        height: width
                        sourceSize.width: width
                        sourceSize.height: height
                    }
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

        // (293) New episodes list as inbox: not downloaded yet -> download
        // button instead of the playlist button (playlist stays in the
        // context menu, by tap on the page and by Auto-Add)
        // (302) Set aside: "Undo" instead of the quick buttons
        MouseArea {
            id: undoButton
            visible: item.setAside && !item.selectMode
            anchors {
                right: playButton.left
                verticalCenter: parent.verticalCenter
            }
            width: undoLabel.width + Theme.paddingLarge
            height: Theme.iconSizeMedium
            onClicked: core.restoreNew(model.episodeId)
            Label {
                id: undoLabel
                anchors.centerIn: parent
                text: qsTr("Undo")
                font.pixelSize: Theme.fontSizeSmall
                color: undoButton.pressed ? Theme.highlightColor : Theme.secondaryHighlightColor
            }
        }

        // (302) Download running: ring with the progress, tap cancels.
        // (309) Everywhere, for a download that is not for the playlist
        // (long press "Download", Auto-Download); one for the playlist shows
        // the ring around the check mark of the playlist button instead
        MouseArea {
            id: downloadingButton
            visible: !item.inPlaylist && model.downloading === true && model.queued !== true && !item.setAside && !item.selectMode
            anchors {
                right: playButton.left
                verticalCenter: parent.verticalCenter
            }
            width: Theme.iconSizeMedium
            height: width
            onClicked: {
                if (cancelBubble.takeHeld()) return
                core.cancelDownload(model.episodeId)
            }
            HoldNameBubble { id: cancelBubble; attached: true; name: qsTr("Cancel download") }
            // (345, Thomas 17 Nov) As large as the ring around the check mark
            ProgressRing {
                anchors.centerIn: parent
                width: Theme.iconSizeMedium
                height: width
                progress: model.downloadProgress
            }
            Rectangle {
                anchors.centerIn: parent
                width: Theme.paddingMedium
                height: width
                radius: Math.round(width / 6)
                color: downloadingButton.pressed ? Theme.highlightColor : Theme.secondaryColor
            }
        }

        // (309) The download button of New episodes is gone: "+" puts the
        // episode into the playlist and loads it (setting "playlist.download")

        // (254, usability audit #6) Playlist without the long press: "+"
        // adds the episode at the end, the check mark (in the playlist)
        // takes it out again - as the toolbar button of the episode page.
        // "Play next" stays in the context menu. Not in the playlist's own
        // rows (handle and removal area there).
        MouseArea {
            id: queueButton
            visible: !item.inPlaylist && !downloadingButton.visible && !undoButton.visible && !item.selectMode
            anchors {
                right: playButton.left
                verticalCenter: parent.verticalCenter
            }
            width: Theme.iconSizeMedium
            height: width
            onClicked: {
                if (queueBubble.takeHeld()) return
                model.queued ? core.queueRemove(model.episodeId) : core.queueAddDefault(model.episodeId)
            }
            // (334, forum 16 Nov) Says that ➕ also downloads (setting
            // "Download playlist episodes"), so the setting can be found
            HoldNameBubble {
                id: queueBubble
                attached: true
                readonly property bool loads: model.downloaded !== true && model.downloading !== true
                                              && core.setting("playlist.download", "1") === "1"
                name: model.queued ? qsTr("Remove from playlist")
                    : core.setting("playlist.addTo", "end") === "next"
                      ? (loads ? qsTr("Add to playlist as next and download") : qsTr("Add to playlist as next"))
                      : (loads ? qsTr("Add to end of playlist and download") : qsTr("Add to end of playlist"))
            }

            // (309) In the playlist and being loaded: the ring around the check
            ProgressRing {
                visible: model.queued === true && model.downloading === true
                anchors.centerIn: parent
                width: Theme.iconSizeMedium
                height: width
                progress: model.downloadProgress
            }
            PlaylistIcon {
                anchors.centerIn: parent
                width: model.queued === true && model.downloading === true ? Theme.iconSizeSmall : Theme.iconSizeSmallPlus
                checked: model.queued === true
                color: queueButton.pressed || model.queued ? Theme.highlightColor : Theme.secondaryColor
            }
        }

        // (313) Selection mode: the circle, filled with a check mark when
        // marked (the whole row is the touch area, this only shows it).
        // (314) A plain drawn check (the theme icon is a box with a check);
        // the episode in the player cannot be marked: a dimmed ▶ instead
        Item {
            id: selectCircle
            visible: item.selectMode
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            width: Theme.iconSizeMedium
            height: width
            Rectangle {
                visible: !item.isCurrent
                anchors.centerIn: parent
                width: Theme.iconSizeSmallPlus
                height: width
                radius: width / 2
                color: item.selected ? Theme.highlightColor : "transparent"
                border.width: Math.max(2, Math.round(Theme.paddingSmall / 3))
                border.color: item.selected ? Theme.highlightColor : Theme.secondaryColor
                // The check: a short and a long stroke
                Item {
                    visible: item.selected
                    anchors.centerIn: parent
                    width: parent.width * 0.5
                    height: width
                    readonly property real stroke: Math.max(2, Math.round(Theme.paddingSmall / 2.5))
                    Rectangle {
                        width: parent.width * 0.42
                        height: parent.stroke
                        radius: height / 2
                        color: Theme.highlightDimmerColor
                        x: parent.width * 0.02
                        y: parent.height * 0.55
                        rotation: 45
                        transformOrigin: Item.Left
                    }
                    Rectangle {
                        width: parent.width * 0.78
                        height: parent.stroke
                        radius: height / 2
                        color: Theme.highlightDimmerColor
                        x: parent.width * 0.30
                        y: parent.height * 0.84
                        rotation: -50
                        transformOrigin: Item.Left
                    }
                }
            }
            Image {
                visible: item.isCurrent
                anchors.centerIn: parent
                source: "image://theme/icon-m-play?" + Theme.secondaryColor
                opacity: 0.5
            }
        }

        RowPlayButton {
            id: playButton
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin - playButton.inset
                verticalCenter: parent.verticalCenter
            }
            icon.source: item.isPlaying ? "image://theme/icon-m-pause" : "image://theme/icon-m-play"
            visible: !item.selectMode
            highlighted: item.isCurrent || down
            HoldNameBubble { id: playBubble; attached: true; name: item.isPlaying ? qsTr("Pause") : qsTr("Play") }
            onClicked: {
                if (playBubble.takeHeld()) return
                appWindow.player.playOrToggle(model.episodeId, item.playlistContext)
            }
        }

        // (299) Ring around the play button: how far the episode has been
        // heard. A faint closed circle with the accent arc on top (at least
        // 4 %, so the first minutes show as well). Drawn beside the button,
        // so its touch area stays as it was.
        Canvas {
            id: progressRing
            visible: item.showProgress && !model.played && !item.selectMode
            anchors.centerIn: playButton
            width: playButton.icon.width + Theme.paddingSmall
            height: width
            readonly property real fraction: Math.max(0.04, Math.min(1, item.progress))
            onFractionChanged: requestPaint()
            onVisibleChanged: if (visible) requestPaint()
            onPaint: {
                var ctx = getContext("2d")
                ctx.reset()
                var lw = Math.max(2, Math.round(Theme.paddingSmall / 3))
                var r = (width - lw) / 2
                ctx.lineWidth = lw
                ctx.lineCap = "round"
                ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.2)
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
                ctx.stroke()
                ctx.strokeStyle = Theme.highlightColor
                ctx.beginPath()
                ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * fraction)
                ctx.stroke()
            }
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

    // Download running: thin bar along the bottom edge. (309) Only in the
    // playlist's rows; elsewhere the ring at the quick button shows it
    Rectangle {
        visible: model.downloading && item.inPlaylist
        anchors.bottom: parent.bottom
        x: art.x
        height: Math.max(2, Theme.paddingSmall / 2)
        width: (parent.width - 2 * Theme.horizontalPageMargin) * Math.max(0.02, model.downloadProgress)
        color: Theme.highlightColor
    }
}

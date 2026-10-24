// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// One episode with the player: image (with the podcast cover as a badge),
// title, state, position slider, skip/chapter/play buttons, download and
// toolbar (speed, download, playlist, sleep timer - 36), the chapters (tap: play from
// there) and the show notes. The player bar opens this page for the
// current episode (episode page and player page are one since build 12).
// For another episode the slider shows its stored position; moving it or
// tapping play starts that episode.
Page {
    id: page
    objectName: "episodePage"
    property int episodeId: -1
    // Opened from the playlist page: playing starts the playlist context
    property bool fromPlaylist: false
    property var episode: core.episode(episodeId)

    readonly property var player: appWindow.player
    readonly property bool isCurrent: player.episodeId === episodeId
    readonly property bool isPlaying: isCurrent && player.playing
    readonly property var speeds: [0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 1.75, 2.0]
    readonly property var sleepMinutes: [0, 5, 10, 15, 30, 45, 60, -1]   // -1: end of episode

    // Position and length shown by the slider: live for the current episode
    readonly property int shownPosition: isCurrent ? player.position : (episode.played ? 0 : (episode.position || 0))
    readonly property int shownDuration: isCurrent ? player.duration : (episode.duration || 0)

    // 1 -> "1,0×", 1.75 -> "1,75×" (decimal separator of the locale)
    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
    }
    property bool downloading: core.isDownloading(episodeId)
    property real downloadProgress: core.downloadProgress(episodeId)
    property bool queued: core.isQueued(episodeId)
    // Bookmarks of this episode (47)
    property var bookmarkList: core.bookmarks(episodeId)

    // The bookmark is stored right away; a small panel at the bottom then
    // offers its name and note (89) - tap beside it or swipe it down when
    // not needed. What was typed is kept when the panel closes.
    function addBookmark() {
        var seconds = page.isCurrent ? page.player.position : page.shownPosition
        var id = core.addBookmark(page.episodeId, seconds)
        if (id > 0) {
            bookmarkPanel.show(id, seconds)
        }
    }
    // Time stamps such as "12:34" or "1:02:03" in the show notes become
    // links that jump there (53) - only in text, not inside tags or links,
    // and only within the episode's length
    function linkTimestamps(html) {
        var parts = html.split(/(<[^>]*>)/)
        var inLink = false
        var length = page.shownDuration > 0 ? page.shownDuration : 24 * 3600
        for (var i = 0; i < parts.length; i++) {
            var part = parts[i]
            if (part.charAt(0) === "<") {
                if (/^<a[\s>]/i.test(part)) {
                    inLink = true
                } else if (/^<\/a\s*>/i.test(part)) {
                    inLink = false
                }
                continue
            }
            if (inLink) {
                continue
            }
            parts[i] = part.replace(/\b(?:(\d{1,2}):)?([0-5]?\d):([0-5]\d)\b(?!\s*(Uhr|h\b|o'clock|am\b|pm\b))/gi, function(match, h, m, s) {
                var seconds = (h ? parseInt(h, 10) * 3600 : 0) + parseInt(m, 10) * 60 + parseInt(s, 10)
                if (seconds > length) {
                    return match
                }
                return "<a href=\"sailcast-seek:" + seconds + "\">" + match + "</a>"
            })
        }
        return parts.join("")
    }

    // Captions under the toolbar symbols for the first few visits (50)
    readonly property bool showHints: _hintCount < 5
    property int _hintCount: parseInt(core.setting("hint.toolbar", "0"))
    Component.onCompleted: {
        core.setSetting("hint.toolbar", "" + (_hintCount + 1))
        // How bookmarks work (66)
        appWindow.showHint("bookmark", qsTr("Tap the star to bookmark the current position - with a name and a note; all bookmarks: playlist pulley"), page)
    }

    // Open row of choices below the toolbar: "", "speed", "sleep"
    property string choices: ""
    onIsCurrentChanged: if (!isCurrent) choices = ""

    Connections {
        target: core
        onEpisodeChanged: {
            if (episodeId === page.episodeId) {
                page.episode = core.episode(page.episodeId)
                page.downloading = core.isDownloading(page.episodeId)
            }
        }
        onDownloadProgressChanged: {
            if (episodeId === page.episodeId) {
                page.downloading = true
                page.downloadProgress = progress
            }
        }
        onDownloadFinished: {
            if (episodeId === page.episodeId) {
                page.downloading = false
            }
        }
        onQueueChanged: page.queued = core.isQueued(page.episodeId)
        onBookmarksChanged: {
            if (episodeId === 0 || episodeId === page.episodeId) {
                page.bookmarkList = core.bookmarks(page.episodeId)
            }
        }
        onDataChanged: page.episode = core.episode(page.episodeId)
    }

    // Landscape (54): two columns - the player on the left, bookmarks,
    // chapters and show notes on the right, each scrolling on its own.
    // The info column moves between the two flickables.
    readonly property bool twoColumns: page.isLandscape

    SilicaFlickable {
        id: flick
        anchors {
            top: parent.top
            bottom: parent.bottom
            bottomMargin: appWindow.playerBarHeight
            left: parent.left
        }
        width: page.twoColumns ? Math.round(page.width / 2) : page.width
        clip: true
        contentHeight: column.height + (page.twoColumns ? 0 : Theme.paddingMedium + infoColumn.height) + Theme.paddingLarge

        PullDownMenu {
            // (83) Share the episode link, optionally from the shown position
            MenuItem {
                visible: page.shownPosition > 0
                text: qsTr("Share from %1").arg(TimeFormat.clock(page.shownPosition))
                onClicked: appWindow.shareEpisode(page.episodeId, page.shownPosition)
            }
            MenuItem {
                text: qsTr("Share")
                onClicked: appWindow.shareEpisode(page.episodeId, 0)
            }
            MenuItem {
                visible: (page.episode.link || "").length > 0
                text: qsTr("Open in browser")
                onClicked: Qt.openUrlExternally(page.episode.link)
            }
            MenuItem {
                visible: page.downloading
                text: qsTr("Cancel download")
                onClicked: core.cancelDownload(page.episodeId)
            }
            MenuItem {
                visible: page.episode.hasChaptersUrl === true && page.episode.chapterCount === 0
                text: qsTr("Load chapters")
                onClicked: core.loadChapters(page.episodeId)
            }
            MenuItem {
                text: qsTr("Show podcast")
                onClicked: pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: page.episode.podcastId })
            }
            // Started episodes offer both: finish them or start over (22)
            MenuItem {
                visible: page.episode.played !== true
                text: qsTr("Mark as played")
                onClicked: {
                    if (page.isCurrent) {
                        page.player.pause()
                    }
                    core.markPlayed(page.episodeId)
                }
            }
            MenuItem {
                visible: page.episode.played === true || (page.episode.position || 0) > 0
                text: qsTr("Mark as unplayed")
                onClicked: {
                    if (page.isCurrent) {
                        page.player.pause()
                        page.player.seek(0)
                    }
                    core.markUnplayed(page.episodeId)
                }
            }
        }

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            PageHeader {
                title: page.episode.podcastTitle || ""
            }

            EpisodeArt {
                anchors.horizontalCenter: parent.horizontalCenter
                width: Math.min(parent.width - 6 * Theme.horizontalPageMargin, page.height * (page.twoColumns ? 0.4 : 0.28))
                height: width
                episodeImageUrl: page.episode.episodeImageUrl || ""
                coverUrl: page.episode.coverUrl || ""
                fallbackText: page.episode.podcastTitle || ""
                // The podcast is in the page header (96)
                showBadge: false

                SleepBadge {
                    visible: page.isCurrent && running
                    anchors {
                        top: parent.top
                        left: parent.left
                        margins: Theme.paddingSmall
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: page.episode.title || ""
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeLarge
                color: Theme.highlightColor
            }

            Label {
                visible: page.isCurrent && page.player.chapterTitle.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: page.player.chapterTitle
                truncationMode: TruncationMode.Fade
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: {
                    var e = page.episode
                    var parts = [TimeFormat.day(e.published, qsTr("today"), qsTr("yesterday"))]
                    // Season / episode number from the feed (83)
                    if (e.episodeNumber > 0) {
                        parts.unshift(e.season > 0 ? qsTr("S%1 E%2").arg(e.season).arg(e.episodeNumber)
                                                   : qsTr("#%1").arg(e.episodeNumber))
                    }
                    if (e.duration > 0) {
                        parts.push(TimeFormat.length(e.duration, qsTr("%1 h %2 min"), qsTr("%1 min")))
                    }
                    if (e.played) {
                        parts.push(qsTr("played"))
                    } else if (e.position > 0 && e.remaining > 0) {
                        parts.push(qsTr("%1 left").arg(TimeFormat.length(e.remaining, qsTr("%1 h %2 min"), qsTr("%1 min"))))
                    }
                    if (e.downloaded) {
                        parts.push(qsTr("downloaded"))
                    }
                    if (page.queued) {
                        parts.push(qsTr("in playlist"))
                    }
                    return parts.join(" · ")
                }
            }

            // Position: only the knob can be dragged (see SeekBar.qml)
            // A bit narrower than the page, centered
            SeekBar {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                position: page.shownPosition
                duration: page.shownDuration
                onSeekRequested: {
                    if (page.isCurrent) {
                        page.player.seek(seconds)
                    } else {
                        page.player.playFrom(page.episodeId, seconds, page.fromPlaylist)
                    }
                }
            }

            // Toolbar (36, after Podcast Addict): speed · download · playlist
            // · bookmark · sleep timer. Symbols only since 50 (five labels
            // overlapped); short captions as a hint for the first 5 times the
            // episode page is opened, status texts (download %, sleep
            // minutes, "podcast" speed) always. Speed and sleep timer open a
            // row of choices below (only for the episode playing now).
            Row {
                id: toolbar
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.horizontalPageMargin
                readonly property real cellWidth: width / 5

                ToolbarButton {
                    width: toolbar.cellWidth
                    enabled: page.isCurrent
                    active: page.choices === "speed"
                    text: page.speedText(page.player.rate)
                    label: page.showHints ? qsTr("Speed") : (page.player.podcastRate > 0 ? qsTr("podcast") : "")
                    onClicked: page.choices = page.choices === "speed" ? "" : "speed"
                }
                ToolbarButton {
                    width: toolbar.cellWidth
                    enabled: !page.downloading || !page.episode.downloaded
                    iconSource: page.episode.downloaded ? "image://theme/icon-m-delete" : "image://theme/icon-m-cloud-download"
                    label: page.downloading ? Math.round(page.downloadProgress * 100) + " %"
                                            : (page.showHints ? (page.episode.downloaded ? qsTr("Delete") : qsTr("Load")) : "")
                    onClicked: {
                        if (page.downloading) {
                            core.cancelDownload(page.episodeId)
                        } else if (page.episode.downloaded) {
                            remorse.execute(qsTr("Deleting download"), function() { core.deleteDownload(page.episodeId) })
                        } else {
                            appWindow.requestDownload(page.episodeId)
                        }
                    }
                }
                ToolbarButton {
                    width: toolbar.cellWidth
                    active: page.queued
                    iconSource: page.queued ? "image://theme/icon-m-acknowledge" : "image://theme/icon-m-add"
                    label: page.showHints ? qsTr("Playlist") : ""
                    onClicked: page.queued ? core.queueRemove(page.episodeId) : core.queueAdd(page.episodeId, false)
                }
                // Bookmark at the current position (47)
                ToolbarButton {
                    width: toolbar.cellWidth
                    // Always the empty star (71: filled after the first
                    // bookmark it looked as if a second tap would undo it); the number of bookmarks below instead
                    iconSource: "image://theme/icon-m-favorite"
                    label: page.showHints ? qsTr("Mark")
                                          : (page.bookmarkList.length > 0 ? "" + page.bookmarkList.length : "")
                    onClicked: page.addBookmark()
                }
                ToolbarButton {
                    width: toolbar.cellWidth
                    enabled: page.isCurrent
                    moon: true
                    active: page.choices === "sleep" || appWindow.sleepTimer.active || page.player.stopAtEnd
                    label: appWindow.sleepTimer.active
                           ? qsTr("%n min", "", appWindow.sleepTimer.remainingMinutes)
                           : (page.player.stopAtEnd ? qsTr("End") : (page.showHints ? qsTr("Timer") : ""))
                    onClicked: page.choices = page.choices === "sleep" ? "" : "sleep"
                }
            }

            // Choices of the speed or sleep timer button
            Flow {
                visible: page.choices !== "" && page.isCurrent
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                Repeater {
                    model: page.choices === "speed" ? page.speeds : (page.choices === "sleep" ? page.sleepMinutes : [])
                    BackgroundItem {
                        readonly property bool current: page.choices === "speed"
                                                        ? Math.abs(modelData - page.player.rate) < 0.01
                                                        : (modelData === 0 ? !appWindow.sleepTimer.active && !page.player.stopAtEnd
                                                                           : (modelData < 0 && page.player.stopAtEnd))
                        width: choiceLabel.implicitWidth + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        Label {
                            id: choiceLabel
                            anchors.centerIn: parent
                            text: page.choices === "speed" ? page.speedText(modelData)
                                  : (modelData === 0 ? qsTr("Off")
                                     : (modelData < 0 ? qsTr("End of episode") : qsTr("%n min", "", modelData)))
                            font.bold: parent.current
                            color: parent.current || parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        onClicked: {
                            if (page.choices === "speed") {
                                page.player.setRate(modelData)
                            } else {
                                appWindow.sleepTimer.cancel()
                                page.player.stopAtEnd = false
                                if (modelData > 0) {
                                    appWindow.sleepTimer.start(modelData)
                                } else if (modelData < 0) {
                                    page.player.stopAtEnd = true
                                }
                            }
                            page.choices = ""
                        }
                    }
                }
            }

            // Chapters / skip / play
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingMedium

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (page.episode.chapterCount || 0) > 0
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    icon.source: "image://theme/icon-m-previous"
                    onClicked: page.player.previousChapter()
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    seconds: -page.player.skipBackSeconds
                    onClicked: page.player.skip(seconds)
                }
                IconButton {
                    id: playButton
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeMedium
                    height: width
                    icon.source: page.isPlaying ? "image://theme/icon-m-pause" : "image://theme/icon-m-play"
                    onClicked: page.player.playOrToggle(page.episodeId, page.fromPlaylist)

                    BusyIndicator {
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Medium
                        running: page.isPlaying && page.player.buffering
                    }
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    seconds: page.player.skipForwardSeconds
                    onClicked: page.player.skip(seconds)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    visible: (page.episode.chapterCount || 0) > 0
                    enabled: page.isCurrent
                    opacity: enabled ? 1.0 : 0.4
                    icon.source: "image://theme/icon-m-next"
                    onClicked: page.player.nextChapter()
                }
            }
        }

        Column {
            id: infoColumn
            parent: page.twoColumns ? infoFlick.contentItem : flick.contentItem
            y: page.twoColumns ? 0 : column.height + Theme.paddingMedium
            width: page.twoColumns ? infoFlick.width : flick.width
            spacing: Theme.paddingMedium

            Item {
                visible: page.twoColumns
                width: 1
                height: Theme.paddingLarge
            }

            // Bookmarks (47): tap plays from there; menu: note, delete
            SectionHeader {
                visible: page.bookmarkList.length > 0
                text: qsTr("Bookmarks")
            }

            Repeater {
                model: page.bookmarkList
                delegate: ListItem {
                    id: bookmarkRow
                    width: infoColumn.width
                    contentHeight: Math.max(Theme.itemSizeSmall, bookmarkText.height + 2 * Theme.paddingSmall)
                    onClicked: page.player.playFrom(page.episodeId, modelData.position, page.fromPlaylist)
                    menu: Component {
                        ContextMenu {
                            MenuItem {
                                text: qsTr("Rename / note")
                                onClicked: appWindow.editBookmark(modelData)
                            }
                            MenuItem {
                                text: qsTr("Delete bookmark")
                                // Countdown in the app window (not in this menu)
                                onClicked: appWindow.remorseDeleteBookmark(modelData.bookmarkId)
                            }
                        }
                    }
                    Label {
                        id: bookmarkTime
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        text: TimeFormat.clock(modelData.position)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Column {
                        id: bookmarkText
                        anchors {
                            left: bookmarkTime.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            text: modelData.title.length > 0 ? modelData.title : qsTr("Bookmark")
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeSmall
                            color: bookmarkRow.highlighted ? Theme.highlightColor
                                                           : (modelData.title.length > 0 ? Theme.primaryColor : Theme.secondaryColor)
                        }
                        Label {
                            visible: modelData.note.length > 0
                            width: parent.width
                            text: modelData.note
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryHighlightColor
                        }
                    }
                }
            }

            // Chapters
            SectionHeader {
                visible: chapterRepeater.count > 0
                text: qsTr("Chapters")
            }

            Repeater {
                id: chapterRepeater
                model: page.episode.chapters || []
                delegate: BackgroundItem {
                    readonly property bool current: page.isCurrent && index === page.player.chapterIndex
                    width: infoColumn.width
                    height: Theme.itemSizeSmall
                    highlighted: down || current
                    onClicked: page.player.playFrom(page.episodeId, modelData.start / 1000, page.fromPlaylist)
                    Label {
                        id: chapterTime
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        text: TimeFormat.clock(modelData.start / 1000)
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.secondaryHighlightColor
                    }
                    Label {
                        anchors {
                            left: chapterTime.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        text: modelData.title
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                }
            }

            // Show notes
            SectionHeader {
                visible: notes.text.length > 0
                text: qsTr("Show notes")
            }

            Label {
                id: notes
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                text: page.linkTimestamps(page.episode.description || "")
                textFormat: Text.StyledText
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.primaryColor
                linkColor: Theme.highlightColor
                onLinkActivated: {
                    // Time stamps in the show notes jump there (53)
                    if (link.indexOf("sailcast-seek:") === 0) {
                        var seconds = parseInt(link.substring(14))
                        if (page.isCurrent) {
                            page.player.seek(seconds)
                            if (!page.player.playing) {
                                page.player.resume()
                            }
                        } else {
                            page.player.playFrom(page.episodeId, seconds, page.fromPlaylist)
                        }
                        return
                    }
                    Qt.openUrlExternally(link)
                }
            }
        }

        VerticalScrollDecorator {}
    }

    // Right column in landscape (54)
    SilicaFlickable {
        id: infoFlick
        visible: page.twoColumns
        anchors {
            top: parent.top
            bottom: parent.bottom
            bottomMargin: appWindow.playerBarHeight
            right: parent.right
        }
        width: page.width - flick.width
        clip: true
        contentHeight: infoColumn.height + Theme.paddingLarge

        VerticalScrollDecorator {}
    }

    RemorsePopup {
        id: remorse
    }

    // Name and note of a new bookmark (89): docked at the bottom, modal - a
    // tap beside it or a swipe down closes it; Enter goes from the name to
    // the note and closes from there. Not a dialog page: most bookmarks are
    // set while listening, often without any text.
    DockedPanel {
        id: bookmarkPanel
        width: page.width
        height: panelColumn.height + Theme.paddingLarge
        dock: Dock.Bottom
        modal: true
        // Above the page's flickables (two columns in landscape)
        z: 100

        // Opaque background (94): the panel itself is translucent and the
        // toolbar and play buttons showed through the fields
        Rectangle {
            anchors.fill: parent
            color: Theme.highlightDimmerColor
            Rectangle {
                anchors.fill: parent
                color: Theme.rgba(Theme.highlightBackgroundColor, 0.18)
            }
            Rectangle {
                width: parent.width
                height: Math.max(1, Theme.paddingSmall / 4)
                color: Theme.rgba(Theme.highlightColor, 0.5)
            }
        }

        property int bookmarkId: 0
        property int seconds: 0
        property bool saved: true

        function show(id, position) {
            bookmarkId = id
            seconds = position
            saved = false
            nameField.text = ""
            noteField.text = ""
            open = true
            nameField.forceActiveFocus()
        }

        function save() {
            if (saved || bookmarkId <= 0) {
                return
            }
            saved = true
            var name = nameField.text.trim()
            var note = noteField.text.trim()
            if (name.length > 0 || note.length > 0) {
                core.editBookmark(bookmarkId, name, note)
            }
        }

        // Leaving the page with the panel open keeps what was typed
        Connections {
            target: page
            onStatusChanged: {
                if (page.status === PageStatus.Deactivating && bookmarkPanel.open) {
                    bookmarkPanel.open = false
                }
            }
        }
        Component.onDestruction: save()

        onOpenChanged: {
            if (!open) {
                nameField.focus = false
                noteField.focus = false
                save()
            }
        }

        Column {
            id: panelColumn
            width: parent.width
            y: Theme.paddingSmall

            Item {
                width: parent.width
                height: Theme.itemSizeExtraSmall
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: "\u2605 " + qsTr("Bookmark at %1").arg(TimeFormat.clock(bookmarkPanel.seconds))
                    color: Theme.highlightColor
                }
                IconButton {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    icon.source: "image://theme/icon-m-close"
                    onClicked: bookmarkPanel.open = false
                }
            }

            TextField {
                id: nameField
                width: parent.width
                placeholderText: qsTr("Name (optional)")
                label: qsTr("Name")
                inputMethodHints: Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: noteField.forceActiveFocus()
            }

            TextField {
                id: noteField
                width: parent.width
                placeholderText: qsTr("Note (optional)")
                label: qsTr("Note")
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: bookmarkPanel.open = false
            }
        }
    }
}

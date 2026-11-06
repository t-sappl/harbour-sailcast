// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Mini player at the bottom of the pages, in the design of Sailwave's
// player bar: adaptive background colour from the ambience, a fine
// highlight line on top (with the listening progress in the same colour),
// episode image with the podcast cover as a badge, title and chapter, and
// the buttons expand / next / play (167: next instead of the moon - next
// chapter, after the last one the next episode).
//
// Tap on image or text: episode page of the current episode (with the
// player controls). Expanded (167: always possible): a row of controls
// (skip back, speed, bookmark, sleep timer, skip forward), then tabs
// (178) for the chapters, the bookmarks and the next playlist episodes.
// Moon: sleep timer durations above the bar.
//
// Hidden on all episode pages (they have the player controls), settings,
// import/export, dialogs and the file picker.
Rectangle {
    id: playerBar
    width: parent.width

    readonly property var player: appWindow.player

    // Soft shadow above the bar: sets it off from any page background (27)
    Rectangle {
        anchors.bottom: parent.top
        width: parent.width
        height: Theme.paddingLarge * 2
        gradient: Gradient {
            GradientStop { position: 0.0; color: "transparent" }
            // (204) Much lighter on a light ambience: the same shadow lay
            // as a dirty grey band over the last row
            GradientStop {
                position: 1.0
                color: Theme.rgba("black", Theme.colorScheme === Theme.LightOnDark ? 0.35 : 0.1)
            }
        }
    }

    // Takes all touches on the bar that no control inside handles - they
    // must not reach the page behind the bar and scroll it
    // (264, forum, Eudoxos: "enlarge the miniplayer with a swipe") A clearly
    // vertical swipe opens (up) or closes (down) the expanded area, in
    // addition to the button. Left/right swipes are not touched (they page
    // through the ring); controls and the seek bar keep their own touches.
    property bool _swiped: false
    function swipeEnded(dx, dy, stage) {
        if (_swiped) {
            return
        }
        // long enough, and steeper than about 50 degrees
        if (Math.abs(dy) < Theme.paddingLarge * 2 || Math.abs(dy) < 1.2 * Math.abs(dx)) {
            return
        }
        console.warn("PlayerBar swipe (" + stage + "): dx=" + Math.round(dx) + " dy=" + Math.round(dy) + " expanded=" + expanded)
        if (dy < 0 && !expanded) {
            expanded = true
            _swiped = true
        } else if (dy > 0 && expanded) {
            expanded = false
            speedChoicesOpen = false
            _swiped = true
        }
    }

    MouseArea {
        anchors.fill: parent
        preventStealing: true
        property real _x0: 0
        property real _y0: 0
        onPressed: {
            _x0 = mouse.x; _y0 = mouse.y; playerBar._swiped = false
            console.warn("PlayerBar touch on the bar: x=" + Math.round(mouse.x) + " y=" + Math.round(mouse.y))
        }
        // acts while the finger moves (a release outside may never arrive)
        onPositionChanged: playerBar.swipeEnded(mouse.x - _x0, mouse.y - _y0, "move")
        onReleased: playerBar.swipeEnded(mouse.x - _x0, mouse.y - _y0, "release")
        onWheel: wheel.accepted = true
    }

    readonly property bool isHiddenPage: {
        var page = pageStack.currentPage
        return page !== null && page !== undefined
                // The episode page has its own player controls - for other
                // episodes the bar would only be in the way there (18)
                && (page.objectName === "episodePage"
                    || page.objectName === "settingsPage" || page.objectName === "podcastSettingsPage" || page.objectName === "aboutPage"
                    // Settings sub-pages (88)
                    || page.objectName.indexOf("settings") === 0
                    || page.objectName.indexOf("restoreBackup") === 0
                    || page.objectName === "importExportPage"
                    // (154) not "Add podcast": ▶ plays there without subscribing
                    || page.objectName === "folderNameDialog"
                    || page.objectName === "folderPickerDialog"
                    || page.objectName === "folderSelectPage"
                    || page.objectName === "folderPodcastsPage"
                    || page.objectName === "bookmarkNoteDialog"
                    || page.objectName === "folderDeleteDialog"
                    || page.objectName === "gpodderDeviceDialog"
                    || page.objectName === "passwordDialog"
                    || page.objectName === "feedLoginDialog"
                    || page.objectName === "archiveDialog"
                    || page.objectName === "mobileDataDialog"
                    // (246) its last line was covered by the bar
                    || page.objectName === "moveDownloadsDialog"
                    || page.objectName === "markAllPlayedDialog"
                    // Sailfish file picker (import)
                    || page.selectedContentProperties !== undefined)
    }
    // In landscape the PlayerPanel on the right takes over (54)
    readonly property bool wanted: player.hasEpisode && !isHiddenPage
    readonly property bool shown: wanted && !appWindow.landscapeLayout

    // Smooth fade in/out via opacity and a Y offset (as in Sailwave)
    opacity: shown ? 1 : 0
    enabled: shown
    visible: opacity > 0

    transform: Translate {
        y: playerBar.shown ? 0 : playerBar.height
        Behavior on y { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }
    }

    Behavior on opacity {
        NumberAnimation { duration: 200; easing.type: Easing.InOutQuad }
    }

    // Background: adaptive colour from the ambience (Sailwave). Hue of the
    // highlight colour, saturation capped (no garish bar with very saturated
    // accents), fixed lightness (the bar stands out the same way with dark
    // or pale accents), then the contrast to the highlight-coloured icons is
    // ensured (>= 3:1, pushing the lightness further away if needed).
    // Opaque on purpose: the bar lies on top of scrolling lists.
    // Re-evaluated automatically when the ambience changes.
    color: adaptiveBarColor(Theme.highlightColor, Theme.colorScheme === Theme.LightOnDark)

    readonly property real barMaxSaturation: 0.45
    readonly property real barLightnessDark: 0.17
    readonly property real barLightnessLight: 0.88
    readonly property real barMinIconContrast: 3.0

    function adaptiveBarColor(accent, dark) {
        // RGB (0..1) -> HSL
        var r = accent.r, g = accent.g, b = accent.b
        var max = Math.max(r, g, b), min = Math.min(r, g, b)
        var h = 0, sat = 0, d = max - min
        if (d > 0) {
            var l0 = (max + min) / 2
            sat = l0 > 0.5 ? d / (2 - max - min) : d / (max + min)
            h = max === r ? (g - b) / d + (g < b ? 6 : 0)
              : max === g ? (b - r) / d + 2
              : (r - g) / d + 4
            h /= 6
        }
        sat = Math.min(sat, barMaxSaturation)

        var light = dark ? barLightnessDark : barLightnessLight
        var color = hslToRgb(h, sat, light)
        for (var step = 0; step < 12 && contrast(color, [r, g, b]) < barMinIconContrast; step++) {
            light = dark ? Math.max(0.05, light - 0.02) : Math.min(0.97, light + 0.02)
            color = hslToRgb(h, sat, light)
        }
        return Qt.rgba(color[0], color[1], color[2], 1.0)
    }

    function hslToRgb(h, s, l) {
        if (s === 0) {
            return [l, l, l]
        }
        function channel(p, q, t) {
            if (t < 0) t += 1
            if (t > 1) t -= 1
            if (t < 1 / 6) return p + (q - p) * 6 * t
            if (t < 1 / 2) return q
            if (t < 2 / 3) return p + (q - p) * (2 / 3 - t) * 6
            return p
        }
        var q = l < 0.5 ? l * (1 + s) : l + s - l * s
        var p = 2 * l - q
        return [channel(p, q, h + 1 / 3), channel(p, q, h), channel(p, q, h - 1 / 3)]
    }

    // Contrast ratio of two RGB colours (WCAG relative luminance)
    function contrast(a, b) {
        function luminance(c) {
            var lin = c.map(function(v) { return v <= 0.03928 ? v / 12.92 : Math.pow((v + 0.055) / 1.055, 2.4) })
            return 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
        }
        var x = luminance(a), y = luminance(b)
        return (Math.max(x, y) + 0.05) / (Math.min(x, y) + 0.05)
    }

    // Expanded state (kept when switching pages)
    property bool expanded: false
    readonly property bool hasChapters: player.chapters.length > 0

    // (167) Bookmarks of the current episode and the next playlist
    // episodes for the expanded area
    property var bookmarkList: []
    property var upNextList: []
    property bool speedChoicesOpen: false
    readonly property var speeds: [0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 1.75, 2.0]
    function refreshBookmarks() {
        bookmarkList = player.hasEpisode ? core.bookmarks(player.episodeId) : []
    }
    function refreshUpNext() {
        var list = player.hasEpisode ? core.upNext(player.episodeId, 10) : []
        // (207) After a trailer from "Neu": its show's first episode comes
        // first - it plays next, not the playlist
        if (player.followUpId > 0) {
            var follow = core.episode(player.followUpId)
            if (follow.episodeId) {
                follow.followUp = true
                list = [follow].concat(list)
            }
        }
        upNextList = list
    }
    // (253) "Previous": the episode heard before the one playing (the top
    // of "Recently heard" without it) - one tap back to it, at its saved
    // position (Thomas: "briefly another episode, then the playlist down
    // to Recently heard")
    property var previousEpisode: ({})
    function refreshPrevious() {
        var ids = ("" + core.setting("player.recent", "")).split(",")
        for (var i = 0; i < ids.length; i++) {
            var id = parseInt(ids[i])
            if (id > 0 && id !== player.episodeId) {
                var e = core.episode(id)
                if (e.episodeId) {
                    previousEpisode = e
                    return
                }
            }
        }
        previousEpisode = ({})
    }
    onExpandedChanged: {
        // (256, Thomas) The duration row closes when the bar is folded or
        // unfolded - it stayed open over the folded bar
        sleepSelectorOpen = false
        if (expanded) refreshPrevious()
    }
    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
    }
    Connections {
        target: player
        onEpisodeIdChanged: {
            playerBar.refreshBookmarks()
            playerBar.refreshUpNext()
            playerBar.refreshPrevious()
        }
        onFollowUpIdChanged: playerBar.refreshUpNext()
    }
    Connections {
        target: core
        onBookmarksChanged: playerBar.refreshBookmarks()
        onQueueChanged: playerBar.refreshUpNext()
        // (253) "Recently heard" changes with dataChanged (noteHeard)
        onDataChanged: if (playerBar.expanded) playerBar.refreshPrevious()
    }
    Component.onCompleted: {
        refreshBookmarks()
        refreshUpNext()
        refreshPrevious()
    }

    // Sleep timer: the moon button opens a row of durations above the bar
    property bool sleepSelectorOpen: false
    property int lastSleepMinutes: parseInt(core.setting("player.sleepMinutes", "30"))
    // An own duration from the time picker (121) is stored by the window
    Connections {
        target: appWindow.sleepTimer
        onActiveChanged: playerBar.lastSleepMinutes = parseInt(core.setting("player.sleepMinutes", "30"))
    }

    readonly property real sleepRowHeight: Theme.itemSizeSmall
    readonly property var sleepOptions: [5, 10, 20, 30, 45, 60]

    readonly property real collapsedHeight: Theme.itemSizeLarge

    height: {
        var sleep = sleepSelectorOpen ? sleepRowHeight : 0
        if (!expanded) return sleep + collapsedHeight
        return sleep + collapsedHeight + expandedColumn.height
    }

    Behavior on height {
        NumberAnimation { duration: 200; easing.type: Easing.OutQuad }
    }

    Connections {
        target: appWindow.sleepTimer
        onExpired: playerBar.sleepSelectorOpen = false
    }

    function openEpisode() {
        if (!player.hasEpisode) {
            return
        }
        expanded = false
        sleepSelectorOpen = false
        pageStack.push(Qt.resolvedUrl("pages/EpisodePage.qml"), { episodeId: player.episodeId })
    }

    // Fine highlight line on top (Sailwave) ...
    Separator {
        anchors.top: parent.top
        width: parent.width
        color: Theme.highlightColor
    }

    // ... and the listening progress on it in the same colour
    Rectangle {
        anchors.top: parent.top
        z: 1
        height: Math.max(2, Math.round(Theme.paddingSmall / 2))
        width: player.duration > 0 ? parent.width * Math.min(1, player.position / player.duration) : 0
        color: Theme.highlightColor
    }

    // --- Sleep timer durations (shown above the main bar) ---
    Item {
        id: sleepRow
        width: parent.width
        anchors.top: parent.top
        height: playerBar.sleepSelectorOpen ? playerBar.sleepRowHeight : 0
        clip: true
        opacity: playerBar.sleepSelectorOpen ? 1.0 : 0.0

        Behavior on height { NumberAnimation { duration: 200; easing.type: Easing.OutQuad } }
        Behavior on opacity { FadeAnimation { duration: 150 } }

        // The durations, "end of episode" (two cells wide since 255) and
        // the unit / stop cell
        readonly property real cellWidth: (width - 2 * Theme.horizontalPageMargin) / (playerBar.sleepOptions.length + 4)

        Row {
            x: Theme.horizontalPageMargin
            height: playerBar.sleepRowHeight

            Repeater {
                model: playerBar.sleepOptions

                BackgroundItem {
                    id: sleepOption
                    width: sleepRow.cellWidth
                    height: parent.height
                    // Highlight the running duration
                    readonly property bool current: appWindow.sleepTimer.active
                                                    && modelData === playerBar.lastSleepMinutes

                    Label {
                        anchors.centerIn: parent
                        text: modelData
                        font.pixelSize: Theme.fontSizeSmall
                        font.bold: sleepOption.current
                        color: (sleepOption.current || sleepOption.highlighted) ? Theme.highlightColor : Theme.primaryColor
                    }

                    onClicked: {
                        // Remembered as the last used duration
                        playerBar.lastSleepMinutes = modelData
                        core.setSetting("player.sleepMinutes", "" + modelData)
                        player.stopAtEnd = false
                        appWindow.sleepTimer.start(modelData)
                        playerBar.sleepSelectorOpen = false
                    }
                }
            }

            // End of the episode
            BackgroundItem {
                id: endOption
                width: 2 * sleepRow.cellWidth
                height: parent.height

                // (255, usability check 254: "End" was not understood) The
                // full text; it wraps to two lines before it shrinks (check
                // in the largest font size, test 4.207)
                Label {
                    id: endLabel
                    anchors.centerIn: parent
                    width: parent.width - Theme.paddingSmall
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    fontSizeMode: Text.Fit
                    minimumPixelSize: Theme.fontSizeTiny
                    height: parent.height
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("End of episode")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    font.bold: player.stopAtEnd
                    color: (player.stopAtEnd || endOption.highlighted) ? Theme.highlightColor : Theme.primaryColor
                }

                onClicked: {
                    appWindow.sleepTimer.cancel()
                    player.stopAtEnd = true
                    playerBar.sleepSelectorOpen = false
                }
            }

            // Own duration (121)
            BackgroundItem {
                id: otherOption
                width: sleepRow.cellWidth
                height: parent.height
                readonly property bool current: appWindow.sleepTimer.active
                                                && playerBar.sleepOptions.indexOf(playerBar.lastSleepMinutes) < 0

                Label {
                    anchors.centerIn: parent
                    text: "…"
                    font.pixelSize: Theme.fontSizeSmall
                    font.bold: otherOption.current
                    color: (otherOption.current || otherOption.highlighted) ? Theme.highlightColor : Theme.primaryColor
                }

                onClicked: {
                    playerBar.sleepSelectorOpen = false
                    appWindow.chooseSleepTime()
                }
            }

            // Last cell: unit label, or a stop button while a timer runs
            Item {
                width: sleepRow.cellWidth
                height: parent.height

                Label {
                    anchors.centerIn: parent
                    visible: !appWindow.sleepTimer.active && !player.stopAtEnd
                    text: qsTr("min")
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }

                IconButton {
                    anchors.centerIn: parent
                    visible: appWindow.sleepTimer.active || player.stopAtEnd
                    icon.source: "image://theme/icon-m-clear"
                    onClicked: {
                        appWindow.sleepTimer.cancel()
                        player.stopAtEnd = false
                        playerBar.sleepSelectorOpen = false
                    }
                }
            }
        }
    }

    // --- Main bar (compact view) ---
    Item {
        id: mainBar
        width: parent.width
        height: playerBar.collapsedHeight
        anchors.top: sleepRow.bottom

        Row {
            anchors.fill: parent
            anchors.leftMargin: Theme.horizontalPageMargin
            anchors.rightMargin: Theme.horizontalPageMargin
            spacing: Theme.paddingMedium

            BackgroundItem {
                width: parent.width - nextButton.width - parent.spacing
                       - expandIcon.width - parent.spacing
                       - playGap.width - parent.spacing
                       - playButton.width - parent.spacing
                height: parent.height
                anchors.verticalCenter: parent.verticalCenter
                property real _x0: 0
                property real _y0: 0
                onPressed: {
                    _x0 = mouse.x; _y0 = mouse.y; playerBar._swiped = false
                    console.warn("PlayerBar touch on the title: x=" + Math.round(mouse.x) + " y=" + Math.round(mouse.y))
                }
                onPositionChanged: playerBar.swipeEnded(mouse.x - _x0, mouse.y - _y0, "move")
                onReleased: playerBar.swipeEnded(mouse.x - _x0, mouse.y - _y0, "release")
                // a swipe that began on the title is no tap
                onClicked: if (!playerBar._swiped) playerBar.openEpisode()

                Row {
                    anchors.fill: parent
                    spacing: Theme.paddingMedium

                    Item {
                        id: artBox
                        width: Theme.iconSizeMedium
                        height: Theme.iconSizeMedium
                        anchors.verticalCenter: parent.verticalCenter

                        EpisodeArt {
                            anchors.fill: parent
                            episodeImageUrl: player.episodeImageUrl
                            coverUrl: player.coverUrl
                            fallbackText: player.podcastTitle
                            badgeFactor: 0.46
                        }

                        SleepBadge {
                            compact: true
                            x: -Theme.paddingSmall / 2
                            y: -Theme.paddingSmall / 2
                            z: 2
                        }
                    }

                    Column {
                        width: parent.width - artBox.width - parent.spacing
                        anchors.verticalCenter: parent.verticalCenter
                        clip: true

                        // (195) The title stands still (faded at the end);
                        // the second line scrolls instead (184-194: the
                        // title scrolled - Thomas had the second line in
                        // mind)
                        Label {
                            width: parent.width
                            text: player.title
                            truncationMode: TruncationMode.Fade
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                        }

                        // Time left stays put (a ticking number would run
                        // along); chapter or podcast after it scrolls
                        Row {
                            id: infoRow
                            width: parent.width
                            Label {
                                id: timeLeft
                                visible: player.duration > 0
                                text: "−" + TimeFormat.clock(Math.max(0, player.duration - player.position)) + " · "
                                color: Theme.secondaryColor
                                font.pixelSize: Theme.fontSizeTiny
                            }
                            ScrollingLabel {
                                width: infoRow.width - (timeLeft.visible ? timeLeft.width : 0)
                                text: player.chapterTitle.length > 0 ? player.chapterTitle : player.podcastTitle
                                color: Theme.secondaryColor
                                fontSize: Theme.fontSizeTiny
                                active: playerBar.wanted
                                fadeColor: playerBar.color
                            }
                        }
                    }
                }
            }

            // Always (167; until then only with chapters)
            IconButton {
                id: expandIcon
                anchors.verticalCenter: parent.verticalCenter
                icon.source: playerBar.expanded ? "image://theme/icon-m-down" : "image://theme/icon-m-up"
                icon.color: playerBar.expanded ? Theme.highlightColor : Theme.secondaryColor
                HoldNameBubble { id: expandBubble; attached: true; name: playerBar.expanded ? qsTr("Close player") : qsTr("Open player") }
                onClicked: {
                    if (expandBubble.takeHeld()) return
                    playerBar.expanded = !playerBar.expanded
                    if (!playerBar.expanded) {
                        playerBar.speedChoicesOpen = false
                    }
                }
            }

            // (167) Next chapter, after the last one the next episode (as
            // the cover); the sleep timer moved to the expanded area
            // (256, Thomas pressed it instead of play - and it marks the
            // episode as played: mockup m5, variant A) smaller and dimmer,
            // with a gap before the play button
            IconButton {
                id: nextButton
                anchors.verticalCenter: parent.verticalCenter
                scale: 0.8
                opacity: 0.75
                icon.source: "image://theme/icon-m-next"
                icon.color: Theme.primaryColor
                HoldNameBubble { id: nextBubble; attached: true; name: qsTr("Next chapter or episode") }
                onClicked: {
                    if (nextBubble.takeHeld()) return
                    player.next()
                }
            }

            Item {
                id: playGap
                width: Theme.paddingLarge
                height: 1
            }

            // (256) Large, the same symbol as on the episode page and in the
            // expanded bar
            IconButton {
                id: playButton
                anchors.verticalCenter: parent.verticalCenter
                icon.source: player.playing ? "image://theme/icon-l-pause" : "image://theme/icon-l-play"
                icon.color: Theme.primaryColor
                HoldNameBubble { id: playBubble; attached: true; name: player.playing ? qsTr("Pause") : qsTr("Play") }
                onClicked: {
                    if (playBubble.takeHeld()) return
                    player.toggle()
                }

                BusyIndicator {
                    anchors.centerIn: parent
                    size: BusyIndicatorSize.Medium
                    // (232) Only while the position really stands
                    running: player.waiting
                }
            }
        }
    }

    // --- Expandable area: controls, chapters, bookmarks, up next ---
    Item {
        id: expandedContent
        width: parent.width
        anchors.top: mainBar.bottom
        anchors.bottom: parent.bottom
        visible: opacity > 0
        opacity: playerBar.expanded ? 1.0 : 0.0
        clip: true

        Behavior on opacity {
            FadeAnimation { duration: 150 }
        }

        Column {
            id: expandedColumn
            width: parent.width

            Separator {
                width: parent.width
                color: Theme.highlightColor
                horizontalAlignment: Qt.AlignHCenter
            }

            // (172) Position bar as on the episode page: drag the knob to
            // seek (the line on top of the bar only shows the progress)
            SeekBar {
                width: parent.width
                // (179) One line: times beside the bar
                compact: true
                position: player.position
                duration: player.duration
                onSeekRequested: player.seek(seconds)
            }

            // (167) Controls: skip back · speed · bookmark · sleep timer ·
            // skip forward
            Row {
                id: controls
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                readonly property real cellWidth: width / 5
                // (181) Smaller than on the episode page: the bar's own buttons
                // above set the size

                Item {
                    width: controls.cellWidth
                    height: Theme.itemSizeSmall
                    SkipButton {
                        anchors.centerIn: parent
                        width: Theme.itemSizeExtraSmall
                        seconds: -player.skipBackSeconds
                        onSkip: player.skip(seconds)
                    }
                }
                ToolbarButton {
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    active: playerBar.speedChoicesOpen
                    text: playerBar.speedText(player.rate)
                    HoldNameBubble { id: speedBubble; attached: true; name: qsTr("Speed") }
                    onClicked: {
                        if (speedBubble.takeHeld()) return
                        playerBar.speedChoicesOpen = !playerBar.speedChoicesOpen
                    }
                }
                ToolbarButton {
                    id: bookmarkButton
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    // (253) Held above its neighbours while the name
                    // bubble is shown, so it is never painted over
                    z: bookmarkBubble.shown ? 100 : 0
                    bookmarkMark: 0
                    // (256, Thomas) the number of bookmarks, as on the
                    // episode page and the panel
                    badge: playerBar.bookmarkList.length > 0 ? "" + playerBar.bookmarkList.length : ""
                    property bool _held: false
                    onPressAndHold: {
                        _held = true
                        bookmarkBubble.shown = true
                    }
                    onPressedChanged: if (!pressed) bookmarkBubble.shown = false
                    onCanceled: {
                        _held = false
                        bookmarkBubble.shown = false
                    }
                    onClicked: {
                        if (_held) {
                            _held = false
                            return
                        }
                        // (170/171) Then name and note in the docked panel,
                        // as on the episode page
                        appWindow.addBookmarkNow()
                    }
                    HoldNameBubble {
                        id: bookmarkBubble
                        name: qsTr("Bookmark")
                    }
                }
                ToolbarButton {
                    id: sleepButton
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    z: sleepBubble.shown ? 100 : 0
                    visible: !appWindow.sleepTimer.ending
                    moon: true
                    active: playerBar.sleepSelectorOpen || appWindow.sleepTimer.active || player.stopAtEnd
                    property bool _held: false
                    onPressAndHold: {
                        _held = true
                        sleepBubble.shown = true
                    }
                    onPressedChanged: if (!pressed) sleepBubble.shown = false
                    onCanceled: {
                        _held = false
                        sleepBubble.shown = false
                    }
                    onClicked: {
                        if (_held) {
                            _held = false
                            return
                        }
                        playerBar.sleepSelectorOpen = !playerBar.sleepSelectorOpen
                    }
                    HoldNameBubble {
                        id: sleepBubble
                        name: qsTr("Sleep timer · stop after this episode")
                    }
                }
                // In the last minute of the sleep timer: "+5" (53)
                ToolbarButton {
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    visible: appWindow.sleepTimer.ending
                    text: "+5"
                    active: true
                    onClicked: appWindow.sleepTimer.extend(5)
                }
                Item {
                    width: controls.cellWidth
                    height: Theme.itemSizeSmall
                    SkipButton {
                        anchors.centerIn: parent
                        width: Theme.itemSizeExtraSmall
                        seconds: player.skipForwardSeconds
                        onSkip: player.skip(seconds)
                    }
                }
            }

            // Speed choices
            Flow {
                visible: playerBar.speedChoicesOpen
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                Repeater {
                    model: playerBar.speedChoicesOpen ? playerBar.speeds : []
                    BackgroundItem {
                        id: speedItem
                        readonly property bool current: Math.abs(modelData - player.rate) < 0.01
                        width: speedLabel.implicitWidth + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        Label {
                            id: speedLabel
                            anchors.centerIn: parent
                            text: playerBar.speedText(modelData)
                            font.bold: speedItem.current
                            color: speedItem.current || speedItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        onClicked: {
                            player.setRate(modelData)
                            playerBar.speedChoicesOpen = false
                        }
                    }
                }
            }

            // (255, usability check 254: "Previous" stood under the
            // heading "Up next") One line above the tabs, on every tab; only
            // while there is a previous episode. Small and subdued like a
            // hint; a tap plays it from its saved position (from the start
            // when it was heard to the end) - playOrToggle as in the lists.
            BackgroundItem {
                width: parent.width
                height: playerBar.previousEpisode.episodeId ? Theme.itemSizeExtraSmall : 0
                visible: playerBar.previousEpisode.episodeId !== undefined
                onClicked: player.playOrToggle(playerBar.previousEpisode.episodeId, false)
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↶ " + qsTr("Previous: %1").arg(playerBar.previousEpisode.title || "")
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }

            // (178) Chapters, bookmarks and the next playlist episodes as
            // tabs over one area of fixed height; (256) Notes as well, and
            // in its own component, shared with the landscape panel
            PlayerTabs {
                rows: 5
                expanded: playerBar.expanded
                bookmarkList: playerBar.bookmarkList
                upNextList: playerBar.upNextList
                // (268) Thomas: also close by swiping down over the tabs
                onSwipedDown: {
                    playerBar.expanded = false
                    playerBar.speedChoicesOpen = false
                }
                onOpenPlaylist: {
                    playerBar.expanded = false
                    appWindow.showPlaylist()
                }
            }
            Item { width: 1; height: Theme.paddingMedium }
        }
    }
}

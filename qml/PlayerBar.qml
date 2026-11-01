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
    MouseArea {
        anchors.fill: parent
        preventStealing: true
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
    // (178) Tabs of the expanded area
    property string chosenTab: ""
    readonly property var tabs: {
        var list = []
        if (player.chapters.length > 0) list.push({ key: "chapters", text: qsTr("Chapters") })
        if (bookmarkList.length > 0) list.push({ key: "bookmarks", text: qsTr("Bookmarks") })
        list.push({ key: "next", text: qsTr("Up next") })
        return list
    }
    readonly property string currentTab: {
        for (var i = 0; i < tabs.length; i++) {
            if (tabs[i].key === chosenTab) return chosenTab
        }
        return tabs[0].key
    }
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
    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
    }
    Connections {
        target: player
        onEpisodeIdChanged: {
            playerBar.chosenTab = ""
            playerBar.refreshBookmarks()
            playerBar.refreshUpNext()
        }
        onFollowUpIdChanged: playerBar.refreshUpNext()
    }
    Connections {
        target: core
        onBookmarksChanged: playerBar.refreshBookmarks()
        onQueueChanged: playerBar.refreshUpNext()
    }
    Component.onCompleted: {
        refreshBookmarks()
        refreshUpNext()
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
    readonly property var sleepOptions: [5, 10, 15, 30, 45, 60, 90]

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

        // The durations, "end of episode" and the unit / stop cell
        readonly property real cellWidth: (width - 2 * Theme.horizontalPageMargin) / (playerBar.sleepOptions.length + 3)

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
                width: sleepRow.cellWidth
                height: parent.height

                Label {
                    anchors.centerIn: parent
                    width: parent.width
                    horizontalAlignment: Text.AlignHCenter
                    fontSizeMode: Text.HorizontalFit
                    minimumPixelSize: Theme.fontSizeTiny
                    text: qsTr("End")
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
                       - playButton.width - parent.spacing
                height: parent.height
                anchors.verticalCenter: parent.verticalCenter
                onClicked: playerBar.openEpisode()

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
                                text: "−" + TimeFormat.clock(player.duration - player.position) + " · "
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
                onClicked: {
                    playerBar.expanded = !playerBar.expanded
                    if (!playerBar.expanded) {
                        playerBar.speedChoicesOpen = false
                    }
                }
            }

            // (167) Next chapter, after the last one the next episode (as
            // the cover); the sleep timer moved to the expanded area
            IconButton {
                id: nextButton
                anchors.verticalCenter: parent.verticalCenter
                icon.source: "image://theme/icon-m-next"
                icon.color: Theme.primaryColor
                onClicked: player.next()
            }

            IconButton {
                id: playButton
                anchors.verticalCenter: parent.verticalCenter
                icon.source: player.playing ? "image://theme/icon-m-pause" : "image://theme/icon-m-play"
                icon.color: Theme.primaryColor
                onClicked: player.toggle()

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
                        onClicked: player.skip(seconds)
                    }
                }
                ToolbarButton {
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    active: playerBar.speedChoicesOpen
                    text: playerBar.speedText(player.rate)
                    onClicked: playerBar.speedChoicesOpen = !playerBar.speedChoicesOpen
                }
                ToolbarButton {
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    iconSource: "image://theme/icon-m-favorite"
                    onClicked: {
                        // (170/171) Then name and note in the docked panel,
                        // as on the episode page
                        appWindow.addBookmarkNow()
                    }
                }
                ToolbarButton {
                    width: controls.cellWidth
                    symbolScale: 0.8
                    height: Theme.itemSizeSmall
                    visible: !appWindow.sleepTimer.ending
                    moon: true
                    active: playerBar.sleepSelectorOpen || appWindow.sleepTimer.active || player.stopAtEnd
                    onClicked: playerBar.sleepSelectorOpen = !playerBar.sleepSelectorOpen
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
                        onClicked: player.skip(seconds)
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

            // (178) Chapters, bookmarks and the next playlist episodes as
            // tabs over one area of fixed height (before: one below the
            // other, "Up next" only without chapters and bookmarks). Tabs
            // without content are left out; the first one with content is
            // chosen, a choice holds until the next episode.
            Row {
                id: barTabs
                width: parent.width
                Repeater {
                    model: playerBar.tabs
                    // By id (145)
                    BackgroundItem {
                        id: barTabItem
                        width: barTabs.width / Math.max(1, playerBar.tabs.length)
                        height: Theme.itemSizeExtraSmall
                        readonly property bool current: playerBar.currentTab === modelData.key
                        onClicked: playerBar.chosenTab = modelData.key
                        Label {
                            anchors.centerIn: parent
                            width: parent.width - 2 * Theme.paddingSmall
                            horizontalAlignment: Text.AlignHCenter
                            truncationMode: TruncationMode.Fade
                            text: modelData.text
                            font.pixelSize: Theme.fontSizeSmall
                            color: barTabItem.current || barTabItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
                        }
                        Rectangle {
                            anchors.bottom: parent.bottom
                            anchors.horizontalCenter: parent.horizontalCenter
                            width: parent.width - 2 * Theme.paddingLarge
                            height: Theme.paddingSmall / 2
                            color: Theme.highlightColor
                            visible: barTabItem.current
                        }
                    }
                }
            }

            Item {
                id: tabArea
                width: parent.width
                height: 5 * Theme.itemSizeSmall

                ChapterList {
                    width: parent.width
                    visible: playerBar.currentTab === "chapters"
                    visibleRows: 5
                    expanded: playerBar.expanded && visible
                }

                // (167) Bookmarks of the episode; tap jumps there
                SilicaListView {
                    id: bookmarkView
                    anchors.fill: parent
                    visible: playerBar.currentTab === "bookmarks"
                    clip: true
                    interactive: contentHeight > height
                    model: playerBar.bookmarkList
                    // (182) A ListItem with the menu of the episode page (183:
                    // without "Open episode" - the cover opens it)
                    delegate: ListItem {
                        id: bookmarkRow
                        width: bookmarkView.width
                        // (173) The note below the name, as on the bookmarks page
                        readonly property bool hasNote: (modelData.note || "").length > 0
                        contentHeight: hasNote ? Math.max(Theme.itemSizeMedium, bookmarkText.height + 2 * Theme.paddingSmall)
                                               : Theme.itemSizeSmall
                        onClicked: player.seek(modelData.position)
                        menu: Component {
                            ContextMenu {
                                MenuItem {
                                    text: qsTr("Rename / note")
                                    onClicked: appWindow.editBookmark(modelData)
                                }
                                MenuItem {
                                    text: qsTr("Delete bookmark")
                                    onClicked: appWindow.remorseDeleteBookmark(modelData.bookmarkId)
                                }
                            }
                        }
                        Label {
                            id: bookmarkTime
                            x: Theme.horizontalPageMargin
                            // (177) On the line of the name, not centred on name
                            // and note (179: by y - anchors only reach
                            // siblings, the name is inside the Column)
                            y: bookmarkText.y + bookmarkName.baselineOffset - baselineOffset
                            width: Theme.itemSizeMedium
                            text: TimeFormat.clock(modelData.position)
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: bookmarkRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
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
                                id: bookmarkName
                                width: parent.width
                                // (170) Numbered: not "Bookmark" under "Bookmarks"
                                text: (modelData.title || "").length > 0 ? modelData.title
                                                                         : qsTr("Bookmark %1").arg(index + 1)
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeSmall
                                color: bookmarkRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                            }
                            Label {
                                visible: bookmarkRow.hasNote
                                width: parent.width
                                text: modelData.note || ""
                                wrapMode: Text.Wrap
                                maximumLineCount: 2
                                elide: Text.ElideRight
                                font.pixelSize: Theme.fontSizeExtraSmall
                                color: Theme.secondaryHighlightColor
                            }
                        }
                    }
                    VerticalScrollDecorator {}
                }

                // (167) The next playlist episodes in the order they will
                // play; tap plays one
                SilicaListView {
                    id: upNextView
                    anchors.fill: parent
                    visible: playerBar.currentTab === "next"
                    clip: true
                    interactive: contentHeight > height
                    model: playerBar.upNextList
                    footer: BackgroundItem {
                        width: upNextView.width
                        height: Theme.itemSizeExtraSmall
                        visible: playerBar.upNextList.length > 0
                        onClicked: {
                            playerBar.expanded = false
                            appWindow.showPlaylist()
                        }
                        Label {
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.horizontalPageMargin
                            anchors.verticalCenter: parent.verticalCenter
                            text: qsTr("Full playlist") + " \u203a"
                            font.pixelSize: Theme.fontSizeSmall
                            color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                        }
                    }
                    // (183) Menu: play next, remove; reordering on the
                    // playlist page (footer)
                    delegate: ListItem {
                        id: nextRow
                        width: upNextView.width
                        contentHeight: Theme.itemSizeSmall
                        onClicked: player.playOrToggle(modelData.episodeId, modelData.followUp !== true)
                        // (207) The trailer's follow-up is not in the playlist
                        menu: modelData.followUp === true ? null : queueMenu
                        Component {
                            id: queueMenu
                            ContextMenu {
                                MenuItem {
                                    visible: index > (playerBar.upNextList.length > 0
                                                      && playerBar.upNextList[0].followUp === true ? 1 : 0)
                                    text: qsTr("Play next")
                                    onClicked: core.queueAdd(modelData.episodeId, true)
                                }
                                MenuItem {
                                    text: qsTr("Remove from playlist")
                                    onClicked: core.queueRemove(modelData.episodeId)
                                }
                            }
                        }
                        EpisodeArt {
                            id: nextArt
                            x: Theme.horizontalPageMargin
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.iconSizeMedium
                            height: Theme.iconSizeMedium
                            episodeImageUrl: modelData.episodeImageUrl || ""
                            coverUrl: modelData.coverUrl || ""
                            fallbackText: modelData.podcastTitle || ""
                            badgeFactor: 0.46
                        }
                        Column {
                            anchors {
                                left: nextArt.right
                                leftMargin: Theme.paddingMedium
                                right: parent.right
                                rightMargin: Theme.horizontalPageMargin
                                verticalCenter: parent.verticalCenter
                            }
                            Label {
                                width: parent.width
                                text: modelData.title
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeSmall
                                color: nextRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                            }
                            Label {
                                width: parent.width
                                text: modelData.followUp === true
                                      ? qsTr("After the trailer · %1").arg(modelData.podcastTitle)
                                      : modelData.podcastTitle
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeTiny
                                color: nextRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                            }
                        }
                    }
                    VerticalScrollDecorator {}
                }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeSmall
                    visible: playerBar.currentTab === "next" && playerBar.upNextList.length === 0
                    verticalAlignment: Text.AlignVCenter
                    text: qsTr("The playlist is empty")
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                    truncationMode: TruncationMode.Fade
                }
            }
            Item { width: 1; height: Theme.paddingMedium }
        }
    }
}

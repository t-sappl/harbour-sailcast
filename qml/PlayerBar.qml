// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Mini player at the bottom of the pages, in the design of Sailwave's
// player bar: adaptive background colour from the ambience, a fine
// highlight line on top (with the listening progress in the same colour),
// episode image with the podcast cover as a badge, title and chapter, and
// the buttons expand (only with chapters) / sleep timer / play. The moon
// replaced the −30 button in 19.
//
// Tap on image or text: episode page of the current episode (with the
// player controls). Moon: sleep timer durations above the bar. Expanded:
// the chapters of the episode - 5 visible, more by scrolling
// inside the area; tap on a chapter jumps there.
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
            GradientStop { position: 1.0; color: Theme.rgba("black", 0.35) }
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
                    || page.objectName === "addPodcastDialog"
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

    // Expanded state (kept when switching pages). The expanded area shows
    // the chapters - without chapters the bar cannot be expanded.
    property bool expanded: false
    readonly property bool hasChapters: player.chapters.length > 0
    onHasChaptersChanged: {
        if (!hasChapters) {
            expanded = false
        }
    }

    // Sleep timer: the moon button opens a row of durations above the bar
    property bool sleepSelectorOpen: false
    property int lastSleepMinutes: parseInt(core.setting("player.sleepMinutes", "30"))

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
        readonly property real cellWidth: (width - 2 * Theme.horizontalPageMargin) / (playerBar.sleepOptions.length + 2)

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
                width: parent.width - sleepButton.width - parent.spacing
                       - (expandIcon.visible ? expandIcon.width + parent.spacing : 0)
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

                        Label {
                            text: player.title
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            color: Theme.primaryColor
                            font.pixelSize: Theme.fontSizeSmall
                            font.bold: true
                        }

                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            text: {
                                var info = player.chapterTitle.length > 0 ? player.chapterTitle : player.podcastTitle
                                if (player.duration > 0) {
                                    info = "−" + TimeFormat.clock(player.duration - player.position) + " · " + info
                                }
                                return info
                            }
                            color: Theme.secondaryColor
                            font.pixelSize: Theme.fontSizeTiny
                        }
                    }
                }
            }

            // Only episodes with chapters have something to expand (18)
            IconButton {
                id: expandIcon
                visible: playerBar.hasChapters
                anchors.verticalCenter: parent.verticalCenter
                icon.source: playerBar.expanded ? "image://theme/icon-m-down" : "image://theme/icon-m-up"
                icon.color: playerBar.expanded ? Theme.highlightColor : Theme.secondaryColor
                onClicked: playerBar.expanded = !playerBar.expanded
            }

            // Sleep timer button: a moon, highlighted while a timer runs.
            // Drawn from two circles (like the SleepBadge). Always in the
            // compact bar, where −30 was until 18 - so the sleep timer is
            // there for episodes without chapters too (19)
            MouseArea {
                id: sleepButton
                width: Theme.itemSizeSmall
                height: Theme.itemSizeSmall
                anchors.verticalCenter: parent.verticalCenter
                // In the last minute of the sleep timer (fading out) the
                // button offers "+5" instead (53)
                onClicked: {
                    if (appWindow.sleepTimer.ending) {
                        appWindow.sleepTimer.extend(5)
                    } else {
                        playerBar.sleepSelectorOpen = !playerBar.sleepSelectorOpen
                    }
                }

                Label {
                    visible: appWindow.sleepTimer.ending
                    anchors.centerIn: parent
                    text: "+5"
                    font.bold: true
                    color: sleepButton.pressed ? Theme.primaryColor : Theme.highlightColor
                }

                readonly property color moonColor: (pressed || playerBar.sleepSelectorOpen
                                                    || appWindow.sleepTimer.active || player.stopAtEnd)
                                                   ? Theme.highlightColor : Theme.primaryColor

                Item {
                    visible: !appWindow.sleepTimer.ending
                    anchors.centerIn: parent
                    width: Math.round(Theme.iconSizeSmall * 0.85)
                    height: width
                    clip: true

                    Rectangle {
                        anchors.fill: parent
                        radius: width / 2
                        color: sleepButton.moonColor
                    }
                    Rectangle {
                        width: parent.width
                        height: parent.height
                        radius: width / 2
                        color: playerBar.color
                        x: parent.width * 0.38
                        y: -parent.height * 0.22
                    }
                }
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
                    running: player.buffering && player.playing
                }
            }
        }
    }

    // --- Expandable area: the chapters ---
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

            SectionHeader {
                text: qsTr("Chapters")
            }

            ChapterList {
                width: parent.width
                visible: player.chapters.length > 0
                visibleRows: 5
                expanded: playerBar.expanded
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: Theme.itemSizeSmall
                visible: player.chapters.length === 0
                verticalAlignment: Text.AlignVCenter
                text: qsTr("No chapters in this episode")
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
                truncationMode: TruncationMode.Fade
            }
        }
    }
}

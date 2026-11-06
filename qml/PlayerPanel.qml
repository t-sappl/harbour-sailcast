// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Player in landscape (54, after Sailwave 1.1): a column on the right
// instead of the player bar at the bottom - in landscape the height is
// short. Always "expanded": image, podcast, episode and chapter, position
// bar, large previous chapter / skip back / play / skip forward / next
// buttons (also for the car or a motorbike mount), sleep timer and the
// chapters. Tap on image or title opens the episode page. The pages end left
// of it (appWindow.playerPanelWidth); the ring swiping stays on the left.
Rectangle {
    id: panel

    // Shown in landscape with an episode, not on the pages that hide the
    // player (episode page, settings, dialogs ...)
    property bool shown: false
    property color barColor: Theme.highlightDimmerColor
    property bool sleepChoicesOpen: false

    readonly property var player: appWindow.player

    // Bookmarks of the episode playing (97): the number below the star, as
    // on the episode page
    property int bookmarkCount: 0
    function refreshBookmarks() {
        bookmarkCount = player.hasEpisode ? core.bookmarks(player.episodeId).length : 0
    }
    Connections {
        target: appWindow.player
        onEpisodeIdChanged: panel.refreshBookmarks()
    }
    Connections {
        target: core
        onBookmarksChanged: {
            if (episodeId === 0 || episodeId === panel.player.episodeId) {
                panel.refreshBookmarks()
            }
        }
    }
    Component.onCompleted: refreshBookmarks()
    // (255) The episode heard before this one (from the player bar, which
    // keeps it up to date); "Previous" line above the chapters
    property var previousEpisode: ({})
    // (256) For the tabs - from the player bar, which keeps them up to date
    property var bookmarkList: []
    property var upNextList: []
    readonly property var sleepOptions: [5, 10, 20, 30, 45, 60]
    // (255, usability check 254: no speed in landscape) as in the player bar
    property bool speedChoicesOpen: false
    readonly property var speeds: [0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 1.75, 2.0]
    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
    }

    color: barColor
    visible: shown || slide.x < width

    transform: Translate {
        id: slide
        x: panel.shown ? 0 : panel.width
        Behavior on x { NumberAnimation { duration: 250; easing.type: Easing.InOutQuad } }
    }

    // Touches that no control takes must not reach the page behind
    MouseArea {
        anchors.fill: parent
        preventStealing: true
        onWheel: wheel.accepted = true
    }

    function openEpisode() {
        if (player.hasEpisode) {
            pageStack.push(Qt.resolvedUrl("pages/EpisodePage.qml"), { episodeId: player.episodeId })
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge
        clip: true

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingMedium

            Item { width: 1; height: Theme.paddingMedium }

            Row {
                x: Theme.paddingLarge
                width: parent.width - 2 * Theme.paddingLarge
                spacing: Theme.paddingMedium

                EpisodeArt {
                    id: art
                    width: Theme.itemSizeExtraLarge
                    height: width
                    episodeImageUrl: player.episodeImageUrl
                    coverUrl: player.coverUrl
                    fallbackText: player.podcastTitle
                    // The podcast name is next to the image (96)
                    showBadge: false

                    SleepBadge {
                        visible: running
                        anchors {
                            top: parent.top
                            left: parent.left
                            margins: Theme.paddingSmall
                        }
                    }

                    MouseArea {
                        anchors.fill: parent
                        onClicked: panel.openEpisode()
                    }
                }

                Column {
                    width: parent.width - art.width - parent.spacing
                    anchors.verticalCenter: parent.verticalCenter

                    Label {
                        width: parent.width
                        text: player.podcastTitle
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                    Label {
                        width: parent.width
                        text: player.title
                        wrapMode: Text.Wrap
                        maximumLineCount: 3
                        elide: Text.ElideRight
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.primaryColor
                        MouseArea {
                            anchors.fill: parent
                            onClicked: panel.openEpisode()
                        }
                    }
                    Label {
                        visible: text.length > 0
                        width: parent.width
                        text: player.chapterTitle.length > 0 ? "▸ " + player.chapterTitle : ""
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.highlightColor
                    }
                }
            }

            SeekBar {
                anchors.horizontalCenter: parent.horizontalCenter
                width: parent.width - 2 * Theme.paddingLarge
                position: player.position
                duration: player.duration
                onSeekRequested: player.seek(seconds)
            }

            // Large buttons
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingSmall

                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    // Without chapters it would only repeat the skip back
                    visible: player.chapters.length > 0
                    icon.source: "image://theme/icon-m-previous"
                    HoldNameBubble { id: prevChapterBubble; attached: true; name: qsTr("Previous chapter") }
                    onClicked: {
                        if (prevChapterBubble.takeHeld()) return
                        player.previousChapter()
                    }
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    seconds: -player.skipBackSeconds
                    onSkip: player.skip(seconds)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeMedium
                    height: width
                    icon.source: player.playing ? "image://theme/icon-l-pause" : "image://theme/icon-l-play"
                    HoldNameBubble { id: playBubble; attached: true; name: player.playing ? qsTr("Pause") : qsTr("Play") }
                    onClicked: {
                        if (playBubble.takeHeld()) return
                        player.toggle()
                    }
                    BusyIndicator {
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Medium
                        running: player.waiting
                    }
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    seconds: player.skipForwardSeconds
                    onSkip: player.skip(seconds)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    // Next chapter, after the last one the next episode
                    icon.source: "image://theme/icon-m-next"
                    HoldNameBubble { id: nextBubble; attached: true; name: qsTr("Next chapter or episode") }
                    onClicked: {
                        if (nextBubble.takeHeld()) return
                        player.next()
                    }
                }
            }

            // Sleep timer: the moon (or "+5" in its last minute) and a row
            // of durations
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge

                ToolbarButton {
                    width: Theme.itemSizeLarge
                    active: panel.speedChoicesOpen
                    text: panel.speedText(player.rate)
                    HoldNameBubble { id: speedBubble; attached: true; name: qsTr("Speed") }
                    onClicked: {
                        if (speedBubble.takeHeld()) return
                        panel.sleepChoicesOpen = false
                        panel.speedChoicesOpen = !panel.speedChoicesOpen
                    }
                }
                // Bookmark at the current position (95, as the star on the
                // episode page; name and note later in the bookmark list)
                ToolbarButton {
                    width: Theme.itemSizeLarge
                    // (253) Held above its neighbours while the name
                    // bubble is shown, so it is never painted over
                    z: bookmarkBubble.shown ? 100 : 0
                    bookmarkMark: 0
                    label: panel.bookmarkCount > 0 ? "" + panel.bookmarkCount : ""
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
                    width: Theme.itemSizeLarge
                    z: sleepBubble.shown ? 100 : 0
                    moon: true
                    visible: !appWindow.sleepTimer.ending
                    active: panel.sleepChoicesOpen || appWindow.sleepTimer.active || player.stopAtEnd
                    label: appWindow.sleepTimer.active
                           ? qsTr("%n min", "", appWindow.sleepTimer.remainingMinutes)
                           // (253, usability audit #33) same fix as the
                           // episode page: "End" alone was cryptic
                           : (player.stopAtEnd ? qsTr("Ends here") : "")
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
                        panel.speedChoicesOpen = false
                        panel.sleepChoicesOpen = !panel.sleepChoicesOpen
                    }
                    HoldNameBubble {
                        id: sleepBubble
                        name: qsTr("Sleep timer · stop after this episode")
                    }
                }
                ToolbarButton {
                    width: Theme.itemSizeLarge
                    visible: appWindow.sleepTimer.ending
                    text: "+5"
                    active: true
                    onClicked: appWindow.sleepTimer.extend(5)
                }
            }

            Flow {
                visible: panel.speedChoicesOpen
                x: Theme.paddingLarge
                width: parent.width - 2 * Theme.paddingLarge
                Repeater {
                    model: panel.speedChoicesOpen ? panel.speeds : []
                    BackgroundItem {
                        id: speedItem
                        readonly property bool current: Math.abs(modelData - player.rate) < 0.01
                        width: speedLabel.implicitWidth + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        Label {
                            id: speedLabel
                            anchors.centerIn: parent
                            text: panel.speedText(modelData)
                            font.bold: speedItem.current
                            color: speedItem.current || speedItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        onClicked: {
                            player.setRate(modelData)
                            panel.speedChoicesOpen = false
                        }
                    }
                }
            }

            Flow {
                visible: panel.sleepChoicesOpen
                x: Theme.paddingLarge
                width: parent.width - 2 * Theme.paddingLarge
                Repeater {
                    model: panel.sleepOptions.concat([-1, -2, 0])
                    BackgroundItem {
                        width: optionLabel.implicitWidth + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        Label {
                            id: optionLabel
                            anchors.centerIn: parent
                            text: modelData > 0 ? qsTr("%n min", "", modelData)
                                                : (modelData === -2 ? qsTr("Other …")
                                                   : (modelData < 0 ? qsTr("End of episode") : qsTr("Off")))
                            color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        onClicked: {
                            if (modelData === -2) {
                                panel.sleepChoicesOpen = false
                                appWindow.chooseSleepTime()
                                return
                            }
                            appWindow.sleepTimer.cancel()
                            player.stopAtEnd = false
                            if (modelData > 0) {
                                appWindow.sleepTimer.start(modelData)
                            } else if (modelData < 0) {
                                player.stopAtEnd = true
                            }
                            panel.sleepChoicesOpen = false
                        }
                    }
                }
            }

            // (255) Same line as in the player bar
            BackgroundItem {
                width: parent.width
                height: panel.previousEpisode.episodeId ? Theme.itemSizeExtraSmall : 0
                visible: panel.previousEpisode.episodeId !== undefined
                onClicked: player.playOrToggle(panel.previousEpisode.episodeId, false)
                Label {
                    x: Theme.paddingLarge
                    width: parent.width - 2 * Theme.paddingLarge
                    anchors.verticalCenter: parent.verticalCenter
                    text: "↶ " + qsTr("Previous: %1").arg(panel.previousEpisode.title || "")
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }

            // (256, "one player view") the same tabs as the expanded bar:
            // Chapters · Bookmarks · Notes · Up next (before: chapters only)
            PlayerTabs {
                width: parent.width
                rows: 6
                expanded: panel.shown
                bookmarkList: panel.bookmarkList
                upNextList: panel.upNextList
                onOpenPlaylist: appWindow.showPlaylist()
            }
        }

        VerticalScrollDecorator {}
    }
}

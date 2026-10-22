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
    readonly property var sleepOptions: [5, 10, 15, 30, 45, 60, 90]

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
                    badgeFactor: 0.3

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
                    onClicked: player.previousChapter()
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    seconds: -player.skipBackSeconds
                    onClicked: player.skip(seconds)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeMedium
                    height: width
                    icon.source: player.playing ? "image://theme/icon-l-pause" : "image://theme/icon-l-play"
                    onClicked: player.toggle()
                    BusyIndicator {
                        anchors.centerIn: parent
                        size: BusyIndicatorSize.Medium
                        running: player.playing && player.buffering
                    }
                }
                SkipButton {
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.itemSizeSmall
                    seconds: player.skipForwardSeconds
                    onClicked: player.skip(seconds)
                }
                IconButton {
                    anchors.verticalCenter: parent.verticalCenter
                    // Next chapter, after the last one the next episode
                    icon.source: "image://theme/icon-m-next"
                    onClicked: player.next()
                }
            }

            // Sleep timer: the moon (or "+5" in its last minute) and a row
            // of durations
            Row {
                anchors.horizontalCenter: parent.horizontalCenter
                spacing: Theme.paddingLarge

                ToolbarButton {
                    width: Theme.itemSizeLarge
                    moon: true
                    visible: !appWindow.sleepTimer.ending
                    active: panel.sleepChoicesOpen || appWindow.sleepTimer.active || player.stopAtEnd
                    label: appWindow.sleepTimer.active
                           ? qsTr("%n min", "", appWindow.sleepTimer.remainingMinutes)
                           : (player.stopAtEnd ? qsTr("End") : "")
                    onClicked: panel.sleepChoicesOpen = !panel.sleepChoicesOpen
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
                visible: panel.sleepChoicesOpen
                x: Theme.paddingLarge
                width: parent.width - 2 * Theme.paddingLarge
                Repeater {
                    model: panel.sleepOptions.concat([-1, 0])
                    BackgroundItem {
                        width: optionLabel.implicitWidth + 2 * Theme.paddingLarge
                        height: Theme.itemSizeExtraSmall
                        Label {
                            id: optionLabel
                            anchors.centerIn: parent
                            text: modelData > 0 ? qsTr("%n min", "", modelData)
                                                : (modelData < 0 ? qsTr("End of episode") : qsTr("Off"))
                            color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        onClicked: {
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

            ChapterList {
                width: parent.width
                visible: player.chapters.length > 0
                visibleRows: 6
                expanded: panel.shown
            }
        }

        VerticalScrollDecorator {}
    }
}

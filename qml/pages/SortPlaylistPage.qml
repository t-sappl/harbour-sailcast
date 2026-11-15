// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (276, was a dialog in 275) Sort the playlist: a tap on an option sorts at
// once and closes the page (swiping back cancelled the dialog by mistake, so
// nothing was sorted). Every option says in a grey line what it does (the
// name "Smart Shuffle" alone doesn't tell that the order within a podcast
// stays). The switch "Keep sorted" acts at once: episodes added to the end
// later are put in the matching place - never reordering what is there. The
// episode playing and episodes dragged by hand (pin at the handle) keep
// their place; "Release pins" lets go of them at once.
// Stored: playlist.sortMode / playlist.sortKeep.
Page {
    id: page
    objectName: "sortPlaylistPage"

    property string mode: core.setting("playlist.sortMode", "")
    property bool keep: core.setting("playlist.sortKeep", "0") === "1"
    property int pins: core.pinnedCount()

    readonly property var options: [
        { mode: "oldest", label: qsTr("Oldest first"), hint: qsTr("By release date, the oldest episode first") },
        { mode: "newest", label: qsTr("Newest first"), hint: qsTr("By release date, the newest episode first") },
        { mode: "shortest", label: qsTr("Shortest first"), hint: qsTr("Short episodes first (same length: the newer first), unknown length last") },
        { mode: "lastDownloaded", label: qsTr("Last downloaded first"), hint: qsTr("The most recent download first, not downloaded ones last (by release date)") },
        { mode: "byPodcast", label: qsTr("By podcast"), hint: qsTr("Podcast by podcast in alphabetical order, each in its own episode order") },
        { mode: "smartShuffle", label: qsTr("Smart Shuffle"), hint: qsTr("Podcasts mixed, order within a podcast stays") }
    ]

    SilicaFlickable {
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Sort playlist")
            }

            // (278) Three groups under section headers like the other pages
            SectionHeader { text: qsTr("Automatic") }

            TextSwitch {
                width: parent.width
                text: qsTr("Keep sorted")
                description: page.mode === ""
                             ? qsTr("Works with the sorting you choose below: the list is sorted when you choose it.")
                             : qsTr("Episodes added later are put in the matching place. Switching this on moves nothing; choosing a sorting below sorts the list. \"Play next\" (right after the episode playing, else at the front) and \"Add to end of playlist\" in the menu pin the episode where you put it.")
                checked: page.keep
                automaticCheck: false
                onClicked: {
                    page.keep = !page.keep
                    core.setSetting("playlist.sortKeep", page.keep ? "1" : "0")
                }
            }

            SectionHeader { text: qsTr("Sort by") }

            Repeater {
                model: page.options
                delegate: BackgroundItem {
                    id: row
                    width: column.width
                    height: Math.max(Theme.itemSizeMedium, texts.height + Theme.paddingLarge)
                    // The sorting chosen last: only its colour (no band)
                    readonly property bool current: page.mode === modelData.mode
                    onClicked: {
                        core.queueSort(modelData.mode, page.keep)
                        pageStack.pop()
                    }

                    Column {
                        id: texts
                        anchors {
                            left: parent.left
                            right: parent.right
                            leftMargin: Theme.horizontalPageMargin
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            text: modelData.label
                            color: row.current || row.down ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            text: modelData.hint
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: row.current || row.down ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                    }
                }
            }

            SectionHeader { text: qsTr("Pins") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: implicitHeight + Theme.paddingMedium
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("The episode playing and episodes you moved by hand (pin at the handle) stay where they are.")
            }

            // A row like the options, in the accent colour (an action, not
            // a way of sorting)
            BackgroundItem {
                id: releaseRow
                width: parent.width
                visible: page.pins > 0
                height: Math.max(Theme.itemSizeMedium, releaseTexts.height + Theme.paddingLarge)
                onClicked: {
                    core.queueClearPins()
                    page.pins = 0
                }
                Column {
                    id: releaseTexts
                    anchors {
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.horizontalPageMargin
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width
                        text: qsTr("Release pins (%1)").arg(page.pins)
                        color: releaseRow.highlighted ? Theme.primaryColor : Theme.highlightColor
                    }
                    Label {
                        width: parent.width
                        text: qsTr("Sort the episodes you moved by hand like the others again")
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator {}
    }
}

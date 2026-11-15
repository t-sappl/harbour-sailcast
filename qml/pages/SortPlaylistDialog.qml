// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (275) Sort the playlist once. Every option says in a grey line what it
// does (the name "Smart Shuffle" alone doesn't tell that the order within a
// podcast stays). "Keep sorted": episodes added to the end later are put in
// the matching place - never reordering what is there. The episode playing
// and episodes dragged by hand (coloured handle, "pinned") keep their place;
// "Release pins" lets go of them. Stored: playlist.sortMode / sortKeep.
Dialog {
    id: page
    objectName: "sortPlaylistDialog"

    property string mode: core.setting("playlist.sortMode", "") || "oldest"
    property bool keep: core.setting("playlist.sortKeep", "0") === "1"
    property int pins: core.pinnedCount()

    readonly property var options: [
        { mode: "oldest", label: qsTr("Oldest first"), hint: qsTr("By release date, the oldest episode first") },
        { mode: "newest", label: qsTr("Newest first"), hint: qsTr("By release date, the newest episode first") },
        { mode: "shortest", label: qsTr("Shortest first"), hint: qsTr("Short episodes first, unknown length last") },
        { mode: "lastDownloaded", label: qsTr("Last downloaded first"), hint: qsTr("The most recent download first, not downloaded ones last") },
        { mode: "byPodcast", label: qsTr("By podcast"), hint: qsTr("Podcast by podcast in alphabetical order, each in its own episode order") },
        { mode: "smartShuffle", label: qsTr("Smart Shuffle"), hint: qsTr("Podcasts mixed alternately, order within a podcast stays") }
    ]

    onAccepted: core.queueSort(mode, keep)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            DialogHeader {
                acceptText: qsTr("Sort")
                title: qsTr("Sort playlist")
            }

            Repeater {
                model: page.options
                delegate: BackgroundItem {
                    id: row
                    width: column.width
                    height: Math.max(Theme.itemSizeMedium, texts.height + Theme.paddingLarge)
                    highlighted: down || page.mode === modelData.mode
                    onClicked: page.mode = modelData.mode

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
                            color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            text: modelData.hint
                            wrapMode: Text.Wrap
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: row.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                    }
                }
            }

            TextSwitch {
                width: parent.width
                text: qsTr("Keep sorted")
                description: qsTr("Episodes added to the end later are put in the matching place. Nothing already in the list is moved.")
                checked: page.keep
                automaticCheck: false
                onClicked: page.keep = !page.keep
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("The episode playing and episodes you moved by hand (handle in colour) stay where they are.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.pins > 0
                text: qsTr("Release pins (%1)").arg(page.pins)
                onClicked: {
                    core.queueClearPins()
                    page.pins = 0
                }
            }
        }

        VerticalScrollDecorator {}
    }
}

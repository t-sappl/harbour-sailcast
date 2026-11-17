// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (282) Sort the downloads: a page of its own like "Sort playlist" (the menu
// of the line in the list header opened upwards and shifted, a Qt 5.6 quirk
// of growing headers). A tap stores the order and closes the page; the
// Downloads page reads "downloads.sort" again when it is shown.
// Stored: downloads.sort (0 last downloaded, 1 newest episode, 2 oldest
// episode, 3 by podcast, 5 largest file first - 303).
Page {
    // (338) No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "sortDownloadsPage"

    property int mode: parseInt(core.setting("downloads.sort", "0"))

    readonly property var options: [
        { mode: 0, label: qsTr("Last downloaded first"), hint: qsTr("The most recent download first") },
        { mode: 1, label: qsTr("Newest episode first"), hint: qsTr("By release date, the newest episode first") },
        { mode: 2, label: qsTr("Oldest episode first"), hint: qsTr("By release date, the oldest episode first") },
        { mode: 3, label: qsTr("By podcast"), hint: qsTr("Podcast by podcast in alphabetical order") },
        // (303) Mode 5 (4 is the grouping of New episodes inside the model)
        { mode: 5, label: qsTr("Largest first"), hint: qsTr("The biggest files first, to free up space") }
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
                title: qsTr("Sort downloads")
            }

            Repeater {
                model: page.options
                delegate: BackgroundItem {
                    id: row
                    width: column.width
                    height: Math.max(Theme.itemSizeMedium, texts.height + Theme.paddingLarge)
                    readonly property bool current: page.mode === modelData.mode
                    onClicked: {
                        core.setSetting("downloads.sort", String(modelData.mode))
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

            Item { width: 1; height: Theme.paddingLarge }
        }

        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (335, forum 16 Nov, Gemini challenge) Download filter of New episodes:
// all, only downloaded, only not downloaded. Opened from the pulley "Filter
// by download …" (a choice page, no entry that cycles blindly in a long
// pulley); the line under the chips swaps and resets it. Stored in
// "home.downloadFilter", for both views and both arrangements.
Page {
    // (338) No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "downloadFilterPage"

    readonly property string current: core.setting("home.downloadFilter", "")

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true

        header: Column {
            width: list.width
            PageHeader { title: qsTr("Filter by download") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Applies to \"New\" and \"All unplayed\". A download still running counts as not downloaded.")
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        model: [
            { key: "", name: qsTr("All episodes") },
            { key: "downloaded", name: qsTr("Only downloaded") },
            { key: "notDownloaded", name: qsTr("Only not downloaded") }
        ]

        delegate: BackgroundItem {
            id: row
            width: list.width
            height: Theme.itemSizeSmall
            highlighted: down || modelData.key === page.current
            onClicked: {
                core.setSetting("home.downloadFilter", modelData.key)
                pageStack.pop()
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                text: modelData.name
                color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }

        VerticalScrollDecorator {}
    }
}

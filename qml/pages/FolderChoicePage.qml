// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (333) The choice of FolderComboBox as a page of its own. Silica's ComboBox
// shows a long list as its own page, which does not know the player bar: from
// about 12 folders the last rows ("New folder …") lay under the bar (Gemini
// audit 15 Nov). This page keeps clear of the bar like the sort pages.
// A tap chooses and closes the page; "New folder …" replaces the page with
// the name dialog, which goes back to the page of the box when it closes.
Page {
    // (338) No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "folderChoicePage"

    property string title: qsTr("Folder")
    property int folderId: 0
    property var folderList: []
    signal chosen(int folderId)
    signal newFolderWanted()

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true

        header: PageHeader {
            title: page.title
        }

        // 0 = "Other podcasts", then the folders, last "New folder …" (-1)
        model: {
            var rows = [{ folderId: 0, name: qsTr("Other podcasts") }]
            for (var i = 0; i < page.folderList.length; i++) {
                rows.push({ folderId: page.folderList[i].folderId, name: page.folderList[i].name })
            }
            rows.push({ folderId: -1, name: qsTr("New folder …") })
            return rows
        }

        delegate: BackgroundItem {
            id: row
            width: list.width
            height: Theme.itemSizeSmall
            readonly property bool current: modelData.folderId === page.folderId
            highlighted: down || current
            onClicked: {
                if (modelData.folderId < 0) {
                    page.newFolderWanted()
                } else {
                    page.chosen(modelData.folderId)
                    pageStack.pop()
                }
            }

            Label {
                anchors {
                    left: parent.left
                    right: parent.right
                    leftMargin: Theme.horizontalPageMargin
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                text: modelData.name
                truncationMode: TruncationMode.Fade
                color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }

        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Choose the folder of a podcast (45): "Other podcasts" (no folder), the
// folders, or a new one. Accepting with a choice sets folderId; the caller
// (appWindow.movePodcastToFolder) moves the podcast.
Dialog {
    id: dialog
    objectName: "folderPickerDialog"

    property int folderId: 0
    property string podcastTitle: ""
    property var folderList: core.folders()
    // A folder created here is the target: the dialog accepts itself as
    // soon as it is shown again (48)
    property bool acceptWhenActive: false

    onStatusChanged: {
        if (status === PageStatus.Active && acceptWhenActive) {
            acceptWhenActive = false
            accept()
        }
    }

    SilicaListView {
        anchors.fill: parent
        model: dialog.folderList

        header: Column {
            width: parent ? parent.width : 0
            DialogHeader {
                title: qsTr("Move to folder")
            }
            Label {
                visible: dialog.podcastTitle.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                truncationMode: TruncationMode.Fade
                color: Theme.secondaryHighlightColor
                text: dialog.podcastTitle
            }
            BackgroundItem {
                width: parent.width
                highlighted: down || dialog.folderId === 0
                onClicked: dialog.folderId = 0
                Label {
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Other podcasts (no folder)")
                    color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
            }
        }

        delegate: BackgroundItem {
            width: parent ? parent.width : 0
            highlighted: down || dialog.folderId === modelData.folderId
            onClicked: dialog.folderId = modelData.folderId
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                text: modelData.name
                color: parent.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
        }

        footer: BackgroundItem {
            width: parent ? parent.width : 0
            onClicked: {
                var nameDialog = pageStack.push(Qt.resolvedUrl("FolderNameDialog.qml"))
                nameDialog.accepted.connect(function() {
                    var id = core.createFolder(nameDialog.folderName)
                    if (id > 0) {
                        dialog.folderList = core.folders()
                        dialog.folderId = id
                        dialog.acceptWhenActive = true
                    }
                })
            }
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("New folder …")
                color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
            }
        }

        VerticalScrollDecorator {}
    }
}

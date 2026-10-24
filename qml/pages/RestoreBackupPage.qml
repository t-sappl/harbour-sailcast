// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Restore a database backup (93, after Sailfeed's restore page): the copies
// on this phone (Downloads/Sailcast-Backups, newest first; the state before
// the last restore on top) and those in the folder "Sailcast-Backups" of the
// Nextcloud of the sync account. A tap asks first (RestoreBackupDialog);
// a Nextcloud backup is downloaded, then restored.
Page {
    id: page
    objectName: "restoreBackupPage"

    // Sync password (from the backup page) for the Nextcloud
    property string password: ""
    readonly property bool nextcloud: core.syncConfigured
                                      && core.setting("sync.backend", "nextcloud") === "nextcloud"

    property var localList: core.localBackups()
    property var cloudList: []
    property bool cloudBusy: false
    property string cloudError: ""
    property string fetching: ""

    function refresh() {
        localList = core.localBackups()
        if (nextcloud) {
            cloudBusy = true
            cloudError = ""
            core.listNextcloudBackups(password)
        }
    }

    function dateText(ms) {
        return ms > 0 ? Qt.formatDateTime(new Date(ms), Qt.DefaultLocaleShortDate) : ""
    }

    function sizeText(bytes) {
        return qsTr("%1 MB").arg((bytes / 1048576).toFixed(1))
    }

    function localTitle(entry) {
        return entry.undo ? qsTr("Before the last restore") : page.dateText(entry.modified)
    }

    // Asks, then restores (a Nextcloud backup is downloaded first)
    function ask(title, localPath, cloudName) {
        var dialog = pageStack.push(Qt.resolvedUrl("RestoreBackupDialog.qml"), { sourceTitle: title })
        dialog.accepted.connect(function() {
            if (cloudName.length > 0) {
                page.fetching = cloudName
                core.fetchNextcloudBackup(cloudName, page.password)
            } else if (core.restoreBackup(localPath)) {
                page.refresh()
            }
        })
    }

    Component.onCompleted: refresh()

    Connections {
        target: core
        onNextcloudBackupsListed: {
            page.cloudBusy = false
            page.cloudList = ok ? backups : []
            page.cloudError = ok ? "" : error
        }
        onNextcloudBackupFetched: {
            page.fetching = ""
            if (!ok) {
                appWindow.showMessage(qsTr("Could not load the backup: %1").arg(error))
            } else if (core.restoreBackup(path)) {
                page.refresh()
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            busy: page.cloudBusy
            MenuItem {
                text: qsTr("Refresh")
                onClicked: page.refresh()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Restore")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("A backup replaces all data of Sailcast on this phone. Tap one to see what happens.")
            }

            SectionHeader { text: qsTr("On this phone") }

            Repeater {
                model: page.localList
                BackgroundItem {
                    id: localItem
                    width: column.width
                    height: Theme.itemSizeMedium
                    onClicked: page.ask(page.localTitle(modelData), modelData.path, "")
                    Column {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            text: page.localTitle(modelData)
                            color: localItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: localItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                            text: (modelData.undo ? page.dateText(modelData.modified) + " · " : "")
                                  + modelData.name + " · " + page.sizeText(modelData.size)
                        }
                    }
                }
            }

            Label {
                visible: page.localList.length === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: qsTr("No backups on this phone yet")
            }

            SectionHeader {
                visible: page.nextcloud
                text: qsTr("In the Nextcloud")
            }

            BusyIndicator {
                visible: running
                running: page.nextcloud && page.cloudBusy
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Medium
            }

            Repeater {
                model: page.nextcloud ? page.cloudList : []
                BackgroundItem {
                    id: cloudItem
                    width: column.width
                    height: Theme.itemSizeMedium
                    enabled: page.fetching.length === 0
                    onClicked: page.ask(page.dateText(modelData.modified) + " · Nextcloud", "", modelData.name)
                    Column {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin - Theme.iconSizeMedium
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            text: page.dateText(modelData.modified) || modelData.name
                            color: cloudItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: cloudItem.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                            text: modelData.name + " · " + page.sizeText(modelData.size)
                        }
                    }
                    BusyIndicator {
                        anchors.right: parent.right
                        anchors.rightMargin: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        size: BusyIndicatorSize.ExtraSmall
                        running: page.fetching === modelData.name
                    }
                }
            }

            Label {
                visible: page.nextcloud && !page.cloudBusy && page.cloudList.length === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: page.cloudError.length > 0 ? qsTr("Could not read the backups: %1").arg(page.cloudError)
                                                 : qsTr("No backups in the Nextcloud yet")
            }
        }

        VerticalScrollDecorator {}
    }
}

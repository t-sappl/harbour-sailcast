// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Settings → Backup (83, own page since 88): automatic database backup into
// Downloads and the Nextcloud of the sync account, "Back up now",
// "Restore …" (93).
Page {
    id: page
    objectName: "settingsBackupPage"

    // Automatic database backup (83): 0 = off, 1 = daily, 7 = weekly
    readonly property var backupOptions: [0, 1, 7]
    // Re-reads the times of the last backups
    property int settingsVersion: 0
    readonly property bool nextcloud: core.setting("sync.backend", "nextcloud") === "nextcloud"
    // The sync password for the Nextcloud copy and the restore page (97:
    // loaded when needed, not when the page opens - Sailfish Secrets may ask
    // for the security code, and a backup started before that answer went
    // to the phone only)
    property string syncPassword: ""
    property bool passwordAsked: false
    // What to do once the password is there: "backup" or "restore"
    property string pendingAction: ""

    readonly property bool needsPassword: core.syncConfigured && nextcloud
                                          && core.setting("backup.nextcloud", "1") === "1"

    Connections {
        target: core
        onSettingsChanged: page.settingsVersion++
    }

    Connections {
        target: secretStore
        onLoaded: {
            if (name !== "sync-password" || page.pendingAction.length === 0) {
                return
            }
            if (ok) {
                page.syncPassword = value
            }
            var action = page.pendingAction
            page.pendingAction = ""
            page.run(action)
        }
    }

    // Gets the password first when the Nextcloud is involved
    function start(action) {
        if (needsPassword && syncPassword.length === 0 && !passwordAsked) {
            passwordAsked = true
            pendingAction = action
            secretStore.load("sync-password")
            return
        }
        run(action)
    }

    function run(action) {
        if (action === "backup") {
            core.backupNow(syncPassword)
        } else if (action === "restore") {
            pageStack.push(Qt.resolvedUrl("RestoreBackupPage.qml"), { password: syncPassword })
        }
    }

    function indexOf(list, value, fallback) {
        var i = list.indexOf(value)
        return i >= 0 ? i : fallback
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Backup")
            }

            ComboBox {
                label: qsTr("Back up automatically")
                description: qsTr("Playlist, folders, bookmarks and settings are not on the sync server - a backup keeps them.")
                currentIndex: page.indexOf(page.backupOptions, parseInt(core.setting("backup.days", "7")), 2)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Off")
                        onClicked: core.setSetting("backup.days", "0")
                    }
                    MenuItem {
                        text: qsTr("Daily")
                        onClicked: core.setSetting("backup.days", "1")
                    }
                    MenuItem {
                        text: qsTr("Weekly")
                        onClicked: core.setSetting("backup.days", "7")
                    }
                }
            }

            // Where the copies go (93: sections instead of "Also to the Nextcloud")
            SectionHeader { text: qsTr("On this phone") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                // settingsVersion: re-read after "Back up now"
                text: {
                    var dummy = page.settingsVersion
                    var last = core.lastBackup()
                    return qsTr("Folder %1, the newest 5 are kept; checked at every start.").arg(core.backupDirectory())
                            + "\n" + qsTr("Last backup: %1").arg(last > 0 ? Qt.formatDateTime(new Date(last), Qt.DefaultLocaleShortDate)
                                                                    : qsTr("never"))
                }
            }

            SectionHeader {
                visible: core.syncConfigured && page.nextcloud
                text: qsTr("Nextcloud")
            }

            // Copy into the Nextcloud of the sync account (83)
            TextSwitch {
                visible: core.syncConfigured && page.nextcloud
                text: qsTr("Back up to Nextcloud")
                description: {
                    var dummy = page.settingsVersion
                    var last = core.lastNextcloudBackup()
                    return qsTr("Into the folder \"Sailcast-Backups\" of the sync account, the newest 5 are kept.")
                            + "\n" + qsTr("Last backup: %1").arg(last > 0 ? Qt.formatDateTime(new Date(last), Qt.DefaultLocaleShortDate)
                                                                    : qsTr("never"))
                }
                checked: core.setting("backup.nextcloud", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("backup.nextcloud", checked ? "1" : "0")
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            ButtonLayout {
                Button {
                    text: qsTr("Back up now")
                    enabled: page.pendingAction.length === 0
                    onClicked: page.start("backup")
                }
                // (93) Local and Nextcloud backups
                Button {
                    text: qsTr("Restore …")
                    enabled: page.pendingAction.length === 0
                    onClicked: page.start("restore")
                }
            }
        }

        VerticalScrollDecorator {}
    }
}

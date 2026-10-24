// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Settings (88, as in Sailfeed): a short main page with one entry per
// subject and a summary of its current values; the settings themselves are
// on sub-pages. Before 88 one long page (8 sections, about 30 controls).
// "Import and export" is an entry here since 88 (before in the Podcasts
// pulley) - not needed every day; while there are no podcasts the New
// episodes pulley still offers it (first steps).
Page {
    id: page
    objectName: "settingsPage"

    // Summaries are re-read when a setting changes or the page is shown again
    property int settingsVersion: 0
    onStatusChanged: if (status === PageStatus.Activating) settingsVersion++

    Connections {
        target: core
        onSettingsChanged: page.settingsVersion++
        onSyncingChanged: page.settingsVersion++
    }

    function join(parts) {
        return parts.filter(function(p) { return p !== "" }).join(" · ")
    }

    function dateText(ms) {
        return ms > 0 ? Qt.formatDateTime(new Date(ms), Qt.DefaultLocaleShortDate) : qsTr("never")
    }

    function startPageName(name) {
        return name === "podcasts" ? qsTr("Podcasts") : (name === "playlist" ? qsTr("Playlist") : qsTr("New episodes"))
    }

    function lockScreenName(prev, next) {
        return (next === "forward" ? qsTr("lock screen: skip")
                : (next === "episodes" ? qsTr("lock screen: episodes") : qsTr("lock screen: chapters")))
    }

    function coverButtonName(name) {
        return name === "back" ? qsTr("Skip back") : name === "forward" ? qsTr("Skip forward")
             : name === "sleep" ? qsTr("Sleep timer") : name === "bookmark" ? qsTr("Bookmark") : qsTr("Next")
    }

    function cleanupName(days) {
        return days < 0 ? qsTr("never delete") : (days === 0 ? qsTr("delete right after listening")
                                                             : qsTr("delete after %n days", "", days))
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        PullDownMenu {
            MenuItem {
                text: qsTr("About Sailcast")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Settings")
            }

            SettingsEntry {
                title: qsTr("General")
                summary: page.settingsVersion >= 0
                         && page.join([qsTr("Start: %1").arg(page.startPageName(core.setting("ui.startPage", "home"))),
                                       core.setting("start.update", "1") === "1" ? qsTr("update on start") : qsTr("no update on start")])
                target: Qt.resolvedUrl("SettingsGeneralPage.qml")
            }

            SettingsEntry {
                title: qsTr("Synchronization")
                summary: page.settingsVersion >= 0 && (!core.syncConfigured ? qsTr("no account")
                         : page.join([core.setting("sync.backend", "nextcloud") === "nextcloud" ? "Nextcloud" : "gpodder.net",
                                      core.setting("sync.user", ""),
                                      core.syncing ? qsTr("synchronizing …")
                                                   : qsTr("last sync %1").arg(page.dateText(parseInt(core.setting("sync.last_sync", "0")) * 1000))]))
                target: Qt.resolvedUrl("SettingsSyncPage.qml")
            }

            SettingsEntry {
                title: qsTr("Playback")
                summary: page.settingsVersion >= 0
                         && page.join([qsTr("skip %1/%2 s").arg(appWindow.player.skipBackSeconds).arg(appWindow.player.skipForwardSeconds),
                                       page.lockScreenName(appWindow.player.remotePrev, appWindow.player.remoteNextAction),
                                       qsTr("cover: %1").arg(page.coverButtonName(appWindow.player.coverButton))])
                target: Qt.resolvedUrl("SettingsPlaybackPage.qml")
            }

            SettingsEntry {
                title: qsTr("Downloads and archive")
                summary: page.settingsVersion >= 0
                         && page.join([core.setting("downloads.wifiOnly", "0") === "1" ? qsTr("Wi-Fi only") : "",
                                       core.setting("downloads.auto", "0") === "1" ? qsTr("new episodes automatically") : "",
                                       page.cleanupName(parseInt(core.setting("cleanup.days", "7")))])
                target: Qt.resolvedUrl("SettingsDownloadsPage.qml")
            }

            SettingsEntry {
                title: qsTr("Backup")
                summary: {
                    var dummy = page.settingsVersion
                    var days = parseInt(core.setting("backup.days", "7"))
                    return page.join([days === 0 ? qsTr("off") : (days === 1 ? qsTr("daily") : qsTr("weekly")),
                                      qsTr("last %1").arg(page.dateText(core.lastBackup()))])
                }
                target: Qt.resolvedUrl("SettingsBackupPage.qml")
            }

            SettingsEntry {
                title: qsTr("Import and export")
                summary: qsTr("OPML, databases, archive folders")
                target: Qt.resolvedUrl("ImportExportPage.qml")
            }

            Item { width: 1; height: Theme.paddingLarge * 2 }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: "Sailcast " + core.version
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("About Sailcast")
                onClicked: pageStack.push(Qt.resolvedUrl("AboutPage.qml"))
            }
        }

        VerticalScrollDecorator {}
    }
}

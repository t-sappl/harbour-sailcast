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

    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
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

            // (326, settings restructured, SETTINGS-PLAN.md) Seven entries by
            // purpose; every setting in one place only
            // (327, Thomas) Order as when setting up the app: pages, account,
            // playback, downloads, bringing data in, backup; About as a row
            SettingsEntry {
                title: qsTr("Pages and navigation")
                summary: page.settingsVersion >= 0
                         && page.join([qsTr("Start: %1").arg(page.startPageName(core.setting("ui.startPage", "home"))),
                                       core.setting("ui.downloadsRing", "0") === "1" ? qsTr("Downloads as a ring page") : ""])
                target: Qt.resolvedUrl("SettingsGeneralPage.qml")
            }

            SettingsEntry {
                title: qsTr("Synchronization")
                // (254, usability audit #4) Before an account is set up,
                // "no account" alone didn't say what this is for
                summary: page.settingsVersion >= 0 && (!core.syncConfigured
                         ? qsTr("Keeps subscriptions and listening progress the same on all your devices")
                         : page.join([core.setting("sync.backend", "nextcloud") === "nextcloud" ? "Nextcloud" : "gpodder.net",
                                      core.syncing ? qsTr("synchronizing …")
                                                   : qsTr("last sync %1").arg(page.dateText(parseInt(core.setting("sync.last_sync", "0")) * 1000)),
                                      core.setting("start.update", "1") === "1" ? qsTr("update on start") : ""]))
                target: Qt.resolvedUrl("SettingsSyncPage.qml")
            }

            SettingsEntry {
                title: qsTr("Playback and playlist")
                summary: page.settingsVersion >= 0
                         && page.join([page.speedText(appWindow.player.globalRate),
                                       qsTr("skip %1/%2 s").arg(appWindow.player.skipBackSeconds).arg(appWindow.player.skipForwardSeconds),
                                       core.setting("playlist.addTo", "end") === "next" ? qsTr("add as next") : qsTr("add to the end")])
                target: Qt.resolvedUrl("SettingsPlaybackPage.qml")
            }

            SettingsEntry {
                title: qsTr("Downloads and storage")
                summary: page.settingsVersion >= 0
                         && page.join([core.setting("downloads.wifiOnly", "0") === "1" ? qsTr("Wi-Fi only") : "",
                                       core.setting("downloads.auto", "0") === "1" ? qsTr("new episodes automatically") : "",
                                       page.cleanupName(parseInt(core.setting("cleanup.days", "7")))])
                target: Qt.resolvedUrl("SettingsDownloadsPage.qml")
            }

            SettingsEntry {
                title: qsTr("Import and export")
                // (312) Its own line for "Share your podcasts"
                summary: qsTr("OPML · gpodder-sailfish · archive folders · share")
                target: Qt.resolvedUrl("ImportExportPage.qml")
            }

            SettingsEntry {
                title: qsTr("Backup and restore")
                summary: {
                    var dummy = page.settingsVersion
                    var days = parseInt(core.setting("backup.days", "7"))
                    return page.join([days === 0 ? qsTr("off") : (days === 1 ? qsTr("daily") : qsTr("weekly")),
                                      qsTr("last %1").arg(page.dateText(core.lastBackup()))])
                }
                target: Qt.resolvedUrl("SettingsBackupPage.qml")
            }

            SettingsEntry {
                title: qsTr("About Sailcast")
                summary: "Sailcast " + core.version
                target: Qt.resolvedUrl("AboutPage.qml")
            }
        }

        VerticalScrollDecorator {}
    }
}

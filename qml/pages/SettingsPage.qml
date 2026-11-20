// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

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

    // (346) Icons of the entries - one place to change a name. (350, Thomas
    // 17 Nov: decided on the device - the icons stay; test switch removed)
    readonly property var icons: ({
        "general": "icon-m-home",
        "sync": "icon-m-sync",
        "playback": "icon-m-media-playlists",
        "downloads": "icon-m-cloud-download",
        "importExport": "icon-m-transfer",
        "backup": "icon-m-history",
        "about": "icon-m-about"
    })
    readonly property bool iconsOn: true

    // (350) "Sailcast 1.6 available" under About while there is a new version
    readonly property bool updateFound: core.appUpdateEnabled
                                        && (core.appUpdateState === "available" || core.appUpdateState === "ready"
                                            || core.appUpdateState === "downloading")

    function join(parts) {
        return parts.filter(function(p) { return p !== "" }).join(" · ")
    }

    // (350) Short, so the line fits next to the icon: "today 19:19",
    // "yesterday 08:12", "Monday 21:05", older ones only the date
    function dateText(ms) {
        if (!(ms > 0)) {
            return qsTr("never")
        }
        var now = new Date()
        var startOfWeek = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 6).getTime()
        var day = TimeFormat.day(ms, qsTr("today"), qsTr("yesterday"))
        return ms >= startOfWeek ? day + " " + Qt.formatTime(new Date(ms), Qt.DefaultLocaleShortDate) : day
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
                icon: page.icons.general
                showIcon: page.iconsOn
                title: qsTr("Pages and navigation")
                summary: page.settingsVersion >= 0
                         && page.join([qsTr("Start: %1").arg(page.startPageName(core.setting("ui.startPage", "home"))),
                                       core.setting("ui.downloadsRing", "0") === "1" ? qsTr("Downloads as a ring page") : ""])
                target: Qt.resolvedUrl("SettingsGeneralPage.qml")
            }

            SettingsEntry {
                icon: page.icons.sync
                showIcon: page.iconsOn
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
                icon: page.icons.playback
                showIcon: page.iconsOn
                title: qsTr("Playback and playlist")
                summary: page.settingsVersion >= 0
                         && page.join([page.speedText(appWindow.player.globalRate),
                                       qsTr("skip %1/%2 s").arg(appWindow.player.skipBackSeconds).arg(appWindow.player.skipForwardSeconds),
                                       core.setting("playlist.addTo", "end") === "next" ? qsTr("add as next") : qsTr("add to the end")])
                target: Qt.resolvedUrl("SettingsPlaybackPage.qml")
            }

            SettingsEntry {
                icon: page.icons.downloads
                showIcon: page.iconsOn
                title: qsTr("Downloads and storage")
                summary: page.settingsVersion >= 0
                         && page.join([core.setting("downloads.wifiOnly", "0") === "1" ? qsTr("Wi-Fi only") : "",
                                       core.setting("downloads.auto", "0") === "1" ? qsTr("new episodes automatically") : "",
                                       page.cleanupName(parseInt(core.setting("cleanup.days", "7")))])
                target: Qt.resolvedUrl("SettingsDownloadsPage.qml")
            }

            SettingsEntry {
                icon: page.icons.importExport
                showIcon: page.iconsOn
                title: qsTr("Import and export")
                // (312) Its own line for "Share your podcasts"
                summary: qsTr("OPML · gpodder-sailfish · archive folders · share")
                target: Qt.resolvedUrl("ImportExportPage.qml")
            }

            SettingsEntry {
                icon: page.icons.backup
                showIcon: page.iconsOn
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
                icon: page.icons.about
                showIcon: page.iconsOn
                title: qsTr("About Sailcast")
                summary: page.updateFound ? qsTr("Sailcast %1 available").arg(core.appUpdateVersion)
                                          : "Sailcast " + core.version
                summaryHighlighted: page.updateFound
                target: Qt.resolvedUrl("AboutPage.qml")
            }
        }

        VerticalScrollDecorator {}
    }
}

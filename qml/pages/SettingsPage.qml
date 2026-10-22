// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Settings: sync account (password in Sailfish Secrets), playback (skip
// times), downloads (Wi-Fi only, automatic downloads, clean-up), archive
// (folder, cloud address). Layout as Sailwave's settings page.
Page {
    id: page
    objectName: "settingsPage"

    readonly property var backends: ["nextcloud", "gpodder"]
    readonly property var skipOptions: [10, 15, 20, 30, 45, 60, 90]
    // -1: never, 0: right after listening, N: days after listening
    readonly property var cleanupOptions: [-1, 0, 1, 3, 7, 14, 30]
    // After an episode that was not started from the playlist (24)
    readonly property var afterSingleOptions: ["load", "play", "stop"]
    readonly property var startPages: ["home", "podcasts", "playlist"]
    readonly property var smartResumeOptions: [0, 5, 10, 20, 30]

    property bool passwordLoaded: false
    property string lastSyncText: lastSync()

    function lastSync() {
        var seconds = parseInt(core.setting("sync.last_sync", "0"))
        if (!seconds) {
            return qsTr("never")
        }
        return Qt.formatDateTime(new Date(seconds * 1000), Qt.DefaultLocaleShortDate)
    }

    function indexOf(list, value, fallback) {
        var i = list.indexOf(value)
        return i >= 0 ? i : fallback
    }

    readonly property bool nextcloud: backendBox.currentIndex === 0
    // Re-reads settings shown in bindings (the device, 51)
    property int settingsVersion: 0

    // Another server type (62): all entered access data go - the
    // stored account too, it belongs to the other type
    property int _shownBackend: indexOf(backends, core.setting("sync.backend", "nextcloud"), 0)
    function backendChosen(index) {
        if (index === _shownBackend) {
            return
        }
        _shownBackend = index
        core.cancelBrowserLogin()
        if (core.syncConfigured) {
            core.removeSyncAccount()
            secretStore.remove("sync-password")
            appWindow.showMessage(qsTr("Server type changed - the previous account was removed"))
        }
        serverField.text = ""
        userField.text = ""
        passwordField.text = ""
    }

    // Stores the account and synchronizes (button and browser login)
    function saveAccount(server, user, password) {
        if (!core.setSyncAccount(page.backends[backendBox.currentIndex], server, user)) {
            appWindow.showMessage(qsTr("Please check the server address and the user name"))
            return
        }
        // Normalized by the core (https://, no trailing slash)
        serverField.text = core.setting("sync.server", "")
        userField.text = user
        passwordField.text = password
        secretStore.store("sync-password", password)
        core.sync(password)
    }

    // Nextcloud login in the browser (Login Flow v2, 44)
    Connections {
        target: core
        onBrowserLoginUrl: Qt.openUrlExternally(url)
        onBrowserLoginFinished: page.saveAccount(server, user, appPassword)
        onBrowserLoginFailed: appWindow.showMessage(error)
        onSettingsChanged: page.settingsVersion++
    }

    Component.onDestruction: core.cancelBrowserLogin()

    Component.onCompleted: {
        if (core.syncConfigured) {
            secretStore.load("sync-password")
        }
    }

    Connections {
        target: secretStore
        onLoaded: {
            if (name === "sync-password" && !page.passwordLoaded) {
                page.passwordLoaded = true
                if (ok) {
                    passwordField.text = value
                }
            }
        }
    }

    Connections {
        target: core
        onSyncingChanged: page.lastSyncText = page.lastSync()
    }

    RemorsePopup {
        id: remorse
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


            // ---------------------------------------------------------
            // App start (43)
            SectionHeader { text: qsTr("App start") }

            TextSwitch {
                text: qsTr("Synchronize and update on start")
                description: qsTr("Otherwise only by hand: pull down → Update.")
                checked: core.setting("start.update", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("start.update", checked ? "1" : "0")
                }
            }

            ComboBox {
                label: qsTr("Start page")
                description: qsTr("Applies from the next start.")
                currentIndex: page.indexOf(page.startPages, core.setting("ui.startPage", "home"), 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("New episodes")
                        onClicked: core.setSetting("ui.startPage", "home")
                    }
                    MenuItem {
                        text: qsTr("Podcasts")
                        onClicked: core.setSetting("ui.startPage", "podcasts")
                    }
                    MenuItem {
                        text: qsTr("Playlist")
                        onClicked: core.setSetting("ui.startPage", "playlist")
                    }
                }
            }

            SectionHeader { text: qsTr("Synchronization") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Subscriptions and listening state are synchronized with other podcast apps (e.g. AntennaPod) through a Nextcloud with the app \"GPodder Sync\" or a gpodder.net compatible server. Folders, the playlist and per-podcast settings are not synchronized - they stay on this phone.")
            }

            ComboBox {
                id: backendBox
                label: qsTr("Server type")
                currentIndex: page.indexOf(page.backends, core.setting("sync.backend", "nextcloud"), 0)
                menu: ContextMenu {
                    MenuItem { text: qsTr("Nextcloud (GPodder Sync)"); onClicked: page.backendChosen(0) }
                    MenuItem { text: qsTr("gpodder.net API"); onClicked: page.backendChosen(1) }
                }
            }

            Label {
                visible: !page.nextcloud
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("gpodder.net or a server with the same API (e.g. oPodSync, mygpo): server address, user name and password of your account there.")
            }

            TextField {
                id: serverField
                width: parent.width
                text: core.setting("sync.server", "")
                label: qsTr("Server")
                placeholderText: backendBox.currentIndex === 0 ? "https://cloud.example.com" : "https://gpodder.net"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: userField.focus = true
            }

            // Nextcloud: sign in in the browser, no app password to type (44)
            Button {
                visible: page.nextcloud && !core.browserLoginActive
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Sign in with the browser")
                enabled: serverField.text.trim().length > 0 && !core.syncing
                onClicked: core.startBrowserLogin(serverField.text)
            }
            Row {
                visible: page.nextcloud && core.browserLoginActive
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                BusyIndicator {
                    anchors.verticalCenter: parent.verticalCenter
                    size: BusyIndicatorSize.Small
                    running: parent.visible
                }
                Label {
                    anchors.verticalCenter: parent.verticalCenter
                    text: qsTr("Waiting for the login in the browser …")
                    color: Theme.secondaryHighlightColor
                    font.pixelSize: Theme.fontSizeSmall
                }
            }
            Button {
                visible: page.nextcloud && core.browserLoginActive
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Cancel")
                onClicked: core.cancelBrowserLogin()
            }
            Label {
                visible: page.nextcloud
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Or enter the user name and an app password by hand:")
            }

            TextField {
                id: userField
                width: parent.width
                text: core.setting("sync.user", "")
                label: qsTr("User name")
                placeholderText: qsTr("User name")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: passwordField.focus = true
            }

            PasswordField {
                id: passwordField
                width: parent.width
                label: backendBox.currentIndex === 0 ? qsTr("App password") : qsTr("Password")
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            // gpodder.net API: which device of the account (51)
            ValueButton {
                visible: !page.nextcloud
                label: qsTr("Device")
                value: page.settingsVersion >= 0 && core.setting("sync.device", "sailcast")
                       + (core.setting("sync.deviceExisting", "0") === "1" ? "" : " " + qsTr("(new)"))
                description: qsTr("Enter server, user name and password first")
                enabled: serverField.text.trim().length > 0 && userField.text.trim().length > 0
                         && passwordField.text.length > 0
                onClicked: pageStack.push(Qt.resolvedUrl("GpodderDeviceDialog.qml"),
                                          { server: serverField.text, user: userField.text, password: passwordField.text })
            }

            Label {
                visible: page.nextcloud
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Nextcloud: Settings → Security → Create new app password. The password is stored encrypted with Sailfish Secrets. Another server or user starts over with a first sync (nothing is lost on the server).")
            }

            Item { width: 1; height: Theme.paddingMedium }

            ButtonLayout {
                Button {
                    text: qsTr("Save and synchronize")
                    enabled: !core.syncing && serverField.text.trim().length > 0
                             && userField.text.trim().length > 0 && passwordField.text.length > 0
                    onClicked: {
                        passwordField.focus = false
                        page.saveAccount(serverField.text, userField.text, passwordField.text)
                    }
                }
                Button {
                    visible: core.syncConfigured
                    text: qsTr("Remove account")
                    enabled: !core.syncing
                    onClicked: remorse.execute(qsTr("Removing the sync account"), function() {
                        core.removeSyncAccount()
                        secretStore.remove("sync-password")
                        serverField.text = ""
                        userField.text = ""
                        passwordField.text = ""
                    })
                }
            }

            Label {
                visible: core.syncConfigured
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: core.syncing ? qsTr("Synchronizing …") : qsTr("Last sync: %1").arg(page.lastSyncText)
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Playback") }

            ComboBox {
                label: qsTr("After an episode outside the playlist")
                currentIndex: page.indexOf(page.afterSingleOptions, core.setting("player.afterSingle", "load"), 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Load the next playlist episode")
                        onClicked: core.setSetting("player.afterSingle", "load")
                    }
                    MenuItem {
                        text: qsTr("Play the next playlist episode")
                        onClicked: core.setSetting("player.afterSingle", "play")
                    }
                    MenuItem {
                        text: qsTr("Stop")
                        onClicked: core.setSetting("player.afterSingle", "stop")
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Episodes started on the playlist page are always followed by the next playlist episode. \"Load\" puts it into the player bar, paused - one tap continues.")
            }

            // Smart resume (53)
            ComboBox {
                label: qsTr("Rewind after a long pause")
                description: qsTr("After more than 5 minutes of pause (or a restart) playback continues a little earlier")
                currentIndex: page.indexOf(page.smartResumeOptions, parseInt(core.setting("player.smartResume", "10")), 2)
                menu: ContextMenu {
                    Repeater {
                        model: page.smartResumeOptions
                        MenuItem {
                            text: modelData > 0 ? qsTr("%n s", "", modelData) : qsTr("Off")
                            onClicked: core.setSetting("player.smartResume", "" + modelData)
                        }
                    }
                }
            }

            // Skip times side by side (44)
            Row {
                width: parent.width

                ComboBox {
                    width: parent.width / 2
                    label: qsTr("Skip back")
                    currentIndex: page.indexOf(page.skipOptions, appWindow.player.skipBackSeconds, 3)
                    menu: ContextMenu {
                        Repeater {
                            model: page.skipOptions
                            MenuItem {
                                text: qsTr("%n s", "", modelData)
                                onClicked: {
                                    appWindow.player.skipBackSeconds = modelData
                                    core.setSetting("player.skipBack", String(modelData))
                                }
                            }
                        }
                    }
                }

                ComboBox {
                    width: parent.width / 2
                    label: qsTr("Skip forward")
                    currentIndex: page.indexOf(page.skipOptions, appWindow.player.skipForwardSeconds, 3)
                    menu: ContextMenu {
                        Repeater {
                            model: page.skipOptions
                            MenuItem {
                                text: qsTr("%n s", "", modelData)
                                onClicked: {
                                    appWindow.player.skipForwardSeconds = modelData
                                    core.setSetting("player.skipForward", String(modelData))
                                }
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Downloads") }

            TextSwitch {
                text: qsTr("Only on Wi-Fi")
                description: qsTr("Over the mobile connection, a download started by hand asks first; automatic downloads wait.")
                checked: core.setting("downloads.wifiOnly", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.wifiOnly", checked ? "1" : "0")
                }
            }

            TextSwitch {
                text: qsTr("Download new episodes")
                description: qsTr("After every feed update, up to 3 new episodes per podcast are downloaded automatically.")
                checked: core.setting("downloads.auto", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.auto", checked ? "1" : "0")
                }
            }

            ComboBox {
                id: cleanupBox
                label: qsTr("Delete played downloads")
                currentIndex: page.indexOf(page.cleanupOptions, parseInt(core.setting("cleanup.days", "7")), 4)
                menu: ContextMenu {
                    Repeater {
                        model: page.cleanupOptions
                        MenuItem {
                            text: modelData < 0 ? qsTr("Never")
                                                : (modelData === 0 ? qsTr("Right after listening")
                                                                   : qsTr("After %n days", "", modelData))
                            onClicked: core.setSetting("cleanup.days", String(modelData))
                        }
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("\"Right after listening\": as soon as an episode is played; otherwise checked at every start. Never in archived podcasts or the archive folder.")
            }

            Item { width: 1; height: Theme.paddingMedium }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.cleanupOptions[cleanupBox.currentIndex] >= 0
                text: qsTr("Clean up now")
                onClicked: core.cleanupDownloads(-1)
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Archive") }

            TextField {
                id: archiveField
                width: parent.width
                text: core.archiveDirectory()
                label: qsTr("Archive folder")
                placeholderText: core.defaultArchiveDirectory()
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus && text !== core.archiveDirectory()) {
                        core.setArchiveDirectory(text)
                        text = core.archiveDirectory()
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("One folder per podcast with all episodes, the feed, the cover and episodes.json. The app may only write inside Music (Sailjail). Empty: %1").arg(core.defaultArchiveDirectory())
            }

            TextField {
                id: archiveUrlField
                width: parent.width
                text: core.setting("archive.url", "")
                label: qsTr("Cloud archive address")
                placeholderText: "http://127.0.0.1:8080/"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        core.setSetting("archive.url", text.trim())
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Played when an episode is neither on the phone nor available at its original address, e.g. \"rclone serve http\" of the archive copy in the cloud.")
            }

            // ---------------------------------------------------------
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

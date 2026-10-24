// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Settings → Synchronization (88): sync account (password in Sailfish
// Secrets), Nextcloud sign-in in the browser (44), gpodder.net device (51),
// "Sync und Update" (86), "Synchronize now".
Page {
    id: page
    objectName: "settingsSyncPage"

    readonly property var backends: ["nextcloud", "gpodder"]

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

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Synchronization")
            }

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
                // Synchronize with the stored account (86) - without saving
                Button {
                    visible: core.syncConfigured
                    text: qsTr("Synchronize now")
                    enabled: !core.syncing
                    onClicked: {
                        if (passwordField.text.length > 0) {
                            core.sync(passwordField.text)
                        } else {
                            appWindow.syncOnly()
                        }
                    }
                }
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

            // "Sync und Update" (86): one pulley entry or two
            TextSwitch {
                visible: core.syncConfigured
                text: qsTr("Sync and update together")
                description: checked ? qsTr("\"Update\" in the pull-down menu first synchronizes, then updates the feeds.")
                                     : qsTr("Two entries in the pull-down menu: \"Synchronize\" and \"Update\" (feeds only).")
                checked: appWindow.syncWithUpdate
                automaticCheck: false
                onClicked: {
                    appWindow.syncWithUpdate = !appWindow.syncWithUpdate
                    core.setSetting("sync.withUpdate", appWindow.syncWithUpdate ? "1" : "0")
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
        }

        VerticalScrollDecorator {}
    }
}

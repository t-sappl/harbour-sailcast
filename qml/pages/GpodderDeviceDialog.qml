// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Device of a gpodder.net API account (51). The API keeps subscriptions per
// device; other apps (e.g. KDE Kasts) ask the same. Choose an existing
// device of the account or a new one for Sailcast, and optionally let all
// devices share their subscriptions (sync group). Accepting stores the
// choice; another device than before starts over with a first sync.
Dialog {
    id: dialog
    objectName: "gpodderDeviceDialog"

    property string server: ""
    property string user: ""
    property string password: ""

    property var devices: []
    property bool loading: true
    property string error: ""
    // Chosen device id; "" = the new device named in the field
    property string chosen: core.setting("sync.deviceExisting", "0") === "1" ? core.setting("sync.device", "") : ""

    // A new device starts without subscriptions (78): sharing is switched on
    // by default for it, until the user changes the switch
    property bool shareTouched: false
    onChosenChanged: if (!shareTouched) shareSwitch.checked = chosen.length === 0

    canAccept: !loading && (chosen.length > 0 || newName.text.trim().length > 0)

    Component.onCompleted: core.listDevices(server, user, password)

    Connections {
        target: core
        onDevicesListed: {
            dialog.devices = devices
            dialog.loading = false
        }
        onDevicesFailed: {
            dialog.error = error
            dialog.loading = false
        }
    }

    onAccepted: {
        var id = chosen.length > 0 ? chosen : newName.text.trim()
        core.chooseDevice(id, chosen.length > 0)
        if (shareSwitch.checked) {
            var ids = [id]
            for (var i = 0; i < devices.length; i++) {
                if (ids.indexOf(devices[i].id) < 0) {
                    ids.push(devices[i].id)
                }
            }
            // A new device is registered on the server first (77)
            core.synchronizeDevices(ids, chosen.length > 0 ? "" : id)
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: qsTr("Device")
                acceptText: qsTr("Use")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("gpodder.net keeps the subscriptions per device. Use the device of another app (its subscriptions) or a new device for Sailcast.")
            }

            BusyIndicator {
                anchors.horizontalCenter: parent.horizontalCenter
                size: BusyIndicatorSize.Medium
                running: dialog.loading
                visible: running
            }

            Label {
                visible: dialog.error.length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.errorColor
                text: dialog.error
            }

            SectionHeader {
                visible: dialog.devices.length > 0
                text: qsTr("Devices of the account")
            }

            Repeater {
                model: dialog.devices
                BackgroundItem {
                    width: column.width
                    height: Theme.itemSizeMedium
                    highlighted: down || dialog.chosen === modelData.id
                    onClicked: dialog.chosen = modelData.id
                    Column {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            text: modelData.caption.length > 0 ? modelData.caption : modelData.id
                            color: parent.parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                            text: modelData.id + " · " + qsTr("%n subscriptions", "", modelData.subscriptions)
                        }
                    }
                }
            }

            SectionHeader { text: qsTr("New device") }

            TextField {
                id: newName
                width: parent.width
                text: dialog.chosen.length === 0 && core.setting("sync.device", "").length > 0
                      ? core.setting("sync.device", "sailcast") : "sailcast"
                label: qsTr("Name of the new device")
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                onActiveFocusChanged: {
                    if (activeFocus) {
                        dialog.chosen = ""
                    }
                }
                color: dialog.chosen.length === 0 ? Theme.highlightColor : Theme.secondaryColor
            }

            Label {
                visible: dialog.chosen.length === 0 && dialog.devices.length > 0 && !shareSwitch.checked
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                text: qsTr("A new device starts without subscriptions - choose the device of your other app above, or share the subscriptions below.")
            }

            TextSwitch {
                id: shareSwitch
                visible: dialog.devices.length > 0
                checked: dialog.chosen.length === 0
                onClicked: dialog.shareTouched = true
                text: qsTr("Share subscriptions with all devices")
                description: qsTr("Puts all devices of the account into one sync group: a podcast subscribed on one device appears on all of them.")
            }
        }

        VerticalScrollDecorator {}
    }
}

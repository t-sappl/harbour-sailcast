// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Device of a gpodder.net API account (51). The API keeps subscriptions per
// device; other apps (e.g. KDE Kasts) ask the same. Choose an existing
// device of the account or a new one for Sailcast, and optionally let
// devices share their subscriptions (sync group) - since 83 with a check
// box per device: preset to the devices already in the chosen device's
// group on the server, else all; unchecked members of that group are taken
// out of it. Accepting stores the choice; another device than before
// starts over with a first sync.
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
    onChosenChanged: {
        if (!shareTouched) {
            shareSwitch.checked = chosen.length === 0
        }
        if (!checksTouched) {
            presetChecks()
        }
    }

    // Devices to group with the chosen one (83): {id: true/false}. Always
    // replaced as a whole (no changes inside a property var)
    property var checkedIds: ({})
    property bool checksTouched: false

    function groupOf(id) {
        for (var i = 0; i < devices.length; i++) {
            if (devices[i].id === id) {
                return devices[i].group
            }
        }
        return -1
    }

    // The chosen device's group on the server, else all devices
    function presetChecks() {
        var group = chosen.length > 0 ? groupOf(chosen) : -1
        var map = {}
        for (var i = 0; i < devices.length; i++) {
            map[devices[i].id] = group < 0 || devices[i].group === group
        }
        checkedIds = map
    }

    function setChecked(id, value) {
        var map = {}
        for (var key in checkedIds) {
            map[key] = checkedIds[key]
        }
        map[id] = value
        checksTouched = true
        checkedIds = map
    }

    readonly property int checkedCount: {
        var n = 0
        for (var i = 0; i < devices.length; i++) {
            if (devices[i].id !== chosen && checkedIds[devices[i].id] === true) {
                n++
            }
        }
        return n
    }

    canAccept: !loading && (chosen.length > 0 || newName.text.trim().length > 0)

    Component.onCompleted: core.listDevices(server, user, password)

    Connections {
        target: core
        onDevicesListed: {
            dialog.devices = devices
            dialog.loading = false
            dialog.presetChecks()
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
            // Local arrays (not properties): push is fine here
            var ids = [id]
            var stop = []
            var group = chosen.length > 0 ? groupOf(chosen) : -1
            for (var i = 0; i < devices.length; i++) {
                var other = devices[i]
                if (other.id === id) {
                    continue
                }
                if (checkedIds[other.id] === true) {
                    ids.push(other.id)
                } else if (group >= 0 && other.group === group) {
                    // Unchecked member of the current group (83)
                    stop.push(other.id)
                }
            }
            if (ids.length > 1 || stop.length > 0) {
                // A new device is registered on the server first (77)
                core.synchronizeDevices(ids, chosen.length > 0 ? "" : id, stop)
            }
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
                    id: deviceItem
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
                            // (102) by id: Silica moves the children of a
                            // BackgroundItem into its content item
                            color: deviceItem.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: Theme.secondaryColor
                            text: modelData.id + " · " + qsTr("%n subscriptions", "", modelData.subscriptions)
                                  + (modelData.group >= 0 ? " · " + qsTr("shared") : "")
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
                text: qsTr("Share subscriptions")
                description: qsTr("Puts the devices checked below into one sync group with this one: a podcast subscribed on one device appears on all of them. Unchecked devices leave the group.")
            }

            // One check box per other device (83)
            Repeater {
                model: shareSwitch.checked ? dialog.devices : []
                TextSwitch {
                    visible: modelData.id !== dialog.chosen
                    x: Theme.paddingLarge
                    width: column.width - Theme.paddingLarge
                    text: modelData.caption.length > 0 ? modelData.caption : modelData.id
                    description: modelData.id
                    checked: dialog.checkedIds[modelData.id] === true
                    automaticCheck: false
                    onClicked: dialog.setChecked(modelData.id, !checked)
                }
            }

            Label {
                visible: shareSwitch.checked && dialog.devices.length > 0 && dialog.checkedCount === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                text: qsTr("No other device checked - nothing is shared.")
            }
        }

        VerticalScrollDecorator {}
    }
}

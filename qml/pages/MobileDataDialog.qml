// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Asked before a download the user started while "Downloads only on Wi-Fi"
// is on and there is no Wi-Fi connection. Automatic downloads never ask -
// they just wait for the next update on Wi-Fi.
Dialog {
    id: dialog
    objectName: "mobileDataDialog"

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            acceptText: qsTr("Download")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeLarge
            text: qsTr("No Wi-Fi connection")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
            text: qsTr("Downloads are set to Wi-Fi only. Download this episode over the mobile connection anyway?")
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Asked before a download the user started while "Downloads only on Wi-Fi"
// is on and there is no Wi-Fi connection. Automatic downloads never ask -
// they just wait for the next update on Wi-Fi.
// (245) Also before a video download or stream without Wi-Fi, with its size
// (texts set by the caller).
Dialog {
    id: dialog
    objectName: "mobileDataDialog"
    property string acceptLabel: qsTr("Download")
    property string message: qsTr("Downloads are set to Wi-Fi only. Download this episode over the mobile connection anyway?")
    // (274) A second choice besides accepting, e.g. "Sound only (about 40 MB)"
    // for a video with an audio version; empty = no such button. It closes
    // the dialog without accepting and says so with alternateChosen.
    property string alternateLabel: ""
    signal alternateChosen()
    property bool _alternate: false
    onRejected: {
        if (_alternate) {
            alternateChosen()
        }
    }

    Column {
        width: parent.width
        spacing: Theme.paddingLarge

        DialogHeader {
            acceptText: dialog.acceptLabel
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
            text: dialog.message
        }

        // (294) The last stock Button of the app: ActionPill at the page
        // margin like everywhere else
        ActionPill {
            visible: dialog.alternateLabel !== ""
            kind: "secondary"
            x: Theme.horizontalPageMargin
            text: dialog.alternateLabel
            onClicked: {
                dialog._alternate = true
                dialog.reject()
            }
        }
    }
}

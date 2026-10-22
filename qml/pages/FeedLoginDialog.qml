// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Login of a feed with HTTP Basic Auth (premium feeds, e.g. podcaster.de).
// The user name is stored in the database, the password in Sailfish
// Secrets ("feed-<id>", see harbour-sailcast.qml). Both are only sent to
// the host of the feed. Accepting with an empty user name removes the login.
Dialog {
    id: dialog
    objectName: "feedLoginDialog"
    property int podcastId: -1
    property string podcastTitle: ""
    property alias user: userField.text
    property alias password: passwordField.text

    canAccept: userField.text.trim().length === 0 || passwordField.text.length > 0

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            acceptText: userField.text.trim().length > 0 ? qsTr("Save") : qsTr("Remove login")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            text: qsTr("Login for %1").arg(dialog.podcastTitle)
        }

        TextField {
            id: userField
            width: parent.width
            focus: text.length === 0
            label: qsTr("User name")
            placeholderText: qsTr("User name")
            inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
            EnterKey.onClicked: passwordField.focus = true
        }

        PasswordField {
            id: passwordField
            width: parent.width
            focus: userField.text.length > 0
            label: qsTr("Password")
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.enabled: dialog.canAccept
            EnterKey.onClicked: dialog.accept()
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: qsTr("Only for feeds that ask for a user name and password (HTTP 401). The password is stored encrypted with Sailfish Secrets and only sent to the server of the feed. Leave the user name empty to remove the login.")
        }
    }
}

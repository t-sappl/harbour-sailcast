// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// A keyword of "Meine Interessen" (139): "Mehr davon" (kind 1) or "Nie"
// (kind 3). The other forms (English) are filled from the built-in word
// list and fyyd's category names while the word is typed, until they are
// edited by hand; without a translation a hint asks for the English word.
Dialog {
    id: dialog

    property int kind: 1
    property string oldWord: ""        // editing an existing keyword
    property var oldForms: []
    // Editing: the stored forms stay unless the word changes by hand
    property bool formsEdited: oldWord.length > 0

    canAccept: wordField.text.trim().length > 0

    onAccepted: {
        var forms = formsField.text.split(",")
        core.setKeyword(dialog.kind, wordField.text.trim(), forms, dialog.oldWord)
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            DialogHeader {
                title: dialog.kind === 3 ? qsTr("Never") : qsTr("More of this")
                acceptText: qsTr("Save")
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                text: dialog.kind === 3
                      ? qsTr("Podcasts with this word in title, description or category are never suggested. The word stays on the phone.")
                      : qsTr("Podcasts with this word in title or description are suggested; the word is also searched in Podcast Index and fyyd.")
            }

            Item { width: 1; height: Theme.paddingLarge }

            TextField {
                id: wordField
                width: parent.width
                label: qsTr("Keyword")
                placeholderText: dialog.kind === 3 ? qsTr("e.g. Rap") : qsTr("e.g. Klassik")
                text: dialog.oldWord
                inputMethodHints: Qt.ImhNoPredictiveText
                focus: dialog.oldWord.length === 0
                EnterKey.iconSource: "image://theme/icon-m-enter-next"
                EnterKey.onClicked: formsField.focus = true
                onTextChanged: {
                    if (!dialog.formsEdited) {
                        formsField.text = core.translateKeyword(text.trim()).join(", ")
                    }
                }
            }

            TextField {
                id: formsField
                width: parent.width
                label: qsTr("Also as (English, other forms; separated by commas)")
                placeholderText: qsTr("e.g. classical")
                text: dialog.oldForms.join(", ")
                inputMethodHints: Qt.ImhNoPredictiveText
                onActiveFocusChanged: {
                    if (activeFocus) {
                        dialog.formsEdited = true
                    }
                }
                EnterKey.iconSource: "image://theme/icon-m-enter-accept"
                EnterKey.onClicked: {
                    if (dialog.canAccept) {
                        dialog.accept()
                    }
                }
            }

            Label {
                visible: wordField.text.trim().length > 0 && formsField.text.trim().length === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("No translation known - if the English word differs, enter it above.")
            }

            Item { width: 1; height: Theme.paddingLarge }

            Button {
                visible: dialog.oldWord.length > 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Remove keyword")
                onClicked: {
                    core.removeKeyword(dialog.kind, dialog.oldWord)
                    pageStack.pop()
                }
            }
        }
    }
}

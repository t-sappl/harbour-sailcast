// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "TimeFormat.js" as TimeFormat

// Name and note of a new bookmark (89; own component since 171, also
// opened from the player bar and the landscape player column): docked at
// the bottom, modal - a tap beside it or a swipe down closes it; Enter goes from the name to
// the note and closes from there. Not a dialog page: most bookmarks are
// set while listening, often without any text.
DockedPanel {
    id: bookmarkPanel
    height: panelColumn.height + Theme.paddingLarge
    dock: Dock.Bottom
    modal: true

    // Opaque background (94): the panel itself is translucent and the
    // toolbar and play buttons showed through the fields
    Rectangle {
        anchors.fill: parent
        color: Theme.highlightDimmerColor
        Rectangle {
            anchors.fill: parent
            color: Theme.rgba(Theme.highlightBackgroundColor, 0.18)
        }
        Rectangle {
            width: parent.width
            height: Math.max(1, Theme.paddingSmall / 4)
            color: Theme.rgba(Theme.highlightColor, 0.5)
        }
    }

    property int bookmarkId: 0
    property int seconds: 0
    property bool saved: true

    function show(id, position) {
        bookmarkId = id
        seconds = position
        saved = false
        nameField.text = ""
        noteField.text = ""
        open = true
        nameField.forceActiveFocus()
        // (232) The first 5 bookmarks: a line in the panel (220-231 a
        // one-time hint at the top - under the panel's shade it could not
        // be read)
        _hintCount = parseInt(core.setting("hint.bookmarkPanel", "0")) || 0
        if (_hintCount < 5) {
            core.setSetting("hint.bookmarkPanel", "" + (_hintCount + 1))
        }
    }

    property int _hintCount: 5

    function save() {
        if (saved || bookmarkId <= 0) {
            return
        }
        saved = true
        var name = nameField.text.trim()
        var note = noteField.text.trim()
        if (name.length > 0 || note.length > 0) {
            core.editBookmark(bookmarkId, name, note)
        }
    }

    Component.onDestruction: save()

    onOpenChanged: {
        if (!open) {
            nameField.focus = false
            noteField.focus = false
            save()
        }
    }

    Column {
        id: panelColumn
        width: parent.width
        y: Theme.paddingSmall

        Item {
            width: parent.width
            height: Theme.itemSizeExtraSmall
            Label {
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                text: qsTr("Bookmark at %1").arg(TimeFormat.clock(bookmarkPanel.seconds))
                color: Theme.highlightColor
            }
            IconButton {
                anchors.right: parent.right
                anchors.rightMargin: Theme.paddingMedium
                anchors.verticalCenter: parent.verticalCenter
                icon.source: "image://theme/icon-m-close"
                onClicked: bookmarkPanel.open = false
            }
        }

        // (232) How it works - the first 5 times
        Label {
            visible: bookmarkPanel._hintCount < 5
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            text: qsTr("Name and note are optional – close with × or tap outside. All bookmarks: playlist page, pull down → Bookmarks")
        }

        TextField {
            id: nameField
            width: parent.width
            placeholderText: qsTr("Name (optional)")
            label: qsTr("Name")
            inputMethodHints: Qt.ImhNoPredictiveText
            EnterKey.iconSource: "image://theme/icon-m-enter-next"
            EnterKey.onClicked: noteField.forceActiveFocus()
        }

        TextField {
            id: noteField
            width: parent.width
            placeholderText: qsTr("Note (optional)")
            label: qsTr("Note")
            EnterKey.iconSource: "image://theme/icon-m-enter-accept"
            EnterKey.onClicked: bookmarkPanel.open = false
        }
    }
}

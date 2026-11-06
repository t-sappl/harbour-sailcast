// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (265, Thomas 6 Nov, mockup m11 variant A) The subtle arrow at the right
// edge of a row that opens a page of its own (rows that only open a choice
// menu have none). Smaller and paler than the arrow of 251.
Label {
    anchors {
        right: parent.right
        rightMargin: Theme.horizontalPageMargin
        verticalCenter: parent.verticalCenter
    }
    text: "›"
    font.pixelSize: Theme.fontSizeLarge
    color: Theme.secondaryColor
    opacity: 0.8
}

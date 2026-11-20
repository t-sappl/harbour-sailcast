// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Entry of the main settings page that opens a sub-page (88, as in
// Sailfeed): title and a one-line summary of the current values below it.
// (251) The big arrow went (Thomas: ugly); (265) a subtle one is back
// (PageArrow, mockup m11), as on the other rows that open a page.
BackgroundItem {
    id: root

    property alias title: titleLabel.text
    property alias summary: summaryLabel.text
    property url target
    // (346) Theme icon name ("icon-m-sync"), empty: none. Shown when
    // showIcon is set (SettingsPage: always, decided 17 Nov, 350)
    property string icon: ""
    property bool showIcon: false
    // (350) Summary in the accent colour ("Sailcast 1.6 available")
    property bool summaryHighlighted: false

    width: parent ? parent.width : Screen.width
    height: Math.max(Theme.itemSizeMedium, textColumn.height + 2 * Theme.paddingMedium)
    onClicked: if (target.toString() !== "") pageStack.push(target)

    Image {
        id: iconImage
        visible: root.showIcon && root.icon !== ""
        anchors {
            left: parent.left
            leftMargin: Theme.horizontalPageMargin
            verticalCenter: parent.verticalCenter
        }
        width: Theme.iconSizeMedium
        height: width
        sourceSize.width: width
        sourceSize.height: height
        source: visible ? "image://theme/" + root.icon + "?"
                          + (root.highlighted || root.summaryHighlighted ? Theme.highlightColor : Theme.secondaryColor)
                        : ""
    }

    Column {
        id: textColumn
        anchors {
            left: iconImage.visible ? iconImage.right : parent.left
            leftMargin: iconImage.visible ? Theme.paddingLarge : Theme.horizontalPageMargin
            right: parent.right
            // (327, Gemini) Room for the arrow: text ran up to it
            rightMargin: Theme.horizontalPageMargin + Theme.paddingLarge * 2
            verticalCenter: parent.verticalCenter
        }
        Label {
            id: titleLabel
            width: parent.width
            wrapMode: Text.Wrap
            color: root.highlighted ? Theme.highlightColor : Theme.primaryColor
        }
        Label {
            id: summaryLabel
            width: parent.width
            visible: text !== ""
            // Up to two lines (92: a long user name cut off "last sync …")
            // (312) three: Import and export has a second line of its own
            wrapMode: Text.Wrap
            maximumLineCount: 3
            elide: Text.ElideRight
            font.pixelSize: Theme.fontSizeExtraSmall
            color: root.summaryHighlighted ? Theme.highlightColor
                 : (root.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor)
        }
    }

    PageArrow {}
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (263, Thomas 6 Nov, mockup m10) The one button of the app, replacing the
// stock Silica Button (centred, narrow, grey: it looked like a text field
// next to the left-aligned settings rows). A rounded pill:
//   "primary"     filled, the main action of a page
//   "secondary"   outline only, side actions
//   "destructive" red outline, deletes / unsubscribes
// Side by side in a Flow (wraps with large fonts); alone in a Column it is
// given x: Theme.horizontalPageMargin by the page.
BackgroundItem {
    id: pill

    property string text: ""
    property string kind: "secondary"

    readonly property color _accent: kind === "destructive" ? "#ff6a60" : Theme.highlightColor
    // In a Flow the page has already inset the Flow; in a Column the pill
    // sits at the page margin itself
    readonly property bool _inFlow: parent && parent.hasOwnProperty("flow")
    readonly property real _maxWidth: _inFlow ? parent.width
                                      : (parent ? parent.width : Screen.width) - 2 * Theme.horizontalPageMargin

    width: Math.min(label.implicitWidth + 2 * Theme.paddingLarge, _maxWidth)
    // The touch target is a full extra-small row; the pill itself is
    // smaller and centred in it, which leaves air above and below it
    height: Theme.itemSizeExtraSmall
    opacity: enabled ? 1.0 : 0.4

    Rectangle {
        anchors.centerIn: parent
        width: parent.width
        height: Theme.itemSizeExtraSmall * 0.8
        radius: height / 2
        color: pill.kind === "primary"
               ? Theme.rgba(Theme.highlightBackgroundColor, pill.highlighted ? 0.6 : 0.4)
               : (pill.highlighted ? Theme.rgba(pill._accent, 0.15) : "transparent")
        border.width: pill.kind === "primary" ? 0 : 2
        border.color: Theme.rgba(pill._accent, pill.highlighted ? 0.9 : 0.55)
    }
    Label {
        id: label
        anchors.centerIn: parent
        width: Math.min(implicitWidth, pill.width - 2 * Theme.paddingLarge)
        truncationMode: TruncationMode.Fade
        font.pixelSize: Theme.fontSizeExtraSmall
        color: pill.kind === "destructive" ? pill._accent : Theme.primaryColor
        text: pill.text
    }
}

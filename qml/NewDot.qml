// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Marker for new episodes since the podcast was last opened. Since 68 a
// glowing ball like the page indicators in the top corners (the flat
// dot of 59 looked ugly) - Silica's GlassItem, the system's own
// "glass" light. Light blue, independent of the ambience; the GlassItem
// draws the glow around its ball, so the item is larger than the ball.
// backdrop (69, grid): a soft dark spot below the ball, so it glows on
// light covers as well.
Item {
    id: dot

    // Diameter of the whole glow
    property real size: Theme.itemSizeExtraSmall
    property bool backdrop: false

    width: size
    height: size

    // Soft dark spot: a few transparent discs, darker towards the middle
    Repeater {
        model: dot.backdrop ? 4 : 0
        Rectangle {
            anchors.centerIn: parent
            width: dot.size * (0.62 - index * 0.11)
            height: width
            radius: width / 2
            color: Theme.rgba("black", 0.16)
        }
    }

    GlassItem {
        anchors.fill: parent
        color: "#5fd0ff"
        radius: 0.25
        falloffRadius: 0.18
    }
}

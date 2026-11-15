// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (314) Two small solid triangles, up and down: "change the order" (as the
// switch of New episodes, 310 - drawn, the scaled theme chevrons ran into
// a grey blob). Used before the sorting on the Downloads page.
Column {
    id: arrows
    property color tint: Theme.secondaryHighlightColor
    readonly property real w: Math.round(Theme.iconSizeExtraSmall * 0.55)
    spacing: Math.max(2, Math.round(Theme.paddingSmall / 2))
    Repeater {
        model: 2
        Item {
            width: arrows.w
            height: Math.round(arrows.w / 2)
            clip: true
            Rectangle {
                width: Math.round(arrows.w / Math.SQRT2)
                height: width
                x: (arrows.w - width) / 2
                y: index === 0 ? parent.height - height / 2 : -height / 2
                rotation: 45
                color: arrows.tint
            }
        }
    }
}

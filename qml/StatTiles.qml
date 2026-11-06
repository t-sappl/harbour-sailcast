// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Small tiles with a value and a caption (162/163, as AntennaPod's podcast
// statistics): tiles = [{value, label}], three per row (five in landscape)
// (192) A Flow: the tiles of a last, incomplete row share its width - no
// gap (five tiles: three and two)
Flow {
    id: grid
    property var tiles: []
    // (253) A tile with "tappable: true" reacts to a tap (and shows "›")
    signal tileClicked(int index)
    x: Theme.horizontalPageMargin
    width: (parent ? parent.width : 0) - 2 * Theme.horizontalPageMargin
    // (192) No gap: 2 or 4 tiles in two columns, otherwise three (five in
    // landscape, fewer when there are fewer tiles)
    readonly property int columns: width > Theme.itemSizeHuge * 4 ? Math.max(1, Math.min(5, tiles.length))
                                             : ((tiles.length === 2 || tiles.length === 4) ? 2 : 3)
    function widthAt(index) {
        var full = Math.floor(tiles.length / columns) * columns
        var perRow = index < full ? columns : tiles.length - full
        return (width - (perRow - 1) * spacing) / perRow
    }
    spacing: Theme.paddingSmall
    // (192) All tiles as high as the highest (a two-line caption made one
    // tile stick out of its row)
    property real tileHeight: 0
    function measure() {
        var h = 0
        for (var i = 0; i < tileRepeater.count; i++) {
            var item = tileRepeater.itemAt(i)
            if (item) {
                h = Math.max(h, item.contentHeight)
            }
        }
        tileHeight = h + 2 * Theme.paddingMedium
    }

    Repeater {
        id: tileRepeater
        model: grid.tiles
        onItemAdded: grid.measure()
        Rectangle {
            readonly property real contentHeight: tileColumn.height
            onContentHeightChanged: grid.measure()
            width: grid.widthAt(index)
            height: grid.tileHeight
            radius: Theme.paddingSmall
            color: Theme.rgba(Theme.highlightBackgroundColor, tileMouse.pressed ? 0.3 : 0.12)
            MouseArea {
                id: tileMouse
                anchors.fill: parent
                enabled: modelData.tappable === true
                onClicked: grid.tileClicked(index)
            }
            Column {
                id: tileColumn
                x: Theme.paddingMedium
                y: Theme.paddingMedium
                width: parent.width - 2 * Theme.paddingMedium
                Label {
                    width: parent.width
                    text: modelData.value
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
                Label {
                    width: parent.width
                    text: modelData.label + (modelData.tappable === true ? " ›" : "")
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeTiny
                    color: Theme.secondaryHighlightColor
                }
            }
        }
    }
}

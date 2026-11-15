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
            // (314) "destructive: true" with "tappable": the tap deletes.
            // (318, variant 4, Thomas + Gemini) The tile stays blue like the
            // others (a red area broke the row); a small red bin before the
            // caption marks it
            readonly property bool red: modelData.destructive === true && modelData.tappable === true
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
                    // (289) A tile with "warning: true" (little space left) in the warning colour
                    color: modelData.warning === true ? Theme.errorColor : Theme.highlightColor
                }
                Item {
                    width: parent.width
                    height: captionLabel.height
                    // (318) Waste bin, drawn (lid, handle, body with two lines)
                    Item {
                        id: bin
                        visible: modelData.destructive === true && modelData.tappable === true
                        y: Math.round((captionLabel.firstLineHeight - height) / 2)
                        width: visible ? Math.round(Theme.fontSizeTiny * 0.75) : 0
                        height: Math.round(width * 1.15)
                        readonly property color tint: "#ff8a80"
                        readonly property real stroke: Math.max(1, Math.round(width / 9))
                        Rectangle { // handle
                            width: Math.round(bin.width * 0.4); height: Math.round(bin.height * 0.12)
                            x: (bin.width - width) / 2; y: 0
                            color: "transparent"; border.width: bin.stroke; border.color: bin.tint
                        }
                        Rectangle { // lid
                            width: bin.width; height: bin.stroke
                            y: Math.round(bin.height * 0.12); color: bin.tint
                        }
                        Rectangle { // body
                            width: Math.round(bin.width * 0.78); height: bin.height - Math.round(bin.height * 0.22)
                            x: (bin.width - width) / 2; y: Math.round(bin.height * 0.22)
                            radius: Math.max(1, bin.stroke); color: "transparent"
                            border.width: bin.stroke; border.color: bin.tint
                            Row {
                                anchors.centerIn: parent
                                spacing: Math.max(1, Math.round(parent.width / 5))
                                Repeater {
                                    model: 2
                                    Rectangle { width: bin.stroke; height: Math.round(bin.height * 0.42); color: bin.tint }
                                }
                            }
                        }
                    }
                    Label {
                        id: captionLabel
                        readonly property real firstLineHeight: fontMetrics.height
                        anchors.left: bin.right
                        anchors.leftMargin: bin.visible ? Theme.paddingSmall : 0
                        anchors.right: parent.right
                        // (316) A tile that deletes names the action itself ("Delete
                        // played") - no "›", that reads as "opens a page"
                        text: modelData.label + (modelData.tappable === true && modelData.destructive !== true ? " ›" : "")
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryHighlightColor
                        FontMetrics { id: fontMetrics; font: captionLabel.font }
                    }
                }
            }
        }
    }
}

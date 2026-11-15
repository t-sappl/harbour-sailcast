// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// One entry of the import/export page: title and an explanation below
// (326, settings restructured) With the arrow of rows that open something
// (file picker, page): set apart from the switches, as the blind test asked
BackgroundItem {
    id: item
    property string title: ""
    property string description: ""
    property bool arrow: true
    // (328, Gemini) A value next to the title, as with ValueButton (e.g. a count)
    property string value: ""

    width: parent ? parent.width : 0
    height: textColumn.height + 2 * Theme.paddingMedium
    opacity: enabled ? 1.0 : 0.4

    Column {
        id: textColumn
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin - (item.arrow ? Theme.paddingLarge * 3 : 0)
        anchors.verticalCenter: parent.verticalCenter

        Row {
            width: parent.width
            spacing: Theme.paddingMedium
            Label {
                width: Math.min(implicitWidth, parent.width - (valueLabel.visible ? valueLabel.width + parent.spacing : 0))
                text: item.title
                truncationMode: TruncationMode.Fade
                color: item.highlighted ? Theme.highlightColor : Theme.primaryColor
            }
            Label {
                id: valueLabel
                visible: item.value.length > 0
                text: item.value
                color: Theme.highlightColor
            }
        }
        Label {
            width: parent.width
            text: item.description
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: item.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
        }
    }

    PageArrow {
        visible: item.arrow
    }
}

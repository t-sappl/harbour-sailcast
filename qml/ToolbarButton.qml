// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Button of the episode page toolbar (36): a symbol (theme icon, a text
// such as "1,0×", or the sleep timer moon) with a small label below.
// active: highlighted (playlist entry, running sleep timer, open choices).
BackgroundItem {
    id: button

    property string iconSource: ""
    property string text: ""
    property string label: ""
    property bool moon: false
    property bool active: false

    readonly property color tint: !enabled ? Theme.rgba(Theme.primaryColor, 0.4)
                                           : (active || highlighted ? Theme.highlightColor : Theme.primaryColor)

    height: Theme.itemSizeMedium + Theme.paddingLarge

    // Without a caption (50: only status texts and the first-use hints)
    // the symbol sits in the middle
    Item {
        id: symbol
        anchors.horizontalCenter: parent.horizontalCenter
        y: button.label.length > 0 ? caption.y - height - Theme.paddingSmall / 2
                                   : (parent.height - height) / 2
        width: Theme.iconSizeMedium
        height: Theme.iconSizeMedium * 0.8

        Image {
            visible: button.iconSource !== ""
            anchors.centerIn: parent
            width: Theme.iconSizeMedium
            height: width
            sourceSize.width: width
            sourceSize.height: height
            source: button.iconSource !== "" ? button.iconSource + "?" + button.tint : ""
        }

        Label {
            visible: button.text !== ""
            anchors.centerIn: parent
            text: button.text
            font.pixelSize: Theme.fontSizeLarge
            color: button.tint
        }

        // Crescent (55): drawn by a small shader - Canvas drew nothing on
        // the device, neither with "destination-out" (50) nor with a
        // clipped path. Disc minus a shifted disc, soft edges.
        ShaderEffect {
            visible: button.moon
            anchors.centerIn: parent
            width: Math.round(Theme.iconSizeSmall * 0.9)
            height: width
            property color fill: button.tint
            property real edge: 1.5 / Math.max(1, width)
            fragmentShader: "
                varying highp vec2 qt_TexCoord0;
                uniform lowp float qt_Opacity;
                uniform lowp vec4 fill;
                uniform highp float edge;
                void main() {
                    highp float d1 = length(qt_TexCoord0 - vec2(0.5, 0.5));
                    highp float d2 = length(qt_TexCoord0 - vec2(0.88, 0.28));
                    highp float disc = 1.0 - smoothstep(0.5 - edge, 0.5, d1);
                    highp float cut = smoothstep(0.5 - edge, 0.5, d2);
                    lowp float a = disc * cut * fill.a * qt_Opacity;
                    gl_FragColor = vec4(fill.rgb * a, a);
                }"
        }
    }

    Label {
        id: caption
        visible: button.label.length > 0
        anchors {
            horizontalCenter: parent.horizontalCenter
            bottom: parent.bottom
            bottomMargin: Theme.paddingMedium
        }
        width: parent.width - Theme.paddingSmall
        horizontalAlignment: Text.AlignHCenter
        truncationMode: TruncationMode.Fade
        text: button.label
        font.pixelSize: Theme.fontSizeExtraSmall
        color: !button.enabled ? Theme.rgba(Theme.secondaryColor, 0.4)
                               : (button.active || button.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor)
    }
}

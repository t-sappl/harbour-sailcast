// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// A text wider than its place scrolls (184, as in Sailwave): 2 s still,
// then it runs to the left with a gap into a second copy, which stands
// where the first started - and again. (185) Only a little too long (less
// than a third cut off): back and forth by the missing piece instead.
// Only while "active" and the app is in front; otherwise faded.
// (195) Taken out of the player bar's title for its second line.
Item {
    id: box

    property string text: ""
    property color color: Theme.primaryColor
    property real fontSize: Theme.fontSizeSmall
    property bool bold: false
    property bool active: true
    // (196) The colour behind the text (opaque): while it runs, its edges
    // fade into it instead of a hard cut (transparent = no fading)
    property color fadeColor: "transparent"

    height: label.height
    clip: true

    readonly property bool overflow: label.implicitWidth > width
    readonly property real gap: Theme.paddingLarge * 3
    readonly property bool running: overflow && active && Qt.application.state === Qt.ApplicationActive
    readonly property real missing: label.implicitWidth - width
    readonly property bool swing: missing < width / 3
    readonly property real speed: 40 * Theme.pixelRatio   // px per second

    Row {
        id: ticker
        spacing: box.gap
        Label {
            id: label
            text: box.text
            width: box.overflow ? implicitWidth : box.width
            truncationMode: box.running ? TruncationMode.None : TruncationMode.Fade
            color: box.color
            font.pixelSize: box.fontSize
            font.bold: box.bold
        }
        Label {
            visible: box.running && !box.swing
            text: box.text
            color: box.color
            font.pixelSize: box.fontSize
            font.bold: box.bold
        }
    }

    // (196) Soft edges while running: the left one only once the text has
    // moved (at rest its start is fully visible)
    ShaderEffect {
        visible: box.running && box.fadeColor.a > 0 && ticker.x < 0
        anchors.left: parent.left
        width: Math.min(Theme.paddingLarge * 2, box.width / 4)
        height: parent.height
        property color fill: box.fadeColor
        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform lowp float qt_Opacity;
            uniform lowp vec4 fill;
            void main() {
                lowp float a = (1.0 - qt_TexCoord0.x) * qt_Opacity;
                gl_FragColor = vec4(fill.rgb * a, a);
            }"
    }
    ShaderEffect {
        visible: box.running && box.fadeColor.a > 0
        anchors.right: parent.right
        width: Math.min(Theme.paddingLarge * 2, box.width / 4)
        height: parent.height
        property color fill: box.fadeColor
        fragmentShader: "
            varying highp vec2 qt_TexCoord0;
            uniform lowp float qt_Opacity;
            uniform lowp vec4 fill;
            void main() {
                lowp float a = qt_TexCoord0.x * qt_Opacity;
                gl_FragColor = vec4(fill.rgb * a, a);
            }"
    }

    SequentialAnimation {
        id: swingAnimation
        running: box.running && box.swing
        loops: Animation.Infinite
        onRunningChanged: if (!running) ticker.x = 0
        PropertyAction { target: ticker; property: "x"; value: 0 }
        PauseAnimation { duration: 2000 }
        NumberAnimation {
            target: ticker
            property: "x"
            from: 0
            to: -box.missing
            duration: Math.max(600, box.missing / box.speed * 1000)
            easing.type: Easing.InOutQuad
        }
        PauseAnimation { duration: 1500 }
        NumberAnimation {
            target: ticker
            property: "x"
            to: 0
            duration: Math.max(600, box.missing / box.speed * 1000)
            easing.type: Easing.InOutQuad
        }
    }

    SequentialAnimation {
        id: tickerAnimation
        running: box.running && !box.swing
        loops: Animation.Infinite
        onRunningChanged: if (!running) ticker.x = 0
        PropertyAction { target: ticker; property: "x"; value: 0 }
        PauseAnimation { duration: 2000 }
        NumberAnimation {
            target: ticker
            property: "x"
            from: 0
            to: -(label.implicitWidth + box.gap)
            // About 40 px per second at the phone's scale
            duration: Math.max(1000, (label.implicitWidth + box.gap) / box.speed * 1000)
        }
    }

    // A new text starts from the beginning
    onTextChanged: {
        if (tickerAnimation.running) {
            tickerAnimation.restart()
        }
        if (swingAnimation.running) {
            swingAnimation.restart()
        }
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6

// (255, usability check 254: the star was read as "favourite", not as a
// bookmark at a position) Bookmark ribbon with a swallowtail notch, as the
// bookmark symbol of e-book readers. Outline in the toolbars, filled in
// lists, on covers and in the bookmark list (Thomas 6 Nov, variant A).
// Drawn by a shader like PlaylistIcon (Canvas drew nothing on the device).
ShaderEffect {
    id: icon

    property color color: "white"
    property bool filled: false
    property real fill: filled ? 1.0 : 0.0
    // Soft edge in grid units (24 per width)
    property real edge: 36.0 / Math.max(1, width)

    width: 32
    height: width

    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform lowp float qt_Opacity;
        uniform lowp vec4 color;
        uniform highp float fill;
        uniform highp float edge;
        highp float box(highp vec2 p, highp vec2 c, highp vec2 h) {
            highp vec2 q = abs(p - c) - h;
            return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
        }
        void main() {
            highp vec2 p = qt_TexCoord0 * 24.0;
            // The band (13 wide, 18 high, rounded corners) minus the notch
            // at the bottom (apex at y = 15, a bit higher than centred)
            highp float band = box(p, vec2(12.0, 12.0), vec2(5.7, 8.2)) - 0.8;
            highp float notch = (p.y - 15.0 - abs(p.x - 12.0) * 0.85) / 1.31;
            highp float d = max(band, notch);
            // Outline: a stroke of 1.7 units inside the shape
            highp float ring = abs(d + 0.85) - 0.85;
            highp float s = fill > 0.5 ? d : ring;
            lowp float a = (1.0 - smoothstep(-0.5 * edge, 0.5 * edge, s)) * color.a * qt_Opacity;
            gl_FragColor = vec4(color.rgb * a, a);
        }"
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6

// (254, usability audit #6) Playlist symbol: three lines with a "+" (not in
// the playlist - a tap adds it) or a check mark (in the playlist). Thomas
// 5 Nov: the theme's icon-m-media-playlists looked like a second play
// button. Same shape as Android's Material playlist_add / playlist_add_check
// (lines of a 24 unit grid). Drawn by a shader like the camera (245) and the
// moon (55) - Canvas drew nothing on the device.
ShaderEffect {
    id: icon

    property color color: "white"
    property bool checked: false
    property real mark: checked ? 1.0 : 0.0
    // Soft edge in grid units (24 per width)
    property real edge: 36.0 / Math.max(1, width)

    width: 32
    height: width

    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform lowp float qt_Opacity;
        uniform lowp vec4 color;
        uniform highp float mark;
        uniform highp float edge;
        highp float box(highp vec2 p, highp vec2 c, highp vec2 h) {
            highp vec2 q = abs(p - c) - h;
            return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0);
        }
        highp float seg(highp vec2 p, highp vec2 a, highp vec2 b) {
            highp vec2 pa = p - a;
            highp vec2 ba = b - a;
            highp float h = clamp(dot(pa, ba) / dot(ba, ba), 0.0, 1.0);
            return length(pa - ba * h);
        }
        void main() {
            highp vec2 p = qt_TexCoord0 * 24.0;
            // The three lines of the list
            highp float d = box(p, vec2(8.5, 7.0), vec2(5.5, 1.0));
            d = min(d, box(p, vec2(8.5, 11.0), vec2(5.5, 1.0)));
            d = min(d, box(p, vec2(6.5, 15.0), vec2(3.5, 1.0)));
            // Bottom right: a plus, or a check mark
            highp float plus = min(box(p, vec2(17.0, 15.0), vec2(1.0, 4.0)),
                                   box(p, vec2(17.0, 15.0), vec2(4.0, 1.0)));
            highp float check = min(seg(p, vec2(13.8, 16.4), vec2(16.6, 19.2)),
                                    seg(p, vec2(16.6, 19.2), vec2(21.6, 14.2))) - 1.0;
            d = min(d, mark > 0.5 ? check : plus);
            lowp float a = (1.0 - smoothstep(-0.5 * edge, 0.5 * edge, d)) * color.a * qt_Opacity;
            gl_FragColor = vec4(color.rgb * a, a);
        }"
}

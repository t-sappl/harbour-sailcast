// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6

// (245) Small video camera (outlined body, filled lens) for video episodes
// and video podcasts, in any colour. Drawn by a shader like the moon of
// the toolbar (55) - Canvas drew nothing on the device.
ShaderEffect {
    id: icon

    property color color: "white"
    property real edge: 1.5 / Math.max(1, width)

    width: 32
    height: width

    fragmentShader: "
        varying highp vec2 qt_TexCoord0;
        uniform lowp float qt_Opacity;
        uniform lowp vec4 color;
        uniform highp float edge;
        highp float box(highp vec2 p, highp vec2 c, highp vec2 b, highp float r) {
            highp vec2 q = abs(p - c) - b + vec2(r);
            return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
        }
        void main() {
            highp vec2 p = qt_TexCoord0;
            // Body: outline of a rounded rectangle
            highp float d = box(p, vec2(0.37, 0.5), vec2(0.31, 0.24), 0.09);
            highp float body = 1.0 - smoothstep(0.045 - edge, 0.045, abs(d));
            // Lens: a trapezoid widening to the right
            highp float lens = 0.0;
            if (p.x > 0.70 && p.x < 0.97) {
                highp float h = 0.07 + (p.x - 0.70) / 0.27 * 0.16;
                lens = 1.0 - smoothstep(h - edge, h, abs(p.y - 0.5));
                lens *= smoothstep(0.70, 0.70 + edge, p.x) * (1.0 - smoothstep(0.97 - edge, 0.97, p.x));
            }
            lowp float a = max(body, lens) * color.a * qt_Opacity;
            gl_FragColor = vec4(color.rgb * a, a);
        }"
}

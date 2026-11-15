// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (309) Thin ring with the progress of a download: a faint closed circle and
// the accent arc on top (at least 4 %, so the start shows as well). Repaints
// when it is shown again (a Canvas may lose its drawing while hidden).
Canvas {
    id: ring
    property real progress: 0
    readonly property real fraction: Math.max(0.04, Math.min(1, progress))
    onFractionChanged: requestPaint()
    onVisibleChanged: if (visible) requestPaint()
    onWidthChanged: requestPaint()
    onPaint: {
        var ctx = getContext("2d")
        ctx.reset()
        var lw = Math.max(2, Math.round(Theme.paddingSmall / 3))
        var r = (width - lw) / 2
        ctx.lineWidth = lw
        ctx.lineCap = "round"
        ctx.strokeStyle = Theme.rgba(Theme.primaryColor, 0.2)
        ctx.beginPath()
        ctx.arc(width / 2, height / 2, r, 0, 2 * Math.PI)
        ctx.stroke()
        ctx.strokeStyle = Theme.highlightColor
        ctx.beginPath()
        ctx.arc(width / 2, height / 2, r, -Math.PI / 2, -Math.PI / 2 + 2 * Math.PI * fraction)
        ctx.stroke()
    }
}

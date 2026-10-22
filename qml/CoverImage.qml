// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Square podcast cover or episode image with a neutral placeholder (first
// letter) while there is no image yet.
//
// "source" is the remote URL; the image itself comes from the local,
// scaled-down copy of the image cache (context property "covers", see
// src/imagecache.h). The first time an image is needed it is fetched in the
// background and appears as soon as it is stored; afterwards it is a small
// local file, which QML also keeps in its pixmap cache - no flickering and
// no reloading while scrolling. No fade-in on purpose: recycled delegates
// show their image at once.
Rectangle {
    id: root
    property string source: ""
    property string fallbackText: ""
    // Short fade-in (51): for pages that create all covers at once (the
    // podcast grid) - images decoded one after the other then appear
    // calmly instead of popping in. Off in recycling lists (no flicker).
    property bool fadeIn: false
    readonly property bool ready: image.status === Image.Ready

    // Local URL (or "" while it is being fetched)
    property string _local: covers.url(source)

    onSourceChanged: _local = covers.url(source)

    Connections {
        target: covers
        onCached: {
            if (remote === root.source) {
                root._local = localUrl
            }
        }
    }

    // Placeholder (51): a calm colour of its own per podcast and up to two
    // initials, so a grid that is still loading does not look empty
    readonly property real _hue: {
        var h = 0
        for (var i = 0; i < fallbackText.length; i++) {
            h = (h * 31 + fallbackText.charCodeAt(i)) % 360
        }
        return h / 360
    }
    color: fallbackText.length > 0 ? Qt.hsla(_hue, 0.35, 0.35, 0.55) : Theme.rgba(Theme.highlightBackgroundColor, 0.15)
    radius: Theme.paddingSmall / 2
    clip: true

    Label {
        anchors.centerIn: parent
        visible: image.status !== Image.Ready
        text: {
            var words = root.fallbackText.replace(/[^A-Za-z0-9\u00C0-\u024F ]/g, " ").split(" ").filter(function(w) { return w.length > 0 })
            if (words.length === 0) {
                return root.fallbackText.length > 0 ? root.fallbackText.charAt(0).toUpperCase() : ""
            }
            return (words[0].charAt(0) + (words.length > 1 ? words[1].charAt(0) : "")).toUpperCase()
        }
        font.pixelSize: root.height * 0.36
        color: Theme.primaryColor
    }

    Image {
        id: image
        anchors.fill: parent
        source: root._local
        // Twice the shown size is enough for sharp images on any screen;
        // the cached files are at most 600 pixels anyway
        sourceSize.width: Math.min(600, Math.round(root.width * 2))
        sourceSize.height: Math.min(600, Math.round(root.height * 2))
        fillMode: Image.PreserveAspectCrop
        asynchronous: true
        cache: true
        smooth: true
        visible: status === Image.Ready
        opacity: root.fadeIn && status !== Image.Ready ? 0 : 1
        Behavior on opacity {
            enabled: root.fadeIn
            NumberAnimation { duration: 180 }
        }
    }
}

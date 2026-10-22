// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Episode image with the podcast cover as a small badge in the bottom right
// corner - only if the episode has an image of its own that is not just
// the cover again (many feeds repeat the cover under another URL; compared
// by content, see covers.differentImages). The badge appears once both
// images are in the cache. Used by the player bar, the episode page and the
// app cover; the lock screen gets the same layout from ArtComposer.
Item {
    id: root

    property string episodeImageUrl: ""
    property string coverUrl: ""
    property string fallbackText: ""
    // Badge size relative to the image
    property real badgeFactor: 0.4

    // Re-checked when one of the two images arrives in the cache
    property int _cacheRevision: 0
    readonly property bool hasBadge: _cacheRevision >= 0 && covers.differentImages(episodeImageUrl, coverUrl)

    Connections {
        target: covers
        onCached: {
            if (remote === root.episodeImageUrl || remote === root.coverUrl) {
                root._cacheRevision += 1
            }
        }
    }

    CoverImage {
        id: main
        anchors.fill: parent
        source: root.episodeImageUrl.length > 0 ? root.episodeImageUrl : root.coverUrl
        fallbackText: root.fallbackText
    }

    // Light frame around the cover, slightly outside the image like in
    // Sailwave's player bar
    Rectangle {
        visible: root.hasBadge
        readonly property real overhang: Math.round(Theme.paddingSmall / 2)
        width: Math.round(root.width * root.badgeFactor)
        height: width
        x: root.width - width + overhang
        y: root.height - height + overhang
        radius: width * 0.22
        color: "white"

        CoverImage {
            anchors.fill: parent
            anchors.margins: Math.max(1, Math.round(parent.width * 0.06))
            radius: parent.radius * 0.7
            source: root.hasBadge ? root.coverUrl : ""
            fallbackText: root.fallbackText
        }
    }
}

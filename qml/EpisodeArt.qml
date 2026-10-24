// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Episode image with the podcast cover as a small badge in the bottom right
// corner - only if the episode has an image of its own that is not just
// the cover again (many feeds repeat the cover under another URL; compared
// by content, see covers.differentImages). The badge appears once both
// images are in the cache. Shown in the player bar and on the app cover
// (and, from ArtComposer, on the lock screen) - not on the episode page and
// in the landscape player column, where the podcast name is right above
// (96: a cover inside a cover looked busy there). Since 96 a fine light edge
// instead of Sailwave's white frame (made for station logos with a
// transparent background), no overhang.
Item {
    id: root

    property string episodeImageUrl: ""
    property string coverUrl: ""
    property string fallbackText: ""
    // Badge size relative to the image
    property real badgeFactor: 0.4
    // false: never a badge (96)
    property bool showBadge: true

    // Re-checked when one of the two images arrives in the cache
    property int _cacheRevision: 0
    readonly property bool hasBadge: showBadge && _cacheRevision >= 0 && covers.differentImages(episodeImageUrl, coverUrl)

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

    // Fine light edge around the cover badge, flush with the corner (96)
    Rectangle {
        visible: root.hasBadge
        width: Math.round(root.width * root.badgeFactor)
        height: width
        x: root.width - width
        y: root.height - height
        radius: width * 0.12
        color: Theme.rgba("white", 0.85)

        CoverImage {
            anchors.fill: parent
            anchors.margins: Math.max(1, Math.round(parent.width * 0.025))
            radius: parent.radius * 0.8
            source: root.hasBadge ? root.coverUrl : ""
            fallbackText: root.fallbackText
        }
    }
}

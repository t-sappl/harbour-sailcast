// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (253, usability audit #10) Pulley entry "Downloads (12 · ↓ 2)" for the
// New episodes and Podcasts pages - before only in the Playlist pulley
// (forum: "I had to search a while"). Same text as there (218): downloaded
// episodes, "↓ N" for running ones. The count is read when the menu opens:
// the page calls refresh() from its PullDownMenu's onActiveChanged.
MenuItem {
    id: item

    property int downloaded: 0
    function refresh() {
        downloaded = core.downloadedCount()
    }

    visible: core.podcastCount > 0
    text: {
        var parts = []
        if (item.downloaded > 0) {
            parts.push(item.downloaded)
        }
        if (core.downloadCount > 0) {
            parts.push("↓ " + core.downloadCount)
        }
        return parts.length > 0 ? qsTr("Downloads (%1)").arg(parts.join(" · ")) : qsTr("Downloads")
    }
    onClicked: pageStack.push(Qt.resolvedUrl("pages/DownloadsPage.qml"))
}

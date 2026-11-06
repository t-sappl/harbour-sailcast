// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// (258, 259) "Share..." of an episode, from the row menu and the episode
// page: the link, the link from a position (episode page), the file - or,
// when it is not downloaded, an offer to download it first. The share itself
// is started from the app window after this page has closed (25).
// Layout as the other pages: header with the episode name, full rows with a
// grey explanation (Thomas 6 Nov, mockup m8).
Page {
    id: page
    objectName: "shareChoicePage"

    property int episodeId: 0
    // Seconds; > 0 adds "Share from ..."
    property real position: 0

    property var episode: core.episode(episodeId)
    property bool downloading: core.isDownloading(episodeId)
    property real downloadProgress: core.downloadProgress(episodeId)
    readonly property bool downloaded: episode.downloaded === true
    readonly property bool hasLink: (core.shareLink(episodeId, 0).url || "").length > 0

    Connections {
        target: core
        onEpisodeChanged: {
            if (episodeId === page.episodeId) {
                page.episode = core.episode(page.episodeId)
                page.downloading = core.isDownloading(page.episodeId)
            }
        }
        onDownloadProgressChanged: {
            if (episodeId === page.episodeId) {
                page.downloading = true
                page.downloadProgress = progress
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        contentHeight: column.height

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Share")
                description: page.episode.title || ""
            }

            BackgroundItem {
                width: parent.width
                height: Theme.itemSizeMedium
                enabled: page.hasLink
                opacity: enabled ? 1.0 : 0.4
                onClicked: {
                    pageStack.pop()
                    appWindow.shareEpisode(page.episodeId, 0)
                }
                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.horizontalPageMargin
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width
                        text: qsTr("Share link")
                        color: parent.parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        text: page.hasLink ? qsTr("Web address of the episode")
                                           : qsTr("This episode has no address that can be shared")
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                }
            }

            BackgroundItem {
                visible: page.position > 0 && page.hasLink
                width: parent.width
                height: Theme.itemSizeMedium
                onClicked: {
                    pageStack.pop()
                    appWindow.shareEpisode(page.episodeId, page.position)
                }
                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.horizontalPageMargin
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width
                        text: qsTr("Share link from %1").arg(TimeFormat.clock(page.position))
                        color: parent.parent.highlighted ? Theme.highlightColor : Theme.primaryColor
                    }
                    Label {
                        width: parent.width
                        text: qsTr("Opens the episode at this position")
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                }
            }

            // The file: shared when downloaded; else the way to get it
            BackgroundItem {
                width: parent.width
                height: Theme.itemSizeMedium
                onClicked: {
                    if (page.downloaded) {
                        pageStack.pop()
                        appWindow.shareEpisodeFile(page.episodeId)
                    } else if (page.downloading) {
                        core.cancelDownload(page.episodeId)
                    } else {
                        appWindow.requestDownload(page.episodeId)
                    }
                }
                Column {
                    anchors {
                        left: parent.left
                        right: parent.right
                        leftMargin: Theme.horizontalPageMargin
                        rightMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    Label {
                        width: parent.width
                        text: page.downloaded ? qsTr("Share file")
                              : (page.downloading ? qsTr("Downloading... %1 %").arg(Math.round(page.downloadProgress * 100))
                                                  : qsTr("Download the episode"))
                        color: page.downloaded ? (parent.parent.highlighted ? Theme.highlightColor : Theme.primaryColor)
                                               : Theme.highlightColor
                    }
                    Label {
                        width: parent.width
                        text: page.downloaded ? qsTr("The downloaded file")
                              : (page.downloading ? qsTr("A tap cancels; the file can be shared when it is done")
                                                  : qsTr("Then you can share the file"))
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                    }
                }
            }
        }
        VerticalScrollDecorator {}
    }
}

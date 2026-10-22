// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// Downloads: running and waiting ones on top (progress, cancel), below all
// downloaded episodes (newest first, delete in the context menu). The
// header shows the number of files and the space they take. Files in the
// archive folder are listed too but are only deleted with the archive.
Page {
    id: page
    objectName: "downloadsPage"

    property var active: core.activeDownloads()
    property var stats: core.downloadStats()

    function refresh() {
        active = core.activeDownloads()
        stats = core.downloadStats()
    }

    Connections {
        target: core
        onDownloadsChanged: page.refresh()
        onDataChanged: page.stats = core.downloadStats()
    }

    EpisodeListModel {
        id: downloaded
        filter: EpisodeListModel.Downloaded
    }

    RemorsePopup {
        id: remorse
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: downloaded
        cacheBuffer: height

        PullDownMenu {
            MenuItem {
                visible: page.active.length > 0
                text: qsTr("Cancel all downloads")
                onClicked: remorse.execute(qsTr("Cancelling downloads"), function() { core.cancelAllDownloads() })
            }
            MenuItem {
                visible: downloaded.count > 0
                text: qsTr("Delete played downloads")
                onClicked: remorse.execute(qsTr("Deleting played downloads"), function() { core.cleanupDownloads(0) })
            }
        }

        header: Column {
            width: list.width

            PageHeader {
                title: qsTr("Downloads")
                description: qsTr("%n files", "", page.stats.count || 0) + " · "
                             + TimeFormat.size(page.stats.bytes || 0, qsTr("%1 MB"), qsTr("%1 GB"))
            }

            SectionHeader {
                visible: page.active.length > 0
                text: qsTr("Running")
            }

            Repeater {
                id: activeRepeater
                model: page.active

                delegate: ListItem {
                    id: activeRow
                    property int episodeId: modelData.episodeId
                    property real progress: modelData.progress
                    property bool running: modelData.running
                    // Only the bar of this row - no reload of the whole
                    // section. Here in the row: the Repeater is in the list
                    // header, its id is not visible to the page (39)
                    Connections {
                        target: core
                        onDownloadProgressChanged: {
                            if (episodeId === activeRow.episodeId) {
                                activeRow.progress = progress
                                activeRow.running = true
                            }
                        }
                    }
                    width: list.width
                    contentHeight: Theme.itemSizeLarge
                    onClicked: pageStack.push(Qt.resolvedUrl("EpisodePage.qml"), { episodeId: activeRow.episodeId })

                    menu: Component {
                        ContextMenu {
                            MenuItem {
                                text: qsTr("Cancel download")
                                onClicked: core.cancelDownload(activeRow.episodeId)
                            }
                        }
                    }

                    CoverImage {
                        id: activeArt
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        height: width
                        source: modelData.imageUrl
                        fallbackText: modelData.podcastTitle
                    }

                    Column {
                        anchors {
                            left: activeArt.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            text: modelData.title
                            truncationMode: TruncationMode.Fade
                            color: activeRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            text: activeRow.running
                                  ? modelData.podcastTitle + " · " + Math.round(activeRow.progress * 100) + " %"
                                  : modelData.podcastTitle + " · " + qsTr("waiting")
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: activeRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                        Item { width: 1; height: Theme.paddingSmall }
                        // Progress line as in the episode rows
                        Rectangle {
                            width: parent.width
                            height: Math.max(2, Math.round(Theme.paddingSmall / 2))
                            radius: height / 2
                            color: Theme.rgba(Theme.secondaryColor, 0.3)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, activeRow.progress))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.highlightColor
                            }
                        }
                    }
                }
            }

            SectionHeader {
                visible: downloaded.count > 0
                text: qsTr("Downloaded")
            }
        }

        delegate: EpisodeDelegate {}

        ViewPlaceholder {
            enabled: downloaded.count === 0 && page.active.length === 0
            text: qsTr("No downloads")
            hintText: qsTr("Download episodes from their menu or page")
        }

        VerticalScrollDecorator {}
    }
}

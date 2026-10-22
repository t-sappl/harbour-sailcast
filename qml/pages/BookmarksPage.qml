// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// All bookmarks (47), newest first: image, episode, podcast, time and note.
// Tap plays the episode from there; menu: open the episode, edit the note,
// delete (countdown in the app window).
Page {
    id: page
    objectName: "bookmarksPage"

    property var bookmarkList: core.bookmarks(0)

    Connections {
        target: core
        onBookmarksChanged: page.bookmarkList = core.bookmarks(0)
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: page.bookmarkList

        header: PageHeader {
            title: qsTr("Bookmarks")
        }

        delegate: ListItem {
            id: row
            width: list.width
            contentHeight: Math.max(Theme.itemSizeLarge, textColumn.height + 2 * Theme.paddingMedium)
            onClicked: appWindow.player.playFrom(modelData.episodeId, modelData.position, false)

            menu: Component {
                ContextMenu {
                    MenuItem {
                        text: qsTr("Open episode")
                        onClicked: pageStack.push(Qt.resolvedUrl("EpisodePage.qml"), { episodeId: modelData.episodeId })
                    }
                    MenuItem {
                        text: qsTr("Rename / note")
                        onClicked: appWindow.editBookmark(modelData)
                    }
                    MenuItem {
                        text: qsTr("Delete bookmark")
                        onClicked: appWindow.remorseDeleteBookmark(modelData.bookmarkId)
                    }
                }
            }

            CoverImage {
                id: cover
                x: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                width: Theme.itemSizeMedium
                height: width
                source: modelData.imageUrl
                fallbackText: modelData.podcastTitle
            }

            Column {
                id: textColumn
                anchors {
                    left: cover.right
                    leftMargin: Theme.paddingMedium
                    right: parent.right
                    rightMargin: Theme.horizontalPageMargin
                    verticalCenter: parent.verticalCenter
                }
                // Same order in every row (73: named and unnamed bookmarks
                // used to look different): bookmark and time, episode,
                // podcast, note
                Label {
                    width: parent.width
                    text: "★ " + (modelData.title.length > 0 ? modelData.title : qsTr("Bookmark"))
                          + " · " + TimeFormat.clock(modelData.position)
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                }
                Label {
                    width: parent.width
                    text: modelData.episodeTitle
                    wrapMode: Text.Wrap
                    maximumLineCount: 2
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeSmall
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: modelData.podcastTitle
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                }
                Label {
                    visible: modelData.note.length > 0
                    width: parent.width
                    text: modelData.note
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                }
            }
        }

        ViewPlaceholder {
            enabled: page.bookmarkList.length === 0
            text: qsTr("No bookmarks")
            hintText: qsTr("Set one with the star on the episode page")
        }

        VerticalScrollDecorator {}
    }
}

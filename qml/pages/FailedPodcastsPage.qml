// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// (355, Thomas 18 Nov) Podcasts whose last update failed, with the reason;
// from the hint line on the Podcasts page when there are several. Tap: the
// podcast's page (address, login, unsubscribe are there). (362) A hint what
// to do and, on long press, the two ways out: "Podcast has ended" and
// leaving it out of "Update" (same words as the podcast settings).
Page {
    // No player bar here (choice-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "failedPodcastsPage"

    property var podcasts: []
    // (review 355) A podcast fixed from here leaves the list on the way back
    onStatusChanged: {
        if (status === PageStatus.Activating && _seen) {
            reload()
        }
        if (status === PageStatus.Active) {
            _seen = true
        }
    }
    property bool _seen: false
    // (362) Called after a context-menu action, after the menu has closed
    // (the row disappears; removing it while the menu is open jumps)
    function reload() { reloadTimer.restart() }
    Timer {
        id: reloadTimer
        interval: 400
        onTriggered: page.podcasts = core.failedPodcasts()
    }

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.rightMargin: appWindow.playerPanelWidth
        model: page.podcasts
        header: Column {
            width: list.width
            PageHeader {
                title: qsTr("Not updated")
                description: qsTr("The last update of these podcasts failed")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Tap a podcast to check its address. A long press offers two ways out: \"Podcast has ended\" stops the updates of a podcast that has ended. \"Leave out of Update\" is for a server that is often down – the podcast is then only updated on its own page. Both can be undone in the podcast settings.")
            }
            Item { width: 1; height: Theme.paddingLarge }
        }
        delegate: ListItem {
            id: row
            width: ListView.view.width
            contentHeight: Math.max(Theme.itemSizeMedium, textColumn.height + 2 * Theme.paddingMedium)
            onClicked: pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: modelData.podcastId })
            menu: Component {
                ContextMenu {
                    MenuItem {
                        text: qsTr("Podcast has ended")
                        onClicked: {
                            core.setArchived(modelData.podcastId, true)
                            page.reload()
                        }
                    }
                    MenuItem {
                        text: qsTr("Leave out of Update")
                        onClicked: {
                            core.setManualUpdate(modelData.podcastId, true)
                            page.reload()
                        }
                    }
                    MenuItem {
                        text: qsTr("Podcast settings")
                        onClicked: pageStack.push(Qt.resolvedUrl("PodcastSettingsPage.qml"), { podcastId: modelData.podcastId })
                    }
                }
            }
            Column {
                id: textColumn
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                Label {
                    width: parent.width
                    text: modelData.title
                    truncationMode: TruncationMode.Fade
                    color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                }
                Label {
                    width: parent.width
                    text: modelData.error
                    wrapMode: Text.Wrap
                    maximumLineCount: 3
                    elide: Text.ElideRight
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.errorColor
                }
            }
        }
        ViewPlaceholder {
            enabled: page.podcasts.length === 0
            text: qsTr("All podcasts are fine again")
        }
        VerticalScrollDecorator {}
    }
}

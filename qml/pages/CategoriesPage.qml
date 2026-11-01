// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// "Kategorien durchstöbern" (150, from the Discover page): the top
// categories of fyyd's tree (German names) as tiles - the most listened
// first, then the most popular, then by name; categories set to "nie" at
// the end. A tap opens the category page.
Page {
    id: page

    property var info: core.interests()
    property var tiles: tilesOf(info)

    function tilesOf(data) {
        var list = []
        var all = data.categories || []
        for (var i = 0; i < all.length; i++) {
            if (all[i].parent.length === 0) {
                list.push(all[i])
            }
        }
        list.sort(function(a, b) {
            var na = a.level === 3 ? 1 : 0
            var nb = b.level === 3 ? 1 : 0
            if (na !== nb) {
                return na - nb
            }
            if (b.listened !== a.listened) {
                return b.listened - a.listened
            }
            var pa = a.popular >= 0 ? a.popular : 99
            var pb = b.popular >= 0 ? b.popular : 99
            return pa !== pb ? pa - pb : a.label.localeCompare(b.label)
        })
        return list
    }

    Connections {
        target: core
        // The tree arrives later on the very first start
        onInterestsChanged: {
            if (page.tiles.length === 0) {
                page.info = core.interests()
                page.tiles = page.tilesOf(page.info)
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        contentHeight: column.height + Theme.paddingLarge
        clip: true

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Categories")
                description: qsTr("Active podcasts in your languages")
            }

            Grid {
                id: grid
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                columns: width > Theme.itemSizeHuge * 4 ? 4 : 2
                spacing: Theme.paddingSmall
                readonly property real tileWidth: (width - (columns - 1) * spacing) / columns

                Repeater {
                    model: page.tiles
                    BackgroundItem {
                        id: tile
                        width: grid.tileWidth
                        height: Theme.itemSizeMedium
                        onClicked: pageStack.push(Qt.resolvedUrl("CategoryPage.qml"),
                                                  { category: modelData.name, label: modelData.label })
                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.paddingSmall
                            color: Theme.rgba(Theme.highlightBackgroundColor,
                                              tile.highlighted ? 0.5 : 0.2 + 0.45 * Math.min(1, modelData.share))
                            opacity: modelData.level === 3 ? 0.4 : 1
                        }
                        Label {
                            x: Theme.paddingMedium
                            width: parent.width - 2 * Theme.paddingMedium
                            anchors.verticalCenter: parent.verticalCenter
                            text: modelData.label
                            wrapMode: Text.Wrap
                            maximumLineCount: 2
                            elide: Text.ElideRight
                            font.pixelSize: Theme.fontSizeSmall
                            color: tile.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                    }
                }
            }
        }

        ViewPlaceholder {
            enabled: page.tiles.length === 0
            text: qsTr("Categories are loading")
            hintText: qsTr("They come from fyyd - try again in a moment")
        }

        VerticalScrollDecorator {}
    }
}

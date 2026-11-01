// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// "Meine Interessen" (139 as a section of Discover, own page since 140):
// what "Für dich" suggests - the languages (► LanguagesPage), the top
// categories as small tiles coloured by their share (tap = next level:
// normal, mehr, weniger, nie; place and order stay while the page is open),
// "Alle Kategorien" with the sub-categories, the keywords "Mehr davon" /
// "Nie" and "Empfehlungen aus meinen Abos". Nothing is computed here:
// Discover computes "Für dich" once when it is shown again
// (core.interestsRevision).
Page {
    id: page

    property var info: core.interests()
    readonly property var levelNames: [qsTr("normal"), qsTr("more"), qsTr("less"), qsTr("never")]
    // The tiles, fixed when the page opens (tapping changes only the level)
    property var tiles: tilesOf(info)
    property var levels: levelsOf(info)

    function tilesOf(data) {
        var list = []
        var all = data.categories || []
        for (var i = 0; i < all.length; i++) {
            if (all[i].parent.length === 0) {
                list.push(all[i])
            }
        }
        // Most listened first; equal (nothing heard yet): the popular ones
        // first (146), then by name
        list.sort(function(a, b) {
            if (b.listened !== a.listened) {
                return b.listened - a.listened
            }
            var pa = a.popular >= 0 ? a.popular : 99
            var pb = b.popular >= 0 ? b.popular : 99
            return pa !== pb ? pa - pb : a.label.localeCompare(b.label)
        })
        return list
    }

    function levelsOf(data) {
        var map = {}
        var all = data.categories || []
        for (var i = 0; i < all.length; i++) {
            map[all[i].name] = all[i].level
        }
        return map
    }

    function nextLevel(entry) {
        var next = ((levels[entry.name] || 0) + 1) % 4
        var map = {}
        for (var key in levels) {
            map[key] = levels[key]
        }
        map[entry.name] = next
        levels = map
        core.setInterest(entry.name, next)
    }

    Connections {
        target: core
        // Languages, keywords, the switch - and the tree once it is loaded;
        // the tiles keep their order
        onInterestsChanged: {
            page.info = core.interests()
            if (page.tiles.length === 0) {
                page.tiles = page.tilesOf(page.info)
                page.levels = page.levelsOf(page.info)
            }
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("My interests")
                description: qsTr("Decides what \"For you\" suggests")
            }

            // Languages
            SectionHeader { text: qsTr("Languages") }
            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Repeater {
                    model: page.info.languages || []
                    Chip {
                        text: (page.info.languageNames || {})[modelData] || modelData
                        on: true
                        onClicked: pageStack.push(Qt.resolvedUrl("LanguagesPage.qml"))
                    }
                }
                Chip {
                    text: qsTr("Change") + " ›"
                    onClicked: pageStack.push(Qt.resolvedUrl("LanguagesPage.qml"))
                }
            }

            // Categories
            SectionHeader { text: qsTr("Categories") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: page.info.hasListening ? qsTr("Darker = heard more. Tap: more, less, never, normal.")
                                             : qsTr("Nothing heard yet - tap what interests you: more, less, never, normal.")
            }
            Item { width: 1; height: Theme.paddingSmall }
            Grid {
                id: grid
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                columns: width > Theme.itemSizeHuge * 4 ? 4 : 3
                spacing: Theme.paddingSmall
                readonly property real tileWidth: (width - (columns - 1) * spacing) / columns

                Repeater {
                    model: page.tiles
                    BackgroundItem {
                        id: tile
                        width: grid.tileWidth
                        height: Theme.itemSizeSmall
                        readonly property int level: page.levels[modelData.name] || 0
                        // The colour follows the listening share, "mehr" at least strong
                        readonly property real strength: level === 1 ? Math.max(modelData.share, 0.8)
                                                         : level === 2 ? modelData.share / 2 : modelData.share
                        onClicked: page.nextLevel(modelData)

                        Rectangle {
                            anchors.fill: parent
                            radius: Theme.paddingSmall
                            color: tile.level === 3 ? "transparent"
                                                    : Theme.rgba(Theme.highlightBackgroundColor, 0.18 + 0.55 * Math.min(1, tile.strength))
                            border.width: tile.level === 3 ? 1 : 0
                            border.color: Theme.rgba(Theme.primaryColor, 0.3)
                        }
                        Column {
                            x: Theme.paddingSmall
                            width: parent.width - 2 * Theme.paddingSmall
                            anchors.verticalCenter: parent.verticalCenter
                            Label {
                                width: parent.width
                                text: modelData.label
                                truncationMode: TruncationMode.Fade
                                font.pixelSize: Theme.fontSizeExtraSmall
                                font.strikeout: tile.level === 3
                                color: tile.level === 3 ? Theme.secondaryColor : Theme.primaryColor
                            }
                            Label {
                                visible: tile.level !== 0
                                width: parent.width
                                font.pixelSize: Theme.fontSizeTiny
                                color: Theme.highlightColor
                                text: page.levelNames[tile.level]
                            }
                        }
                    }
                }
            }
            BackgroundItem {
                width: parent.width
                onClicked: pageStack.push(Qt.resolvedUrl("InterestsCategoriesPage.qml"))
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    horizontalAlignment: Text.AlignRight
                    font.pixelSize: Theme.fontSizeSmall
                    text: qsTr("All categories and sub-categories") + " ›"
                    color: parent.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
            }

            // Keywords
            Repeater {
                model: [1, 3]
                Column {
                    id: keywordColumn
                    width: column.width
                    readonly property int kind: modelData
                    readonly property var words: (kind === 3 ? page.info.never : page.info.more) || []
                    SectionHeader { text: keywordColumn.kind === 3 ? qsTr("Never") : qsTr("More of this") }
                    Flow {
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        spacing: Theme.paddingSmall
                        Repeater {
                            model: keywordColumn.words
                            Chip {
                                text: modelData.forms.length > 0 ? modelData.word + " · " + modelData.forms.join(" · ") : modelData.word
                                on: true
                                negative: keywordColumn.kind === 3
                                onClicked: pageStack.push(Qt.resolvedUrl("KeywordDialog.qml"),
                                                          { kind: keywordColumn.kind, oldWord: modelData.word, oldForms: modelData.forms })
                            }
                        }
                        Chip {
                            text: "+ " + qsTr("Keyword")
                            onClicked: pageStack.push(Qt.resolvedUrl("KeywordDialog.qml"), { kind: keywordColumn.kind })
                        }
                    }
                }
            }

            Item { width: 1; height: Theme.paddingLarge }

            TextSwitch {
                text: qsTr("Recommendations from my subscriptions")
                description: qsTr("Sends the addresses of your 10 most listened podcasts to fyyd to find similar ones.")
                checked: page.info.fromSubscriptions === true
                automaticCheck: false
                onClicked: core.setRecommendFromSubscriptions(!checked)
            }
        }

        VerticalScrollDecorator {}
    }
}

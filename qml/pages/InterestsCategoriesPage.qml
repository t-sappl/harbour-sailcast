// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// "Alle Kategorien" of "Meine Interessen" (139): fyyd's whole category tree
// (German names), sub-categories indented under their top category, each
// with a bar for its share and its level; tap = next level (normal, mehr,
// weniger, nie). Discover computes "Für dich" again when it is shown
// (140); a sub-category of a category set to "nie" is greyed out.
Page {
    id: page

    property var categories: core.interests().categories || []
    // Levels apart from the model: a new model would scroll to the top
    property var levels: levelsOf(categories)

    function levelsOf(list) {
        var map = {}
        for (var i = 0; i < list.length; i++) {
            map[list[i].name] = list[i].level
        }
        return map
    }
    readonly property var levelNames: [qsTr("normal"), qsTr("more"), qsTr("less"), qsTr("never")]

    Connections {
        target: core
        // Only while the tree is still loading (it arrives once)
        onInterestsChanged: {
            if (page.categories.length === 0) {
                page.categories = core.interests().categories || []
            }
        }
    }

    SilicaListView {
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        currentIndex: -1
        model: page.categories

        header: PageHeader {
            title: qsTr("All categories")
            description: qsTr("Tap: more, less, never")
        }

        delegate: BackgroundItem {
            id: row
            width: ListView.view.width
            height: Theme.itemSizeSmall
            readonly property bool sub: modelData.parent.length > 0
            readonly property int level: page.levels[modelData.name] || 0
            // Its top category is "nie": it does not count (140: "Musik: nie"
            // beside "Musik-Interviews: mehr" was confusing)
            readonly property bool excluded: sub && (page.levels[modelData.parent] || 0) === 3
            opacity: excluded ? 0.4 : 1
            onClicked: {
                var next = (row.level + 1) % 4
                var map = {}
                for (var key in page.levels) {
                    map[key] = page.levels[key]
                }
                map[modelData.name] = next
                page.levels = map
                core.setInterest(modelData.name, next)
            }

            // Share of the profile (192: a thin line below the name - as a
            // block behind it, small shares showed as a stub behind the first
            // letters or a bare stroke, which looked broken); hidden below 1 %
            Rectangle {
                x: (row.sub ? 2 * Theme.horizontalPageMargin : Theme.horizontalPageMargin) + Theme.paddingSmall
                anchors.bottom: parent.bottom
                anchors.bottomMargin: Theme.paddingSmall
                height: Math.max(2, Math.round(Theme.paddingSmall / 2))
                radius: height / 2
                width: Math.max(Theme.paddingLarge, (parent.width - x - Theme.horizontalPageMargin) * Math.min(1, modelData.share))
                color: Theme.rgba(Theme.highlightColor, 0.6)
                visible: row.level !== 3 && modelData.share >= 0.01
            }

            Label {
                x: (row.sub ? 2 * Theme.horizontalPageMargin : Theme.horizontalPageMargin) + Theme.paddingSmall
                width: levelLabel.x - x - Theme.paddingMedium
                anchors.verticalCenter: parent.verticalCenter
                truncationMode: TruncationMode.Fade
                text: modelData.label
                font.pixelSize: row.sub ? Theme.fontSizeSmall : Theme.fontSizeMedium
                font.strikeout: row.level === 3
                color: row.highlighted ? Theme.highlightColor
                                       : (row.level === 3 ? Theme.secondaryColor : Theme.primaryColor)
            }

            Label {
                id: levelLabel
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                text: row.level !== 0 ? page.levelNames[row.level] : ""
            }
        }

        ViewPlaceholder {
            enabled: page.categories.length === 0
            text: qsTr("Categories are loading")
            hintText: qsTr("They come from fyyd - try again in a moment")
        }

        VerticalScrollDecorator {}
    }
}

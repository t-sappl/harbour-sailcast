// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (195) Languages of a Discover list ("Im Trend", "Neu"): all of them side
// by side, the chosen one highlighted and underlined like the tabs, a tap
// on a name switches; "ändern ›" opens "Meine Interessen", where the
// languages come from. More than fit in a row wrap to a second one.
// (Before: "Sprache: Deutsch (tippen zum Wechseln)" - it did not show
// between which languages, nor where they came from.)
Item {
    id: line

    property var languages: []      // codes, e.g. ["de", "pl"]
    property var names: ({})        // code -> name
    property string current: ""

    signal chosen(string language)

    width: parent ? parent.width : 0
    height: flow.height + Theme.paddingSmall

    Flow {
        id: flow
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        spacing: Theme.paddingLarge

        Label {
            height: Theme.itemSizeExtraSmall
            verticalAlignment: Text.AlignVCenter
            text: qsTr("Language:")
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
        }

        Repeater {
            model: line.languages
            BackgroundItem {
                id: languageItem
                readonly property bool isCurrent: modelData === line.current
                width: languageLabel.implicitWidth + Theme.paddingMedium
                height: Theme.itemSizeExtraSmall
                onClicked: {
                    if (!languageItem.isCurrent) {
                        line.chosen(modelData)
                    }
                }
                Label {
                    id: languageLabel
                    anchors.centerIn: parent
                    text: line.names[modelData] || modelData
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: languageItem.isCurrent || languageItem.highlighted ? Theme.highlightColor
                                                                              : Theme.secondaryHighlightColor
                }
                Rectangle {
                    visible: languageItem.isCurrent
                    anchors.horizontalCenter: languageLabel.horizontalCenter
                    anchors.top: languageLabel.bottom
                    anchors.topMargin: Theme.paddingSmall / 2
                    width: languageLabel.width
                    height: Math.max(1, Theme.paddingSmall / 4)
                    color: Theme.highlightColor
                }
            }
        }

        BackgroundItem {
            id: changeItem
            width: changeLabel.implicitWidth + Theme.paddingMedium
            height: Theme.itemSizeExtraSmall
            onClicked: pageStack.push(Qt.resolvedUrl("pages/InterestsPage.qml"))
            Label {
                id: changeLabel
                anchors.centerIn: parent
                text: qsTr("change") + " ›"
                font.pixelSize: Theme.fontSizeExtraSmall
                color: changeItem.highlighted ? Theme.highlightColor : Theme.secondaryColor
            }
        }
    }
}

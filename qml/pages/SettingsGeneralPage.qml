// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Settings → General (88): what happens at app start (43).
Page {
    id: page
    objectName: "settingsGeneralPage"

    readonly property var startPages: ["home", "podcasts", "playlist"]

    function indexOf(list, value, fallback) {
        var i = list.indexOf(value)
        return i >= 0 ? i : fallback
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("General")
            }

            TextSwitch {
                text: qsTr("Synchronize and update on start")
                description: qsTr("Otherwise only by hand: pull down → Update.")
                checked: core.setting("start.update", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("start.update", checked ? "1" : "0")
                }
            }

            TextSwitch {
                text: qsTr("Discover as a ring page")
                description: qsTr("Swipe from New episodes to Discover and on to Podcasts. Off: Discover is in the Podcasts pull-down menu instead.")
                // (229) On by default
                checked: core.setting("ui.discoverRing", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("ui.discoverRing", checked ? "1" : "0")
                }
            }

            // (164) "Neu" exists only in German (Hörsuppe's list)
            TextSwitch {
                text: qsTr("Tab \"New\" in Discover")
                description: qsTr("New podcasts: German from fyyd (first episodes, chosen by Hörsuppe), English from Podnews (trailers). Shown when German or English is one of your languages.")
                checked: core.setting("ui.discoverNewTab", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("ui.discoverNewTab", checked ? "1" : "0")
                }
            }

            ComboBox {
                label: qsTr("Start page")
                description: qsTr("Applies from the next start.")
                currentIndex: page.indexOf(page.startPages, core.setting("ui.startPage", "home"), 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("New episodes")
                        onClicked: core.setSetting("ui.startPage", "home")
                    }
                    MenuItem {
                        text: qsTr("Podcasts")
                        onClicked: core.setSetting("ui.startPage", "podcasts")
                    }
                    MenuItem {
                        text: qsTr("Playlist")
                        onClicked: core.setSetting("ui.startPage", "playlist")
                    }
                }
            }

            // (246) Rarely needed, so here and not in a pulley (Thomas);
            // (248) a row as the other entries instead of a lone button
            SectionHeader {
                visible: core.podcastCount > 0
                text: qsTr("Episodes")
            }
            SettingsEntry {
                visible: core.podcastCount > 0
                title: qsTr("Mark podcasts as played …")
                summary: qsTr("Of all or chosen podcasts – all episodes, or all but the newest of each")
                target: Qt.resolvedUrl("MarkAllPlayedDialog.qml")
            }
        }

        VerticalScrollDecorator {}
    }
}

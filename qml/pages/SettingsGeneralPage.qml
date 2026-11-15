// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Settings → General (88): what happens at app start (43).
// (326, settings restructured) "Pages and navigation": only the settings of
// the pages are left here - "Synchronize and update on start" went to
// Synchronization, "Mark podcasts as played …" to Import and export.
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
                title: qsTr("Pages and navigation")
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

            TextSwitch {
                text: qsTr("New episodes: group by podcast")
                description: qsTr("A header per podcast. Also a switch on the page.")
                checked: core.setting("home.grouped", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("home.grouped", checked ? "1" : "0")
                }
            }

            // (329) The line on the Podcasts page; off: the statistics are in
            // its pull-down menu
            TextSwitch {
                text: qsTr("Podcasts: listening summary")
                description: qsTr("A line under the header, tap for the listening statistics. Off: they are in the pull-down menu.")
                checked: core.setting("ui.statsLine", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("ui.statsLine", checked ? "1" : "0")
                }
            }

            TextSwitch {
                text: qsTr("Discover as a ring page")
                description: qsTr("Swipe to Discover. Off: it is in the Podcasts pull-down menu.")
                // (229) On by default
                checked: core.setting("ui.discoverRing", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("ui.discoverRing", checked ? "1" : "0")
                }
            }

            TextSwitch {
                text: qsTr("Downloads as a ring page")
                description: qsTr("Swipe from New episodes to Downloads. Off: it is in the pull-down menus.")
                checked: core.setting("ui.downloadsRing", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("ui.downloadsRing", checked ? "1" : "0")
                }
            }

            // (164) "Neu" exists only in German (Hörsuppe's list)
            TextSwitch {
                text: qsTr("Tab \"New\" in Discover")
                description: qsTr("New podcasts in German (fyyd, Hörsuppe) and English (Podnews), if one of them is your language.")
                checked: core.setting("ui.discoverNewTab", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("ui.discoverNewTab", checked ? "1" : "0")
                }
            }

        }

        VerticalScrollDecorator {}
    }
}

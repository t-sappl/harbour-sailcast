// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

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
        }

        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Settings of one podcast (35): own speed, skip intro and outro. Stored as
// settings "podcast.<id>.rate/.skipIntro/.skipOutro" (0 = off / global),
// removed when unsubscribing. Changes apply at once (also to the episode
// playing now).
Page {
    id: page
    objectName: "podcastSettingsPage"

    property int podcastId: 0
    property string podcastTitle: ""
    // Archive state and functions (50, moved here from the podcast page's
    // pulley)
    property var podcast: core.podcast(podcastId)
    readonly property bool archiving: core.archivingPodcastId === podcastId

    Connections {
        target: core
        onDataChanged: page.podcast = core.podcast(page.podcastId)
    }

    readonly property var speeds: [0, 0.8, 0.9, 1.0, 1.1, 1.2, 1.3, 1.5, 1.75, 2.0]
    readonly property var skipTimes: [0, 5, 10, 15, 20, 30, 45, 60, 90, 120]

    function key(name) {
        return "podcast." + podcastId + "." + name
    }

    function number(name) {
        var value = parseFloat(core.setting(key(name), "0"))
        return isNaN(value) ? 0 : value
    }

    function indexOf(list, value) {
        for (var i = 0; i < list.length; i++) {
            if (Math.abs(list[i] - value) < 0.01) {
                return i
            }
        }
        return 0
    }

    function speedText(value) {
        var decimals = Math.round(value * 100) % 10 === 0 ? 1 : 2
        return Number(value).toLocaleString(Qt.locale(), "f", decimals) + "×"
    }

    function store(name, value) {
        core.setSetting(key(name), value > 0 ? "" + value : "0")
        if (appWindow.player.podcastId === podcastId) {
            appWindow.player.reloadPodcastSettings()
        }
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Podcast settings")
                description: page.podcastTitle
            }

            SectionHeader { text: qsTr("Folder") }

            FolderComboBox {
                folderId: core.podcast(page.podcastId).folderId || 0
                onFolderIdChanged: {
                    if ((core.podcast(page.podcastId).folderId || 0) !== folderId) {
                        core.setPodcastFolder(page.podcastId, folderId)
                    }
                }
            }

            ComboBox {
                label: qsTr("Order")
                description: qsTr("Oldest first for series heard from the beginning")
                currentIndex: core.setting("podcast." + page.podcastId + ".oldestFirst", "0") === "1" ? 1 : 0
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Newest first")
                        onClicked: core.setSetting("podcast." + page.podcastId + ".oldestFirst", "0")
                    }
                    MenuItem {
                        text: qsTr("Oldest first")
                        onClicked: core.setSetting("podcast." + page.podcastId + ".oldestFirst", "1")
                    }
                }
            }

            SectionHeader { text: qsTr("Update") }

            TextSwitch {
                text: qsTr("Include in \"Update\"")
                description: qsTr("Off: the podcast is only updated on its own page (pull down → Update). Subscription and listening state are still synchronized.")
                checked: !core.podcast(page.podcastId).manualUpdate
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setManualUpdate(page.podcastId, !checked)
                }
            }

            SectionHeader { text: qsTr("Playback") }

            ComboBox {
                label: qsTr("Speed")
                currentIndex: page.indexOf(page.speeds, page.number("rate"))
                menu: ContextMenu {
                    Repeater {
                        model: page.speeds
                        MenuItem {
                            text: modelData > 0
                                  ? page.speedText(modelData)
                                  : qsTr("As set globally (%1)").arg(page.speedText(appWindow.player.globalRate))
                            onClicked: page.store("rate", modelData)
                        }
                    }
                }
            }

            ComboBox {
                label: qsTr("Skip intro")
                currentIndex: page.indexOf(page.skipTimes, page.number("skipIntro"))
                menu: ContextMenu {
                    Repeater {
                        model: page.skipTimes
                        MenuItem {
                            text: modelData > 0 ? qsTr("%1 s").arg(modelData) : qsTr("Off")
                            onClicked: page.store("skipIntro", modelData)
                        }
                    }
                }
            }

            ComboBox {
                label: qsTr("Skip outro")
                currentIndex: page.indexOf(page.skipTimes, page.number("skipOutro"))
                menu: ContextMenu {
                    Repeater {
                        model: page.skipTimes
                        MenuItem {
                            text: modelData > 0 ? qsTr("%1 s").arg(modelData) : qsTr("Off")
                            onClicked: page.store("skipOutro", modelData)
                        }
                    }
                }
            }

            SectionHeader { text: qsTr("Archive") }

            TextSwitch {
                text: qsTr("Stop updates (archived)")
                description: qsTr("For a podcast that has ended: no more updates and no error messages. Episodes, downloads and the listening state are kept.")
                checked: page.podcast.archived === true
                automaticCheck: false
                onClicked: core.setArchived(page.podcastId, !checked)
            }

            Button {
                visible: page.archiving || core.archivingPodcastId === 0
                anchors.horizontalCenter: parent.horizontalCenter
                text: page.archiving ? qsTr("Cancel archiving") : qsTr("Archive podcast")
                onClicked: {
                    if (page.archiving) {
                        core.cancelArchive()
                        return
                    }
                    var dialog = pageStack.push(Qt.resolvedUrl("ArchiveDialog.qml"), { podcast: page.podcast })
                    dialog.accepted.connect(function() { core.archivePodcast(page.podcastId) })
                }
            }

            Label {
                visible: (page.podcast.archiveFolder || "").length > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAnywhere
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Archive folder: %1").arg(page.podcast.archiveFolder || "")
            }

            Item { width: 1; height: Theme.paddingLarge }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("An episode that starts from the beginning starts after the intro. The outro ends the episode early: it counts as played and the next one follows. With a speed of its own, the speed on the episode page changes this podcast's speed.")
            }
        }
    }
}

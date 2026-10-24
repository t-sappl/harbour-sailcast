// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Settings → Playback (88): after an episode, smart resume, skip times;
// lock screen and cover buttons (83, 87).
Page {
    id: page
    objectName: "settingsPlaybackPage"

    readonly property var skipOptions: [10, 15, 20, 30, 45, 60, 90]
    // After an episode that was not started from the playlist (24)
    readonly property var afterSingleOptions: ["load", "play", "stop"]
    readonly property var smartResumeOptions: [0, 5, 10, 20, 30]
    // The two lock screen / headset buttons (99)
    readonly property var remotePrevOptions: ["chapter", "back"]
    readonly property var remoteNextOptions: ["chapters", "episodes", "forward"]
    // Second button of the app cover (87)
    readonly property var coverButtonOptions: ["next", "back", "forward", "sleep", "bookmark"]

    function setCoverButton(value) {
        appWindow.player.coverButton = value
        core.setSetting("player.coverButton", value)
    }

    function setRemotePrev(value) {
        appWindow.player.remotePrev = value
        core.setSetting("player.remotePrev", value)
    }

    function setRemoteNext(value) {
        appWindow.player.remoteNextAction = value
        core.setSetting("player.remoteNext", value)
    }

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
                title: qsTr("Playback")
            }

            ComboBox {
                label: qsTr("After an episode outside the playlist")
                currentIndex: page.indexOf(page.afterSingleOptions, core.setting("player.afterSingle", "load"), 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Load the next playlist episode")
                        onClicked: core.setSetting("player.afterSingle", "load")
                    }
                    MenuItem {
                        text: qsTr("Play the next playlist episode")
                        onClicked: core.setSetting("player.afterSingle", "play")
                    }
                    MenuItem {
                        text: qsTr("Stop")
                        onClicked: core.setSetting("player.afterSingle", "stop")
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Episodes started on the playlist page are always followed by the next playlist episode. \"Load\" puts it into the player bar, paused - one tap continues.")
            }

            // Smart resume (53)
            ComboBox {
                label: qsTr("Rewind after a long pause")
                description: qsTr("After more than 5 minutes of pause (or a restart) playback continues a little earlier")
                currentIndex: page.indexOf(page.smartResumeOptions, parseInt(core.setting("player.smartResume", "10")), 2)
                menu: ContextMenu {
                    Repeater {
                        model: page.smartResumeOptions
                        MenuItem {
                            text: modelData > 0 ? qsTr("%n s", "", modelData) : qsTr("Off")
                            onClicked: core.setSetting("player.smartResume", "" + modelData)
                        }
                    }
                }
            }

            // Skip times side by side (44)
            Row {
                width: parent.width

                ComboBox {
                    width: parent.width / 2
                    label: qsTr("Skip back")
                    currentIndex: page.indexOf(page.skipOptions, appWindow.player.skipBackSeconds, 3)
                    menu: ContextMenu {
                        Repeater {
                            model: page.skipOptions
                            MenuItem {
                                text: qsTr("%n s", "", modelData)
                                onClicked: {
                                    appWindow.player.skipBackSeconds = modelData
                                    core.setSetting("player.skipBack", String(modelData))
                                }
                            }
                        }
                    }
                }

                ComboBox {
                    width: parent.width / 2
                    label: qsTr("Skip forward")
                    currentIndex: page.indexOf(page.skipOptions, appWindow.player.skipForwardSeconds, 3)
                    menu: ContextMenu {
                        Repeater {
                            model: page.skipOptions
                            MenuItem {
                                text: qsTr("%n s", "", modelData)
                                onClicked: {
                                    appWindow.player.skipForwardSeconds = modelData
                                    core.setSetting("player.skipForward", String(modelData))
                                }
                            }
                        }
                    }
                }
            }

            // ---------------------------------------------------------
            // Lock screen and cover (83, own cover setting since 87)
            SectionHeader { text: qsTr("Lock screen and cover") }

            // Each of the two buttons on its own (99); only actions that
            // fit the arrows the system draws (101)
            ComboBox {
                label: qsTr("Lock screen: left button")
                description: qsTr("Also applies to the headset buttons")
                currentIndex: page.indexOf(page.remotePrevOptions, appWindow.player.remotePrev, 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Previous chapter")
                        onClicked: page.setRemotePrev("chapter")
                    }
                    MenuItem {
                        text: qsTr("Skip back")
                        onClicked: page.setRemotePrev("back")
                    }
                }
            }

            ComboBox {
                label: qsTr("Lock screen: right button")
                currentIndex: page.indexOf(page.remoteNextOptions, appWindow.player.remoteNextAction, 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Chapter, then episode")
                        onClicked: page.setRemoteNext("chapters")
                    }
                    MenuItem {
                        text: qsTr("Next episode")
                        onClicked: page.setRemoteNext("episodes")
                    }
                    MenuItem {
                        text: qsTr("Skip forward")
                        onClicked: page.setRemoteNext("forward")
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("\"Previous chapter\": the start of the chapter, a second press goes to the one before; without chapters it skips back.")
            }

            ComboBox {
                label: qsTr("Second button on the cover")
                description: qsTr("The first one is always play/pause")
                currentIndex: page.indexOf(page.coverButtonOptions, appWindow.player.coverButton, 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Next: chapter, then episode")
                        onClicked: page.setCoverButton("next")
                    }
                    MenuItem {
                        text: qsTr("Skip back")
                        onClicked: page.setCoverButton("back")
                    }
                    MenuItem {
                        text: qsTr("Skip forward")
                        onClicked: page.setCoverButton("forward")
                    }
                    MenuItem {
                        text: qsTr("Sleep timer (15 min)")
                        onClicked: page.setCoverButton("sleep")
                    }
                    MenuItem {
                        text: qsTr("Set a bookmark")
                        onClicked: page.setCoverButton("bookmark")
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }
}

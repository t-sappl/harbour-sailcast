// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Settings → Downloads and archive (88): Wi-Fi only, automatic downloads,
// clean-up; archive folder and cloud archive address.
Page {
    id: page
    objectName: "settingsDownloadsPage"

    // -1: never, 0: right after listening, N: days after listening
    readonly property var cleanupOptions: [-1, 0, 1, 3, 7, 14, 30]

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
                title: qsTr("Downloads and archive")
            }

            SectionHeader { text: qsTr("Downloads") }

            TextSwitch {
                text: qsTr("Only on Wi-Fi")
                description: qsTr("Over the mobile connection, a download started by hand asks first; automatic downloads wait.")
                checked: core.setting("downloads.wifiOnly", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.wifiOnly", checked ? "1" : "0")
                }
            }

            TextSwitch {
                text: qsTr("Download new episodes")
                description: qsTr("After every feed update, up to 3 new episodes per podcast are downloaded automatically.")
                checked: core.setting("downloads.auto", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.auto", checked ? "1" : "0")
                }
            }

            ComboBox {
                id: cleanupBox
                label: qsTr("Delete played downloads")
                currentIndex: page.indexOf(page.cleanupOptions, parseInt(core.setting("cleanup.days", "7")), 4)
                menu: ContextMenu {
                    Repeater {
                        model: page.cleanupOptions
                        MenuItem {
                            text: modelData < 0 ? qsTr("Never")
                                                : (modelData === 0 ? qsTr("Right after listening")
                                                                   : qsTr("After %n days", "", modelData))
                            onClicked: core.setSetting("cleanup.days", String(modelData))
                        }
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("\"Right after listening\": as soon as an episode is played; otherwise checked at every start. Never in archived podcasts or the archive folder.")
            }

            Item { width: 1; height: Theme.paddingMedium }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                visible: page.cleanupOptions[cleanupBox.currentIndex] >= 0
                text: qsTr("Clean up now")
                onClicked: core.cleanupDownloads(-1)
            }

            // ---------------------------------------------------------
            SectionHeader { text: qsTr("Archive") }

            TextField {
                id: archiveField
                width: parent.width
                text: core.archiveDirectory()
                label: qsTr("Archive folder")
                placeholderText: core.defaultArchiveDirectory()
                inputMethodHints: Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus && text !== core.archiveDirectory()) {
                        core.setArchiveDirectory(text)
                        text = core.archiveDirectory()
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("One folder per podcast with all episodes, the feed, the cover and episodes.json. The app may only write inside Music (Sailjail). Empty: %1").arg(core.defaultArchiveDirectory())
            }

            TextField {
                id: archiveUrlField
                width: parent.width
                text: core.setting("archive.url", "")
                label: qsTr("Cloud archive address")
                placeholderText: "http://127.0.0.1:8080/"
                inputMethodHints: Qt.ImhUrlCharactersOnly | Qt.ImhNoAutoUppercase | Qt.ImhNoPredictiveText
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        core.setSetting("archive.url", text.trim())
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Played when an episode is neither on the phone nor available at its original address, e.g. \"rclone serve http\" of the archive copy in the cloud.")
            }
        }

        VerticalScrollDecorator {}
    }
}

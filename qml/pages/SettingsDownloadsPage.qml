// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// Settings → Downloads and archive (88): Wi-Fi only, automatic downloads,
// clean-up; archive folder and cloud archive address.
Page {
    id: page
    objectName: "settingsDownloadsPage"

    // -1: never, 0: right after listening, N: days after listening
    readonly property var cleanupOptions: [-1, 0, 1, 3, 7, 14, 30]

    // (212) Counts of the per-podcast choices, refreshed on changes
    property int revision: 0
    Connections {
        target: core
        onSettingsChanged: page.revision++
        onDataChanged: page.revision++
    }
    readonly property bool globalInMusic: revision >= 0 && core.setting("downloads.inMusic", "0") === "1"
    readonly property int globalCleanup: revision >= 0 ? parseInt(core.setting("cleanup.days", "7")) : 7
    readonly property var musicChoice: revision >= 0 ? core.podcastChoice("inMusic", "1", globalInMusic) : ({})
    readonly property var keepChoice: revision >= 0 ? core.podcastChoice("cleanupDays", "-1", globalCleanup < 0) : ({})

    // (217) Downloads left in the old place after the location changed
    readonly property var misplaced: revision >= 0 ? core.misplacedDownloads(0) : []
    readonly property var misplacedTotal: {
        var count = 0
        var bytes = 0
        for (var i = 0; i < misplaced.length; i++) {
            count += misplaced[i].count
            bytes += misplaced[i].bytes
        }
        return { count: count, bytes: bytes }
    }
    // Set when the list of 212 was saved: offered once back on this page
    property bool pendingMove: false

    function offerMove() {
        if (core.misplacedDownloads(0).length > 0) {
            pageStack.push(Qt.resolvedUrl("MoveDownloadsDialog.qml"))
        }
    }

    onStatusChanged: {
        if (status === PageStatus.Active && pendingMove) {
            pendingMove = false
            offerMove()
        }
    }

    function choiceText(choice) {
        return qsTr("%1 of %2").arg(choice.on || 0).arg(choice.total || 0)
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
                title: qsTr("Downloads and archive")
            }

            // (253, usability audit #10) The downloaded episodes themselves -
            // whoever looks for "Downloads" in the settings finds them here
            SettingsEntry {
                title: qsTr("Show downloaded episodes")
                summary: core.downloadedCount() > 0 ? qsTr("%n episodes", "", core.downloadedCount()) : ""
                target: Qt.resolvedUrl("DownloadsPage.qml")
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

            // (245) Video streams are large: own rule for the mobile connection
            ComboBox {
                label: qsTr("Stream videos without Wi-Fi")
                description: qsTr("Video downloads always ask without Wi-Fi. \"Never\": with an audio version in the feed that one plays.")
                currentIndex: core.setting("video.mobile", "ask") === "always" ? 1
                            : (core.setting("video.mobile", "ask") === "never" ? 2 : 0)
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Ask")
                        onClicked: core.setSetting("video.mobile", "ask")
                    }
                    MenuItem {
                        text: qsTr("Always")
                        onClicked: core.setSetting("video.mobile", "always")
                    }
                    MenuItem {
                        text: qsTr("Never")
                        onClicked: core.setSetting("video.mobile", "never")
                    }
                }
            }

            TextSwitch {
                text: qsTr("Download new episodes")
                description: qsTr("After every feed update, up to 3 new episodes per podcast are downloaded automatically. Can be changed for each podcast in its settings.")
                checked: core.setting("downloads.auto", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.auto", checked ? "1" : "0")
                }
            }

            // (209) All new downloads in Music (else per episode / podcast:
            // "Save in Music"); cleaned up as in the app's folder
            TextSwitch {
                text: qsTr("Downloads in the Music folder")
                description: qsTr("New downloads go to Music/Podcasts/<podcast> (video episodes to Videos/Podcasts/<podcast>) - visible to other apps, also the music player. They are cleaned up as set below; \"Save in Music\" keeps an episode for good. Can be changed for each podcast in its settings.")
                checked: core.setting("downloads.inMusic", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.inMusic", checked ? "1" : "0")
                    // (217) Offer to move the existing downloads
                    page.offerMove()
                }
            }

            // (212) Choose the podcasts at once instead of in each podcast's
            // settings
            ValueButton {
                label: qsTr("Podcasts in the Music folder")
                value: page.choiceText(page.musicChoice)
                onClicked: {
                    var dialog = pageStack.push(Qt.resolvedUrl("PodcastChoiceDialog.qml"), {
                        title: qsTr("Downloads in the Music folder"),
                        hint: qsTr("On: new downloads of the podcast go to Music/Podcasts/<podcast>. Existing downloads can be moved afterwards."),
                        settingName: "inMusic", onValue: "1", offValue: "0",
                        globalOn: page.globalInMusic
                    })
                    dialog.accepted.connect(function() { page.pendingMove = true })
                }
                PageArrow {}
            }

            // (217) Downloads still in the old place (also after a change in
            // a podcast's settings, or when the move was declined)
            ValueButton {
                visible: page.misplaced.length > 0
                label: qsTr("Move downloads")
                value: qsTr("%n downloads", "", page.misplacedTotal.count) + " · "
                       + TimeFormat.size(page.misplacedTotal.bytes, qsTr("%1 MB"), qsTr("%1 GB"))
                onClicked: pageStack.push(Qt.resolvedUrl("MoveDownloadsDialog.qml"))
                PageArrow {}
            }

            ComboBox {
                id: cleanupBox
                label: qsTr("Delete played downloads")
                description: qsTr("Can be changed for each podcast in its settings.")
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

            // (212) Podcasts whose played downloads are never deleted -
            // with the global "Never" all are kept anyway
            ValueButton {
                visible: page.globalCleanup >= 0
                label: qsTr("Never delete for")
                value: page.choiceText(page.keepChoice)
                onClicked: pageStack.push(Qt.resolvedUrl("PodcastChoiceDialog.qml"), {
                    title: qsTr("Never delete downloads"),
                    hint: qsTr("On: played downloads of the podcast are kept – also with \"Delete played downloads\" on the downloads page. Off: as set above."),
                    settingName: "cleanupDays", onValue: "-1", offValue: "",
                    globalOn: false,
                    // (213) Sorted by download location: Music first
                    groupIds: core.podcastIdsWithChoice("inMusic", "1", page.globalInMusic),
                    firstTitle: qsTr("Downloads in the Music folder"),
                    restTitle: qsTr("Downloads on the phone")
                })
                PageArrow {}
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

            ActionPill {
                kind: "destructive"
                x: Theme.horizontalPageMargin
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

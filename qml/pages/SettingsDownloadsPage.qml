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

    // (327, Gemini: "79 of 79" under the switch that is on said nothing) The
    // podcasts that differ from the global switch
    function musicExceptionsText(choice) {
        var differ = page.globalInMusic ? (choice.total || 0) - (choice.on || 0) : (choice.on || 0)
        return differ > 0 ? qsTr("%n podcasts", "", differ) : qsTr("none")
    }

    function indexOf(list, value, fallback) {
        var i = list.indexOf(value)
        return i >= 0 ? i : fallback
    }

    // (287) Opened from the "Archived" section of the Downloads page:
    // scrolled to the section "Archive"
    property bool toArchive: false
    Timer {
        interval: 60
        running: page.toArchive
        onTriggered: settingsFlick.contentY = Math.max(0, Math.min(archiveHeader.y - Theme.paddingLarge,
                                                       settingsFlick.contentHeight - settingsFlick.height))
    }

    SilicaFlickable {
        id: settingsFlick
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Downloads and storage")
            }

            // (326, settings restructured) Sections by purpose: mobile data,
            // automatic downloads, clean up, storage, archive. "Show downloaded
            // episodes" and "Downloads as a ring page" (twice: also in Pages and
            // navigation) are gone from here.

            SectionHeader { text: qsTr("Mobile data") }

            TextSwitch {
                text: qsTr("Only on Wi-Fi")
                description: qsTr("On mobile data, a download by hand asks first and automatic downloads wait for Wi-Fi.")
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
                description: qsTr("\"Never\": with an audio version in the feed the sound plays instead.")
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

            SectionHeader { text: qsTr("Automatic downloads") }

            TextSwitch {
                text: qsTr("Download new episodes")
                description: qsTr("Up to 3 per podcast after a feed update. Also in each podcast's settings.")
                checked: core.setting("downloads.auto", "0") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("downloads.auto", checked ? "1" : "0")
                }
            }

            // (309) "+" loads as well (the quick button is always the playlist)
            TextSwitch {
                text: qsTr("Download playlist episodes")
                description: qsTr("What you add to the playlist is downloaded, so it plays without a connection. Off: it is streamed.")
                checked: core.setting("playlist.download", "1") === "1"
                automaticCheck: false
                onClicked: {
                    checked = !checked
                    core.setSetting("playlist.download", checked ? "1" : "0")
                }
            }

            SectionHeader { text: qsTr("Clean up") }

            ComboBox {
                id: cleanupBox
                label: qsTr("Delete played downloads")
                description: qsTr("Also in each podcast's settings.")
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
                    hint: qsTr("On: played downloads of the podcast are kept – also with \"Delete played\" on the downloads page. Off: as set above."),
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
                color: Theme.secondaryColor
                text: qsTr("Archived podcasts and the archive folder are never cleaned up. A deleted download stays in the playlist and streams.")
            }

            Item { width: 1; height: Theme.paddingMedium }

            ActionPill {
                kind: "destructive"
                x: Theme.horizontalPageMargin
                visible: page.cleanupOptions[cleanupBox.currentIndex] >= 0
                text: qsTr("Clean up now")
                onClicked: core.cleanupDownloads(-1)
            }


            SectionHeader { text: qsTr("Storage") }

            // (209) All new downloads in Music (else per episode / podcast:
            // "Save in Music"); cleaned up as in the app's folder
            TextSwitch {
                text: qsTr("Downloads in the Music folder")
                description: qsTr("New downloads go to Music/Podcasts/<podcast> (videos: Videos/Podcasts), visible to other apps. Also in each podcast's settings.")
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
                label: page.globalInMusic ? qsTr("Exceptions: not in Music") : qsTr("Exceptions: in Music")
                value: page.musicExceptionsText(page.musicChoice)
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
            // (327, Gemini) An action: a pill, not a row that looks like a value
            Item { visible: page.misplaced.length > 0; width: 1; height: Theme.paddingSmall }
            ActionPill {
                visible: page.misplaced.length > 0
                x: Theme.horizontalPageMargin
                text: qsTr("Move %n downloads (%1) …", "", page.misplacedTotal.count)
                      .arg(TimeFormat.size(page.misplacedTotal.bytes, qsTr("%1 MB"), qsTr("%1 GB")))
                onClicked: pageStack.push(Qt.resolvedUrl("MoveDownloadsDialog.qml"))
            }

            // ---------------------------------------------------------
            SectionHeader { id: archiveHeader; text: qsTr("Archive") }

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
                color: Theme.secondaryColor
                text: qsTr("Copies kept for good, one folder per podcast with feed and cover (podcast settings: \"Copy podcast to archive folder\"). Inside Music, may be the downloads folder above; never cleaned up. Empty: %1").arg(core.defaultArchiveDirectory())
            }

            // (328, Gemini) More room before the next field: the text above
            // ended right on top of it
            Item { width: 1; height: Theme.paddingLarge }

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
                color: Theme.secondaryColor
                text: qsTr("Played when an episode is gone from the phone and its original address, e.g. \"rclone serve http\".")
            }
        }

        VerticalScrollDecorator {}
    }
}

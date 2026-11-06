// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// Settings of one podcast (35): own speed, skip intro and outro. Stored as
// settings "podcast.<id>.rate/.skipIntro/.skipOutro" (0 = off / global),
// removed when unsubscribing. Changes apply at once (also to the episode
// playing now). Since 112 also an own name (column custom_title) and the
// automatic download ("podcast.<id>.autoDownload": "1", "0", empty = global),
// download location (211, "podcast.<id>.inMusic": "1", "0", empty = global)
// and since 117 the clean-up ("podcast.<id>.cleanupDays": -1, 0, N, empty = global).
// Since 143 also the actions moved from the podcast page's pulley:
// download all, save in Music, login, support, unsubscribe.
Page {
    id: page
    // (192) One width for all buttons (they were as wide as their text)
    readonly property real buttonWidth: Math.min(width - 2 * Theme.horizontalPageMargin, Theme.itemSizeHuge * 2.4)
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

    // Own name: stored when the field loses focus, on Enter and when the
    // page is left (the word being typed is committed first)
    function saveName() {
        Qt.inputMethod.commit()
        core.setPodcastTitle(podcastId, nameField.text)
    }

    onStatusChanged: {
        if (status === PageStatus.Deactivating) {
            saveName()
        }
    }

    readonly property string autoDownload: core.setting(key("autoDownload"), "")
    readonly property bool globalAutoDownload: core.setting("downloads.auto", "0") === "1"
    // (211) Download location ("podcast.<id>.inMusic": "1", "0", empty = global)
    readonly property string inMusic: core.setting(key("inMusic"), "")
    readonly property bool globalInMusic: core.setting("downloads.inMusic", "0") === "1"
    // (217) Re-evaluates the "move downloads" button
    property int locationRevision: 0
    Connections {
        target: core
        onSettingsChanged: page.locationRevision++
        onDataChanged: page.locationRevision++
    }

    // Delete played downloads (117): -2 = as set globally (stored as "")
    readonly property var cleanupOptions: [-2, -1, 0, 1, 3, 7, 14, 30]
    readonly property string cleanupDays: core.setting(key("cleanupDays"), "")
    readonly property int globalCleanupDays: parseInt(core.setting("cleanup.days", "7"))

    function cleanupText(days) {
        return days < 0 ? qsTr("Never")
                        : (days === 0 ? qsTr("Right after listening") : qsTr("After %n days", "", days))
    }

    function store(name, value) {
        core.setSetting(key(name), value > 0 ? "" + value : "0")
        if (appWindow.player.podcastId === podcastId) {
            appWindow.player.reloadPodcastSettings()
        }
    }

    // Unsubscribe (143, before in the podcast page's pulley): back to the
    // page below the podcast page first - neither page may be in the way
    // while the podcast disappears
    function unsubscribe() {
        var id = podcastId
        remorse.execute(qsTr("Unsubscribing"), function() {
            var target = pageStack.previousPage(page)
            if (target && target.objectName === "episodesPage") {
                target = pageStack.previousPage(target)
            }
            if (target) {
                pageStack.pop(target)
            } else {
                pageStack.pop()
            }
            appWindow.unsubscribe(id)
        })
    }

    RemorsePopup {
        id: remorse
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Podcast settings")
                description: page.podcast.title || page.podcastTitle
            }

            SectionHeader { text: qsTr("Name") }

            TextField {
                id: nameField
                width: parent.width
                label: qsTr("Name")
                placeholderText: page.podcast.feedTitle || page.podcastTitle
                // Set once, not bound: a list reload (dataChanged) while typing
                // would otherwise replace the typed text
                Component.onCompleted: text = page.podcast.customTitle || ""
                inputMethodHints: Qt.ImhNoAutoUppercase
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: {
                    page.saveName()
                    focus = false
                }
                onActiveFocusChanged: {
                    if (!activeFocus) {
                        page.saveName()
                    }
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: (page.podcast.customTitle || "").length > 0
                      ? qsTr("Name in the feed: %1").arg(page.podcast.feedTitle || "")
                      : qsTr("Empty: the name from the feed. Only shown on this phone – not synchronized.")
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

            // (244) The feed offers a video version (podcast:alternateEnclosure)
            ComboBox {
                visible: core.podcastHasVideoVersion(page.podcastId)
                label: qsTr("Version")
                description: qsTr("The feed offers the episodes also as video. Video is streamed; downloads stay audio.")
                currentIndex: core.setting("podcast." + page.podcastId + ".version", "") === "video" ? 1 : 0
                menu: ContextMenu {
                    MenuItem {
                        text: qsTr("Audio")
                        onClicked: {
                            core.setSetting("podcast." + page.podcastId + ".version", "")
                            // (245) The episode playing switches at once
                            appWindow.player.reloadVersion(page.podcastId)
                        }
                    }
                    MenuItem {
                        text: qsTr("Video")
                        onClicked: {
                            core.setSetting("podcast." + page.podcastId + ".version", "video")
                            appWindow.player.reloadVersion(page.podcastId)
                        }
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

            // (255) Was "Stop updates (archived)" in the Archive section:
            // "archived" now only means the folder with copies
            TextSwitch {
                text: qsTr("Podcast has ended")
                description: qsTr("For a podcast that has ended: no more updates and no error messages. Episodes, downloads and the listening state are kept.")
                checked: page.podcast.archived === true
                automaticCheck: false
                onClicked: core.setArchived(page.podcastId, !checked)
            }

            SectionHeader { text: qsTr("Downloads") }

            ComboBox {
                label: qsTr("Download new episodes")
                // (245) Video podcasts: the files are large
                description: core.podcast(page.podcastId).video === true
                             ? qsTr("After a feed update, up to 3 new episodes – this is a video podcast, its files are large")
                             : qsTr("After a feed update, up to 3 new episodes")
                currentIndex: page.autoDownload === "1" ? 1 : (page.autoDownload === "0" ? 2 : 0)
                menu: ContextMenu {
                    MenuItem {
                        text: page.globalAutoDownload ? qsTr("As set globally (on)") : qsTr("As set globally (off)")
                        onClicked: core.setSetting(page.key("autoDownload"), "")
                    }
                    MenuItem {
                        text: qsTr("Always")
                        onClicked: core.setSetting(page.key("autoDownload"), "1")
                    }
                    MenuItem {
                        text: qsTr("Never")
                        onClicked: core.setSetting(page.key("autoDownload"), "0")
                    }
                }
            }

            // (211) Download location per podcast, as the global switch
            ComboBox {
                label: qsTr("Download location")
                description: qsTr("For new downloads. Music/Podcasts/<podcast> (video episodes: Videos/Podcasts/<podcast>) is visible to other apps, also the music player. To keep the episodes there, set \"Delete played downloads\" below to \"Never\".")
                currentIndex: page.inMusic === "0" ? 1 : (page.inMusic === "1" ? 2 : 0)
                menu: ContextMenu {
                    MenuItem {
                        text: page.globalInMusic ? qsTr("As set globally (Music folder)") : qsTr("As set globally (phone)")
                        onClicked: core.setSetting(page.key("inMusic"), "")
                    }
                    MenuItem {
                        text: qsTr("Phone (hidden)")
                        onClicked: core.setSetting(page.key("inMusic"), "0")
                    }
                    MenuItem {
                        text: qsTr("Music folder")
                        onClicked: core.setSetting(page.key("inMusic"), "1")
                    }
                }
            }

            // (217) Existing downloads in the other place: move them
            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
                property var entry: {
                    page.locationRevision
                    var list = core.misplacedDownloads(page.podcastId)
                    return list.length > 0 ? list[0] : null
                }
                visible: entry !== null
                text: entry === null ? ""
                      : (entry.toMusic ? qsTr("Move %n downloads to Music", "", entry.count)
                                       : qsTr("Move %n downloads to the phone", "", entry.count))
                onClicked: {
                    core.moveDownloads([page.podcastId])
                    page.locationRevision++
                }
            }

            ComboBox {
                label: qsTr("Delete played downloads")
                description: qsTr("Never: downloads of this podcast are kept – also with \"Delete played downloads\" on the downloads page")
                currentIndex: {
                    var i = page.cleanupOptions.indexOf(page.cleanupDays === "" ? -2 : parseInt(page.cleanupDays))
                    return i >= 0 ? i : 0
                }
                menu: ContextMenu {
                    Repeater {
                        model: page.cleanupOptions
                        MenuItem {
                            text: modelData === -2 ? qsTr("As set globally (%1)").arg(page.cleanupText(page.globalCleanupDays))
                                                   : page.cleanupText(modelData)
                            onClicked: core.setSetting(page.key("cleanupDays"), modelData === -2 ? "" : String(modelData))
                        }
                    }
                }
            }

            // (143) All episodes at once - all or only the unplayed ones
            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
                // (159) with a choice of episodes
                text: qsTr("Download episodes …")
                onClicked: pageStack.push(Qt.resolvedUrl("DownloadAllDialog.qml"),
                                          { podcastId: page.podcastId,
                                            podcastTitle: page.podcast.title || page.podcastTitle })
            }

            Item { width: 1; height: Theme.paddingMedium }

            // (121, from the pulley since 143) Keep the downloads in Music
            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
                visible: (page.podcast.downloadedCount || 0) > 0
                text: qsTr("Save downloads in Music")
                onClicked: core.keepPodcastDownloads(page.podcastId)
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
                // (253, usability audit #15) "Skip outro" read as "skip over
                // the outro while it still plays" - it actually ends the
                // episode early, the hint text below says so
                label: qsTr("Skip the end (outro)")
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

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("An episode that starts from the beginning starts after the intro. The outro ends the episode early: it counts as played and the next one follows. With a speed of its own, the speed on the episode page changes this podcast's speed.")
            }

            SectionHeader { text: qsTr("Archive") }

            // (254, usability audit #4/jargon) "Archive podcast" is a
            // different thing from "archived" above (that one only stops
            // updates) - this one copies the episodes out to a folder, e.g.
            // for the rclone/cloud backup described in DESIGN.md.
            Label {
                visible: page.archiving || core.archivingPodcastId === 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Copies the episodes, feed and cover into a folder you can back up elsewhere (e.g. with rclone)")
            }

            ActionPill {
                kind: page.archiving ? "destructive" : "secondary"
                x: Theme.horizontalPageMargin
                visible: page.archiving || core.archivingPodcastId === 0
                text: page.archiving ? qsTr("Cancel copying") : qsTr("Copy podcast to archive folder")
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

            // Login (from the pulley since 143): only when the feed has a
            // login or asked for one
            SectionHeader {
                visible: loginButton.visible
                text: qsTr("Login")
            }

            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
                id: loginButton
                visible: (page.podcast.authUser || "").length > 0
                         || /HTTP 40[13]/.test(page.podcast.lastError || "")
                text: (page.podcast.authUser || "").length > 0 ? qsTr("Change login") : qsTr("Login")
                onClicked: {
                    var dialog = pageStack.push(Qt.resolvedUrl("FeedLoginDialog.qml"),
                                                { podcastId: page.podcastId, podcastTitle: page.podcast.title || "",
                                                  user: page.podcast.authUser || "" })
                    dialog.accepted.connect(function() {
                        appWindow.setFeedLogin(page.podcastId, dialog.user.trim(), dialog.password)
                        core.updatePodcast(page.podcastId)
                    })
                }
            }

            // podcast:funding of the feed (83; from the pulley since 143)
            SectionHeader {
                visible: supportButton.visible
                text: qsTr("Support")
            }

            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
                id: supportButton
                visible: (page.podcast.funding || []).length > 0
                text: qsTr("Support the podcast")
                onClicked: Qt.openUrlExternally(page.podcast.funding[0].url)
            }

            SectionHeader { text: qsTr("Subscription") }

            ActionPill {
                kind: "destructive"
                x: Theme.horizontalPageMargin
                text: qsTr("Unsubscribe")
                onClicked: page.unsubscribe()
            }

            Item { width: 1; height: Theme.paddingLarge }
        }
    }
}

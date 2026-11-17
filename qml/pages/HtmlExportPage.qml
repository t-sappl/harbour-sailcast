// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import Sailfish.Share 1.0
import "../"

// (294) The web page of the subscriptions: one HTML file (folders, covers,
// last episodes with chapters and bookmarks, links, OPML per folder and a
// selection to save as OPML). With chapters the missing ones are loaded
// first - the page says how many, shows the progress and can stop (the file
// is then written with what was loaded). The switches are remembered;
// bookmarks and listening time are off by default (private).
Page {
    // (338) No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "htmlExportPage"

    property var folders: []
    // Counted a moment after the page is shown (reads all episodes)
    property var preview: ({ "podcasts": 0, "skipped": 0, "chapterEpisodes": 0, "chapterEpisodesAll": 0, "episodesAll": 0 })
    property bool running: false
    property bool building: false
    property int buildDone: 0
    property int buildTotal: 0
    property bool share: false
    property int done: 0
    property int total: 0

    function options() {
        return {
            "descriptions": descriptionsSwitch.checked,
            "episodes": episodesBox.currentIndex,
            "episodeNotes": episodesBox.currentIndex > 0 && notesSwitch.checked,
            "chapters": episodesBox.currentIndex > 0 && chaptersSwitch.checked,
            "bookmarks": episodesBox.currentIndex > 0 && bookmarksSwitch.checked,
            "statistics": statisticsBox.currentIndex
        }
    }

    // (304, Thomas 13 Nov: "Share" after "Save" loaded all 11 000 episodes
    // again) The file made last on this page, with what it was made from:
    // unchanged choices use it again
    property string lastPath: ""
    property string lastKey: ""
    property string _pendingKey: ""
    function choiceKey() {
        return JSON.stringify(page.folders) + JSON.stringify(page.options())
    }

    function start(toShare) {
        if (page.lastPath.length > 0 && page.lastKey === page.choiceKey()) {
            if (toShare) {
                shareAction.resources = [page.lastPath]
                shareAction.trigger()
            } else {
                appWindow.showMessage(qsTr("Saved as %1").arg(page.lastPath.substring(page.lastPath.lastIndexOf("/") + 1)))
            }
            return
        }
        page.share = toShare
        page._pendingKey = page.choiceKey()
        var go = function() {
            page.running = true
            page.building = false
            page.done = 0
            page.total = 0
            core.startHtmlExport(page.folders, page.options())
        }
        var loads = episodesBox.currentIndex > 0 && chaptersSwitch.checked && page.chaptersToLoad > 0
        if (!loads || core.downloadAllowed()) {
            go()
            return
        }
        var dialog = pageStack.push(Qt.resolvedUrl("MobileDataDialog.qml"), {
            acceptLabel: qsTr("Load"),
            message: qsTr("Downloads are set to Wi-Fi only. Load the chapters of %n episodes over the mobile connection anyway?", "", page.chaptersToLoad)
        })
        dialog.accepted.connect(go)
    }

    // Chapters still to load for the chosen episodes
    readonly property int chaptersToLoad: episodesBox.currentIndex === 2 ? preview.chapterEpisodesAll : preview.chapterEpisodes

    Timer {
        id: previewTimer
        interval: 80
        running: true
        onTriggered: page.preview = core.htmlExportPreview(page.folders)
    }
    // (330) A new folder choice: count again
    onFoldersChanged: previewTimer.restart()

    // (330, Thomas 15 Nov: the choice was gone since 326) Folders of the web
    // page, chosen here (as for the OPML file, but its own choice);
    // [] = everything, [-1] = none (Save/Share then off: 0 podcasts)
    function foldersText() {
        if (page.folders.length === 0) {
            return qsTr("Everything")
        }
        if (page.folders.length === 1 && page.folders[0] === -1) {
            return qsTr("None")
        }
        var names = []
        var list = core.folders()
        for (var i = 0; i < list.length; i++) {
            if (page.folders.indexOf(list[i].folderId) >= 0) {
                names.push(list[i].name)
            }
        }
        if (page.folders.indexOf(0) >= 0) {
            names.push(qsTr("Other podcasts"))
        }
        return names.join(", ")
    }

    function remember(key, on) {
        core.setSetting("htmlExport." + key, on ? "1" : "0")
    }

    Connections {
        target: core
        onHtmlExportProgress: {
            page.done = done
            page.total = total
        }
        onHtmlExportBuilding: {
            page.building = true
            page.buildDone = done
            page.buildTotal = total
        }
        onHtmlExportFinished: {
            page.running = false
            page.building = false
            page.preview = core.htmlExportPreview(page.folders)
            if (path.length === 0) {
                return
            }
            var name = path.substring(path.lastIndexOf("/") + 1)
            // A cancelled export (chapters only partly loaded) is not kept
            page.lastPath = cancelled ? "" : path
            page.lastKey = cancelled ? "" : page._pendingKey
            if (page.share) {
                shareAction.resources = [path]
                shareAction.trigger()
            } else {
                appWindow.showMessage(cancelled
                                      ? qsTr("Saved as %1 (with the chapters loaded so far)").arg(name)
                                      : qsTr("Saved as %1").arg(name))
            }
        }
    }

    ShareAction {
        id: shareAction
        title: qsTr("Share my podcasts")
        mimeType: "text/html"
    }

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingSmall

            PageHeader { title: qsTr("Share my podcasts") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("One HTML file with your podcasts (%n podcasts) in Downloads: folders, covers, links, OPML per folder. Opens in any browser, also without internet.", "", page.preview.podcasts)
            }

            Label {
                visible: page.preview.skipped > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("%n private feeds (with a password or a personal link) are left out.", "", page.preview.skipped)
            }

            // (330) Which folders go into the page
            ValueButton {
                visible: core.folders().length > 0
                enabled: !page.running
                label: qsTr("Folders")
                value: page.foldersText()
                onClicked: {
                    var selectPage = pageStack.push(Qt.resolvedUrl("FolderSelectPage.qml"), { selected: page.folders })
                    selectPage.selectedChanged.connect(function() { page.folders = selectPage.selected })
                }
                PageArrow {}
            }

            SectionHeader { text: qsTr("Content") }

            TextSwitch {
                id: descriptionsSwitch
                enabled: !page.running
                text: qsTr("Descriptions")
                checked: core.setting("htmlExport.descriptions", "1") === "1"
                onCheckedChanged: page.remember("descriptions", checked)
            }

            ComboBox {
                id: episodesBox
                enabled: !page.running
                label: qsTr("Episodes")
                description: currentIndex === 2
                             ? qsTr("All %n episodes: 5 shown, the others folded under \"More episodes\". Makes the file bigger.", "", page.preview.episodesAll)
                             : qsTr("Title, date, length and whether you heard it")
                currentIndex: parseInt(core.setting("htmlExport.episodes", "1"))
                menu: ContextMenu {
                    MenuItem { text: qsTr("None") }
                    MenuItem { text: qsTr("Last 5") }
                    MenuItem { text: qsTr("All") }
                }
                onCurrentIndexChanged: core.setSetting("htmlExport.episodes", String(currentIndex))
            }

            TextSwitch {
                id: notesSwitch
                enabled: episodesBox.currentIndex > 0 && !page.running
                text: qsTr("Descriptions of these episodes")
                description: qsTr("Already on the phone with the episodes, nothing is loaded")
                checked: core.setting("htmlExport.episodeNotes", "1") === "1"
                onCheckedChanged: page.remember("episodeNotes", checked)
            }

            TextSwitch {
                id: chaptersSwitch
                enabled: episodesBox.currentIndex > 0 && !page.running
                text: qsTr("Chapters of these episodes")
                description: (page.chaptersToLoad > 0
                              ? qsTr("Loads the chapters of %n episodes from the internet first.", "", page.chaptersToLoad)
                              : qsTr("All known chapters are already loaded."))
                             + " " + qsTr("Chapters only inside the audio file are missing for episodes that are not downloaded.")
                checked: core.setting("htmlExport.chapters", "0") === "1"
                onCheckedChanged: page.remember("chapters", checked)
            }

            TextSwitch {
                id: bookmarksSwitch
                enabled: episodesBox.currentIndex > 0 && !page.running
                text: qsTr("Bookmarks of these episodes")
                description: qsTr("Your own bookmarks with their notes – private")
                checked: core.setting("htmlExport.bookmarks", "0") === "1"
                onCheckedChanged: page.remember("bookmarks", checked)
            }

            ComboBox {
                id: statisticsBox
                enabled: !page.running
                label: qsTr("Listening statistics")
                description: currentIndex === 2
                             ? qsTr("Contains detailed listening habits: top podcasts, weekdays, time per podcast – private")
                             : currentIndex === 1
                               ? qsTr("Total time, time saved by speed, episodes – folded at the top of the page")
                               : qsTr("Not in the page")
                currentIndex: parseInt(core.setting("htmlExport.statistics", "0"))
                menu: ContextMenu {
                    MenuItem { text: qsTr("None") }
                    MenuItem { text: qsTr("Totals only") }
                    MenuItem { text: qsTr("Everything") }
                }
                onCurrentIndexChanged: core.setSetting("htmlExport.statistics", String(currentIndex))
            }

            Item { width: 1; height: Theme.paddingLarge }

            // Progress while chapters are loaded, then while the page is put together
            ProgressBar {
                visible: page.running && (page.building ? page.buildTotal > 0 : page.total > 0)
                width: parent.width
                minimumValue: 0
                maximumValue: Math.max(1, page.building ? page.buildTotal : page.total)
                value: page.building ? page.buildDone : page.done
                label: page.building ? qsTr("Building the page %1 / %2").arg(page.buildDone).arg(page.buildTotal)
                                     : qsTr("Loading chapters %1 / %2").arg(page.done).arg(page.total)
            }

            ActionPill {
                visible: page.running && !page.building
                kind: "secondary"
                x: Theme.horizontalPageMargin
                text: qsTr("Stop and save what is loaded")
                onClicked: core.cancelHtmlExport()
            }

            ActionPill {
                visible: !page.running
                enabled: page.preview.podcasts > 0
                kind: "primary"
                x: Theme.horizontalPageMargin
                text: qsTr("Save to Downloads")
                onClicked: page.start(false)
            }

            Item { width: 1; height: Theme.paddingSmall; visible: !page.running }

            ActionPill {
                visible: !page.running
                enabled: page.preview.podcasts > 0
                kind: "secondary"
                x: Theme.horizontalPageMargin
                text: qsTr("Share")
                onClicked: page.start(true)
            }
        }
        VerticalScrollDecorator {}
    }
}

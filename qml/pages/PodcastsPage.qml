// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// All subscriptions as a cover grid (with the number of unplayed episodes
// as a badge) or as a list - switchable in the pull-down menu, the choice
// is stored. Part of the ring navigation.
//
// Folders (45, like the favourite groups in Sailwave): one collapsible
// section per folder with its header right-aligned between the rows, the
// podcasts without a folder last under "Other podcasts". Without any folder
// there is one section without a header (as before 45).
//
// One flickable column with a Repeater per folder instead of a GridView/
// ListView: Qt 5.6 grids have no sections, and the context menus of folder
// headers and podcast rows must not sit in a list header (they grew
// upwards, see DESIGN). With a few dozen podcasts all delegates at once
// are no problem (covers come from the image cache).
Page {
    // (246) "podcasts.sort": "" alphabetical, "newest", "added", "unplayed"
    property string sortOrder: core.setting("podcasts.sort", "")
    // (280) Two forms: whole in the menu, small after "Sorted:" on the line
    function sortName(order) {
        return order === "newest" ? qsTr("newest episode first")
             : order === "added" ? qsTr("last subscribed first")
             : order === "unplayed" ? qsTr("unplayed episodes")
             : qsTr("alphabetical")
    }
    // (314) Short names for the line (the menu keeps the long ones)
    function sortShortName(order) {
        return order === "newest" ? qsTr("Newest first")
             : order === "added" ? qsTr("Last subscribed")
             : order === "unplayed" ? qsTr("Most unplayed")
             : qsTr("Alphabetical")
    }
    function sortMenuName(order) {
        return order === "newest" ? qsTr("Newest episode first")
             : order === "added" ? qsTr("Last subscribed first")
             : order === "unplayed" ? qsTr("Unplayed episodes")
             : qsTr("Alphabetical")
    }
    id: page
    objectName: "podcastsPage"
    property bool ringMember: false
    property bool gridMode: core.setting("ui.podcastGrid", "1") === "1"

    // (251) Folders off: one list/grid of all podcasts in the chosen order
    // (the sort works inside the folders only - "what did I subscribe to
    // last" could not be seen); the folders themselves stay untouched
    property bool flat: core.setting("podcasts.flat", "0") === "1"
    function toggleFolders() {
        flat = !flat
        core.setSetting("podcasts.flat", flat ? "1" : "0")
    }

    function toggleView() {
        gridMode = !gridMode
        core.setSetting("ui.podcastGrid", gridMode ? "1" : "0")
    }

    FolderListModel {
        id: folders
    }

    // (256) The first open folder (with podcasts) carries the legend line;
    // _foldRevision is raised when a folder is folded or the list changes
    property int _foldRevision: 0
    readonly property int legendFolderId: {
        if (_foldRevision < 0 || folders.folderCount < 0) return -2
        for (var i = 0; i < sectionRepeater.count; i++) {
            var item = sectionRepeater.itemAt(i)
            if (item && !item.collapsed && item.podcastCount > 0) {
                return item.folderId
            }
        }
        return -2
    }

    // (220) After the first subscription: since 231 one hint for the
    // podcast's context menu and folders (a separate menu hint would have
    // been a third one here); the hint of 52 on the folder header follows
    // once there is a folder
    function folderHint() {
        if (core.podcastCount > 0) {
            appWindow.showHint("podcastMenu", qsTr("Press and hold a podcast:\nsettings · move to folder\nNew folder: pull down"), page)
        }
    }
    Component.onCompleted: folderHint()

    // (329, Thomas + Gemini) Listening summary under the ring neighbours:
    // read when the page becomes active (fresh after the player) and when
    // the setting changes
    property var overview: ({})
    function refreshOverview() {
        overview = appWindow.statsLineOn ? core.listeningOverview() : ({})
    }
    onStatusChanged: if (status === PageStatus.Active) refreshOverview()
    Connections {
        target: appWindow
        onStatsLineOnChanged: page.refreshOverview()
    }
    function overviewText() {
        if (overview.empty !== false) {
            return ""
        }
        if (overview.weekSeconds > 0) {
            var text = qsTr("7 days: %1 listened").arg(TimeFormat.length(overview.weekSeconds, qsTr("%1 h %2 min"), qsTr("%1 min")))
            // Not "0/1 day in a row"
            if (overview.streak >= 2) {
                text += " · " + qsTr("%n days in a row", "", overview.streak)
            }
            return text
        }
        // A week without listening: the total, so the way to the statistics stays
        return qsTr("Total: %1 listened").arg(TimeFormat.length(overview.totalSeconds, qsTr("%1 h %2 min"), qsTr("%1 min")))
    }
    Connections {
        target: core
        onDataChanged: page.folderHint()
    }

    SilicaFlickable {
        id: flickable
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        contentHeight: column.height + Theme.paddingLarge

        PodcastsMenu { showNewFolder: folders.folderCount === 0 || page.flat }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Podcasts")
                description: core.updating
                             ? qsTr("Updating %1 %").arg(Math.round(core.updateProgress * 100))
                             : (core.syncing ? qsTr("Synchronizing") : "")
            }
            // (255) Neighbours of the ring, below the header
            RingNeighbours { kind: "podcasts"; ringMember: page.ringMember }

            // (329) Tap: listening statistics
            BackgroundItem {
                id: overviewLine
                visible: appWindow.statsLineOn && overviewLabel.text.length > 0
                width: parent.width
                height: Theme.itemSizeExtraSmall * 0.75
                onClicked: pageStack.push(Qt.resolvedUrl("StatisticsPage.qml"))
                Label {
                    id: overviewLabel
                    anchors {
                        left: parent.left
                        leftMargin: Theme.horizontalPageMargin
                        right: overviewArrow.left
                        rightMargin: Theme.paddingMedium
                        verticalCenter: parent.verticalCenter
                    }
                    text: page.overviewText()
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: overviewLine.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                }
                PageArrow { id: overviewArrow }
            }

            // (246) Order of the podcasts in every folder, list and grid
            ListItem {
                id: sortLine
                visible: core.podcastCount > 1
                width: parent.width
                contentHeight: Theme.itemSizeExtraSmall
                readonly property string order: page.sortOrder
                onClicked: openMenu()

                // (253) A pill like the video switch of the episode page
                // (250: "worked perfectly") instead of plain text, so it
                // reads as a button and not as a status line (usability
                // audit #23); the label still names the action, not the
                // state ("Folders: on/off" could be read either way)
                BackgroundItem {
                    id: foldersSwitch
                    visible: folders.folderCount > 0
                    anchors {
                        left: parent.left
                        leftMargin: Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    width: foldersLabel.width + 2 * Theme.paddingLarge
                    height: Theme.itemSizeExtraSmall * 0.8
                    onClicked: page.toggleFolders()

                    Rectangle {
                        anchors.fill: parent
                        radius: height / 2
                        color: Theme.rgba(Theme.highlightBackgroundColor, 0.25)
                    }
                    Label {
                        id: foldersLabel
                        anchors.centerIn: parent
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: foldersSwitch.highlighted ? Theme.highlightColor : Theme.primaryColor
                        text: page.flat ? qsTr("Show folders") : qsTr("Hide folders")
                    }
                }

                // (314) Grid or list in one tap (Thomas + Gemini 14 Nov: under
                // ▲▼ nobody looks for it). Shows where the tap leads (list
                // lines in the grid, squares in the list) - as "Show as list"
                // said; left with the folders, which also choose what is shown
                MouseArea {
                    id: viewToggle
                    anchors {
                        left: foldersSwitch.visible ? foldersSwitch.right : parent.left
                        leftMargin: foldersSwitch.visible ? Theme.paddingMedium : Theme.horizontalPageMargin - Theme.paddingSmall
                        verticalCenter: parent.verticalCenter
                    }
                    width: Theme.itemSizeExtraSmall
                    height: Theme.itemSizeExtraSmall
                    onClicked: {
                        if (viewBubble.takeHeld()) return
                        page.toggleView()
                    }
                    HoldNameBubble { id: viewBubble; attached: true; name: page.gridMode ? qsTr("Show as list") : qsTr("Show as grid") }
                    readonly property color tint: pressed ? Theme.highlightColor : Theme.secondaryHighlightColor
                    // (316) Smaller than the bars of 315 (they looked heavy next to
                    // the small text), and no three equal bars: that is the
                    // "menu" symbol everywhere. A list: small square, gap, line.
                    readonly property real s: Math.round(Theme.iconSizeExtraSmall * 0.75)
                    readonly property real stroke: Math.max(2, Math.round(s / 10))
                    // List: three rows of ▪—
                    Column {
                        visible: page.gridMode
                        anchors.centerIn: parent
                        spacing: Math.round(viewToggle.s * 0.2)
                        Repeater {
                            model: 3
                            Row {
                                spacing: Math.max(3, Math.round(viewToggle.s * 0.18))
                                Rectangle {
                                    width: Math.max(3, Math.round(viewToggle.s * 0.22))
                                    height: width
                                    radius: 1
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: viewToggle.tint
                                }
                                Rectangle {
                                    width: Math.round(viewToggle.s * 0.6)
                                    height: viewToggle.stroke
                                    radius: height / 2
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: viewToggle.tint
                                }
                            }
                        }
                    }
                    // Grid: two by two squares
                    Grid {
                        visible: !page.gridMode
                        anchors.centerIn: parent
                        columns: 2
                        spacing: Math.max(2, Math.round(viewToggle.s / 7))
                        Repeater {
                            model: 4
                            Rectangle {
                                width: Math.round(viewToggle.s * 0.44)
                                height: width
                                radius: Math.max(1, width / 6)
                                color: viewToggle.tint
                            }
                        }
                    }
                }

                // (314) ▲▼ and the order, as on New episodes and Downloads
                // (was "Sorted: …"); a short name, faded when there is no room
                Row {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    spacing: Theme.paddingSmall / 2
                    SortArrows {
                        id: podcastArrows
                        anchors.verticalCenter: parent.verticalCenter
                        tint: sortLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        width: Math.min(implicitWidth, sortLine.width - Theme.horizontalPageMargin - viewToggle.x - viewToggle.width
                                        - Theme.paddingMedium - podcastArrows.width - parent.spacing)
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: sortLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                        text: page.sortShortName(sortLine.order) + " ›"
                    }
                }

                menu: ContextMenu {
                    Repeater {
                        model: ["", "newest", "added", "unplayed"]
                        MenuItem {
                            text: page.sortMenuName(modelData)
                            font.bold: modelData === sortLine.order
                            onClicked: {
                                core.setSetting("podcasts.sort", modelData)
                                page.sortOrder = modelData
                            }
                        }
                    }
                }
            }

            // (255, usability check 254: numbers and dots without labels,
            // found by all runs) The legend; (256) with the real symbols,
            // under the first open folder header (FolderSection) - here only
            // when there are no folder headers (folders hidden or none);
            // list view: only the dot (the rows say "unplayed" in words)
            FolderLegend {
                visible: core.podcastCount > 0 && (page.flat || folders.folderCount === 0)
                height: visible ? implicitHeight : 0
                gridStyle: page.gridMode
                showNumber: page.gridMode
            }

            // (251) Folders off: all podcasts as one section without a header.
            // (254) Always there, switched by visible/active - not a Loader:
            // its size stayed at the height of the list after it was
            // unloaded, so "Show folders" left a blank page (the sections
            // sat far below); 253's guess (Repeater model null) was not it
            FolderSection {
                visible: page.flat
                active: page.flat
                folderId: -1
                showHeader: false
                gridMode: page.gridMode
            }

            Repeater {
                id: sectionRepeater
                onItemAdded: page._foldRevision++
                onItemRemoved: page._foldRevision++
                // (253) The model stays; switching it to null and back
                // left the Repeater empty (Qt 5.6) - the sections are hidden
                model: folders
                FolderSection {
                    id: folderSection
                    visible: !page.flat
                    folderId: model.folderId
                    name: model.name
                    manualUpdate: model.manualUpdate
                    unplayed: model.unplayed
                    podcastCount: model.podcastCount
                    showHeader: folders.folderCount > 0
                    gridMode: page.gridMode
                    canMoveUp: model.folderId > 0 && index > 0
                    canMoveDown: model.folderId > 0 && index < folders.folderCount - 1
                    // (256) The legend under the first open folder only
                    showLegend: folderId === page.legendFolderId
                    onFolded: {
                        page._foldRevision++
                    }
                }
            }
        }

        VerticalScrollDecorator {}
    }

    // (219) First steps as buttons instead of a hint to the pulley
    GetStarted {
        visible: core.podcastCount === 0
        anchors.verticalCenter: parent.verticalCenter
    }
}

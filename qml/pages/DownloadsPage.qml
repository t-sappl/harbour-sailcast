// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// Downloads: running and waiting ones on top (progress, cancel), below all
// downloaded episodes (delete in the context menu). The
// header shows the number of files and the space they take; (274) the
// order is chosen on a line above the list (default: last downloaded) and a
// filter in the pulley shows a search field. Files in the
// archive folder are listed too but are only deleted with the archive.
Page {
    id: page
    objectName: "downloadsPage"

    // (301) True as the last page of the ring (setting "ui.downloadsRing"):
    // the page then exists once for the whole session
    property bool ringMember: false

    property var active: core.activeDownloads()
    // (324, code review) Calculated in refresh() only - and only while the
    // page is shown (in the ring it lives the whole session; every
    // dataChanged read all downloads with their file sizes)
    property var stats: ({})
    property bool _activatedOnce: false
    property bool _statsStale: false
    // (324, code review) The archived rows are built when the list comes near
    // them (or "Archived ›" is tapped), not all at once on opening
    property bool archivedRowsWanted: false
    function checkArchivedRows() {
        if (!archivedRowsWanted && list.footerItem
                && list.contentY + 2 * list.height >= list.footerItem.y) {
            archivedRowsWanted = true
        }
    }
    // (285) Neither "Downloaded" nor "Archived" folds (Thomas: no use, and
    // folding cost the footer its place); a line below the sorting jumps
    // to "Archived"
    // (282) No rows in the list (a ListView hides its footer then):
    // "Archived" moves up into the header
    readonly property bool archivedInHeader: downloaded.count === 0

    // (274) Order of the downloaded episodes ("downloads.sort": 0 last
    // downloaded - the default, 1 newest episode, 2 oldest episode, 3 by
    // podcast) and a filter (pulley "Filter" shows the search field)
    property int sortMode: parseInt(core.setting("downloads.sort", "0"))
    property bool searching: false
    property string searchText: ""
    // (280) Two forms: whole in the menu, small after "Sorted:" on the line
    function sortName(mode) {
        return mode === 1 ? qsTr("newest episode first")
             : mode === 2 ? qsTr("oldest episode first")
             : mode === 3 ? qsTr("by podcast")
             : mode === 5 ? qsTr("largest first")
             : qsTr("last downloaded first")
    }

    // (313) Selection mode: marked episode ids (object id -> true, replaced
    // as a whole so the rows see the change), the anchor of the range long
    // press is an id too and is dropped when the order changes (Gemini)
    property bool selectMode: false
    property var selected: ({})
    property int selectedCount: 0
    property real selectedBytes: 0
    property int anchorId: -1
    onSortModeChanged: anchorId = -1

    // (314) The episode in the player cannot be deleted: never marked, so
    // the number and the size in the bar are what goes (Gemini)
    function applySelection(map) {
        var ids = []
        var playing = appWindow.player.episodeId
        if (map[playing] === true) delete map[playing]
        for (var k in map) {
            if (map[k] === true) ids.push(parseInt(k, 10))
        }
        selected = map
        selectedCount = ids.length
        selectedBytes = downloaded.bytesOf(ids)
    }
    function selectedIds() {
        var ids = []
        for (var k in selected) {
            if (selected[k] === true) ids.push(parseInt(k, 10))
        }
        return ids
    }
    function copySelection() {
        var map = {}
        for (var k in selected) {
            if (selected[k] === true) map[k] = true
        }
        return map
    }
    function startSelect(ids) {
        var map = {}
        for (var i = 0; i < ids.length; i++) map[ids[i]] = true
        anchorId = ids.length === 1 ? ids[0] : -1
        selectMode = true
        list._keepTop = false
        appWindow.collapsePlayerBar()
        applySelection(map)
        hintRange()
    }
    function toggleSelect(id) {
        if (id === appWindow.player.episodeId) return
        var map = copySelection()
        if (map[id] === true) delete map[id]
        else map[id] = true
        anchorId = id
        applySelection(map)
        hintRange()
    }
    // (314) Once: how the range works (after the first mark)
    function hintRange() {
        if (selectedCount > 0) {
            appWindow.showHint("selectRange", qsTr("Press and hold another episode to mark all episodes in between"), page, true)
        }
    }
    // From the last tapped row to this one (all marked); without an anchor
    // only this row
    function rangeSelect(id) {
        var map = copySelection()
        var ids = anchorId >= 0 ? downloaded.episodeIdsBetween(anchorId, id) : []
        if (ids.length === 0) ids = [id]
        for (var i = 0; i < ids.length; i++) map[ids[i]] = true
        anchorId = id
        applySelection(map)
    }
    function selectList(ids) {
        var map = {}
        for (var i = 0; i < ids.length; i++) map[ids[i]] = true
        anchorId = -1
        applySelection(map)
    }
    function invertSelection() {
        var all = downloaded.allEpisodeIds()
        var map = {}
        for (var i = 0; i < all.length; i++) {
            if (selected[all[i]] !== true) map[all[i]] = true
        }
        anchorId = -1
        applySelection(map)
    }
    function endSelect() {
        selectMode = false
        anchorId = -1
        applySelection({})
    }
    function deleteSelected() {
        // (321, code review) Only what is in the list now (a mark left over
        // from an earlier search or filter stays out)
        var visible = {}
        var all = downloaded.allEpisodeIds()
        for (var v = 0; v < all.length; v++) visible[all[v]] = true
        var ids = []
        var marked = selectedIds()
        for (var m = 0; m < marked.length; m++) {
            if (visible[marked[m]] === true) ids.push(marked[m])
        }
        if (ids.length === 0) return
        var queued = 0
        for (var i = 0; i < ids.length; i++) {
            if (core.isQueued(ids[i])) queued++
        }
        var text = qsTr("Deleting %n downloads", "", ids.length) + " · "
                + TimeFormat.size(selectedBytes, qsTr("%1 MB"), qsTr("%1 GB"))
        if (queued > 0) text += " · " + qsTr("%n in the playlist", "", queued)
        endSelect()
        page.remorseBusy = true
        remorse.execute(text, function() { core.deleteDownloads(ids) })
    }

    // (314) Short names for the line above the list
    function sortShort(mode) {
        return mode === 1 ? qsTr("Newest")
             : mode === 2 ? qsTr("Oldest")
             : mode === 3 ? qsTr("By podcast")
             : mode === 5 ? qsTr("Largest")
             : qsTr("Last downloaded")
    }

    function refresh() {
        active = core.activeDownloads()
        stats = core.downloadStats()
        _statsStale = false
    }
    Component.onCompleted: refresh()

    // (282) The order is chosen on its own page
    // (314) Leaving the page ends the selection (no half selection left in
    // the ring, the player bar is back on the other pages)
    onStatusChanged: if (status === PageStatus.Inactive && selectMode) {
        endSelect()
    } else if (status === PageStatus.Active) {
        sortMode = parseInt(core.setting("downloads.sort", "0"))
        // (301) A page that stays alive in the ring: current numbers on arrival
        // (324) not on the first arrival (onCompleted has just calculated),
        // unless something changed since
        if (_activatedOnce || _statsStale) refresh()
        _activatedOnce = true
        checkArchivedRows()
    }

    Connections {
        target: core
        onDownloadsChanged: {
            page.active = core.activeDownloads()
            if (page.status === PageStatus.Active) page.stats = core.downloadStats()
            else page._statsStale = true
        }
        onDataChanged: {
            if (page.status === PageStatus.Active) page.stats = core.downloadStats()
            else page._statsStale = true
        }
    }

    EpisodeListModel {
        id: downloaded
        // (275) Without the archived ones: they have a section of their own
        filter: EpisodeListModel.DownloadedActive
        sortMode: page.sortMode
        search: page.searching ? page.searchText : ""
        // (313) Rows gone (deleted elsewhere, filter): marks of rows no longer
        // in the list are dropped, an empty list ends the mode
        onCountChanged: {
            if (!page.selectMode) return
            if (count === 0) { page.endSelect(); return }
            var all = allEpisodeIds()
            var map = {}
            for (var i = 0; i < all.length; i++) {
                if (page.selected[all[i]] === true) map[all[i]] = true
            }
            page.applySelection(map)
        }
    }

    // (275) Downloads of ended-and-archived podcasts and files kept with
    // "Save in Music": not touched by "Delete played downloads" and not in
    // its tile, so they are shown apart, at the very bottom, with the size
    EpisodeListModel {
        id: archivedDownloads
        filter: EpisodeListModel.DownloadedArchived
        sortMode: page.sortMode
        search: page.searching ? page.searchText : ""
    }

    // (282) The archived section, built once: in the footer below the list,
    // but when the list shows no rows (nothing downloaded outside the
    // archive, or "Downloaded" folded) in the header - a ListView hides its
    // footer when it has no rows
    Component {
        id: archivedSection
        Column {
            width: list.width
            // (324, code review) Set by the footer loader: the header copy has
            // rows only while it is shown, the footer copy only when wanted
            // (both were built before, with all rows)
            property bool inFooter: false
            readonly property bool rowsShown: inFooter ? (!page.archivedInHeader && page.archivedRowsWanted)
                                                       : page.archivedInHeader
        FoldHeader {
            id: archivedHeader
            visible: archivedDownloads.count > 0
            title: qsTr("Archived")
            subtitle: qsTr("%1 · %2")
                    .arg(archivedDownloads.count === 1 ? qsTr("1 episode") : qsTr("%1 episodes").arg(archivedDownloads.count))
                    .arg(TimeFormat.size(page.stats.archivedBytes || 0, qsTr("%1 MB"), qsTr("%1 GB")))
            foldable: false
        }
        Label {
            visible: archivedDownloads.count > 0
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            height: implicitHeight + Theme.paddingMedium
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            text: qsTr("Ended podcasts, files saved with \"Save in Music\" and copied archives – also their files that are still on the phone. \"Delete played downloads\" leaves them alone.")
        }
        // (287) Way to the settings of the archive (the other download
        // settings stay in the pulley, as on the other pages)
        BackgroundItem {
            id: archiveSettingsLine
            visible: archivedDownloads.count > 0
            width: parent.width
            height: Theme.itemSizeExtraSmall
            onClicked: pageStack.push(Qt.resolvedUrl("SettingsDownloadsPage.qml"), { toArchive: true })
            Label {
                anchors.right: parent.right
                anchors.rightMargin: Theme.horizontalPageMargin
                anchors.verticalCenter: parent.verticalCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: archiveSettingsLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                text: qsTr("Archive settings") + " ›"
            }
        }
        Repeater {
            model: rowsShown ? archivedDownloads : null
            delegate: EpisodeDelegate {
                width: list.width
                menuEnabled: false
                showFileSize: true
            }
        }
            Item { width: 1; height: Theme.paddingLarge }
        }
    }

    // (319) True while a countdown runs: a second tap on the tile would
    // trigger the waiting action at once (no more "Tap to undo")
    property bool remorseBusy: false
    RemorsePopup {
        id: remorse
        onTriggered: page.remorseBusy = false
        onCanceled: page.remorseBusy = false
    }

    // (279, 285) Header of "Archived" to the top (the end of the list stops
    // it when the rows do not fill the screen); used by the shortcut line
    NumberAnimation {
        id: scrollAnim
        target: list
        property: "contentY"
        duration: 250
        easing.type: Easing.InOutQuad
    }
    Timer {
        id: scrollTimer
        interval: 50
        onTriggered: {
            if (!list.footerItem) {
                return
            }
            scrollAnim.stop()
            scrollAnim.from = list.contentY
            scrollAnim.to = Math.max(list.originY, Math.min(list.footerItem.y,
                                     list.originY + list.contentHeight - list.height))
            scrollAnim.start()
        }
    }

    // (313) Selection mode: number and size of the marked files, cancel and
    // delete - fixed below the list so "Delete" stays in reach while scrolling
    Rectangle {
        id: selectBar
        visible: page.selectMode
        // (314) At the bottom, above the player bar: in reach of the thumb
        // (at the top it sat under the status glow, out of reach)
        anchors {
            bottom: parent.bottom
            bottomMargin: appWindow.playerBarHeight
            left: parent.left
            right: parent.right
            rightMargin: appWindow.playerPanelWidth
        }
        height: visible ? Theme.itemSizeLarge : 0
        color: Theme.rgba(Theme.highlightBackgroundColor, 0.2)
        z: 2

        Label {
            anchors {
                left: parent.left
                leftMargin: Theme.horizontalPageMargin
                right: selectButtons.left
                rightMargin: Theme.paddingMedium
                verticalCenter: parent.verticalCenter
            }
            truncationMode: TruncationMode.Fade
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.highlightColor
            text: qsTr("%n selected", "", page.selectedCount)
                  + (page.selectedCount > 0 ? " · " + TimeFormat.size(page.selectedBytes, qsTr("%1 MB"), qsTr("%1 GB")) : "")
        }
        // A Flow (ActionPill measures itself against it), filled from the right
        Flow {
            id: selectButtons
            anchors {
                right: parent.right
                rightMargin: Theme.horizontalPageMargin
                verticalCenter: parent.verticalCenter
            }
            width: Math.round(parent.width * 0.5)
            layoutDirection: Qt.RightToLeft
            spacing: Theme.paddingMedium
            ActionPill {
                kind: "destructive"
                enabled: page.selectedCount > 0
                text: qsTr("Delete")
                onClicked: page.deleteSelected()
            }
            ActionPill {
                text: qsTr("Cancel")
                onClicked: page.endSelect()
            }
        }
    }

    SilicaListView {
        id: list
        // Opens at the top with the page header (66, 83): no current item
        // (ListView keeps item 0 in view while the header grows), and the
        // top is kept until the user moves the list
        currentIndex: -1
        property bool _keepTop: true
        onMovementStarted: _keepTop = false
        onContentHeightChanged: {
            if (_keepTop) positionViewAtBeginning()
            page.checkArchivedRows()
        }
        onContentYChanged: page.checkArchivedRows()
        onHeightChanged: page.checkArchivedRows()
        onOriginYChanged: if (_keepTop) positionViewAtBeginning()
        Component.onCompleted: positionViewAtBeginning()
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight + selectBar.height
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: downloaded
        cacheBuffer: height

        PullDownMenu {
            // (313) Selection mode: only the quick selections
            MenuItem {
                visible: page.selectMode
                text: qsTr("Invert selection")
                onClicked: page.invertSelection()
            }
            MenuItem {
                visible: page.selectMode
                text: qsTr("Select all not in the playlist")
                onClicked: page.selectList(downloaded.episodeIdsNotQueued())
            }
            MenuItem {
                visible: page.selectMode
                text: qsTr("Select all")
                onClicked: page.selectList(downloaded.allEpisodeIds())
            }
            // (264, Thomas 6 Nov: no way from here to the download settings)
            // Navigation first, as "Settings" on the other pages
            MenuItem {
                visible: !page.selectMode
                text: qsTr("Download settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsDownloadsPage.qml"))
            }
            // (274) Not a text field in the pulley (the menu closes when the
            // keyboard comes up): the search field in the list header
            MenuItem {
                visible: (page.stats.count || 0) > 1 && !page.selectMode
                text: page.searching ? qsTr("Hide filter") : qsTr("Filter")
                onClicked: page.searching = !page.searching
            }
            // (274) The unplayed episodes of several podcasts at once
            MenuItem {
                visible: !page.selectMode
                text: qsTr("Download unplayed …")
                onClicked: pageStack.push(Qt.resolvedUrl("DownloadUnplayedDialog.qml"))
            }
            // (313) Selection mode; (314) named after what it is for, at the
            // bottom (the usual reason to come here: make room). "Delete
            // played downloads" is the tile now.
            MenuItem {
                visible: downloaded.count > 1 && !page.selectMode
                text: qsTr("Delete several …")
                onClicked: page.startSelect([])
            }
            // (314) Only while downloads run - then it is the emergency
            // brake, so the last entry (Gemini 14 Nov)
            MenuItem {
                visible: page.active.length > 0 && !page.selectMode
                text: qsTr("Cancel all downloads")
                onClicked: {
                    page.remorseBusy = true
                    remorse.execute(qsTr("Cancelling downloads"), function() { core.cancelAllDownloads() })
                }
            }
            // (303) "Add unplayed to the playlist" removed (decision 11 Nov)
        }

        header: Column {
            width: list.width

            PageHeader {
                title: qsTr("Downloads")
            }

            RingNeighbours { kind: "downloads"; ringMember: page.ringMember }

            // (190) Tiles instead of the header line: files, space used,
            // played (what "Delete played downloads" frees), free space
            StatTiles {
                visible: (page.stats.count || 0) > 0 || (page.stats.free || -1) >= 0
                // Three tiles, one row: number and size together
                tiles: {
                    var list = [
                        { value: (page.stats.count || 0) + " · " + TimeFormat.size(page.stats.bytes || 0, qsTr("%1 MB"), qsTr("%1 GB")),
                          label: qsTr("total incl. archive") },
                        // (314) Tap deletes them (the red button and the pulley
                        // entry are gone); not in the selection mode
                        { value: (page.stats.played || 0) > 0
                                 ? (page.stats.played + " · " + TimeFormat.size(page.stats.playedBytes || 0, qsTr("%1 MB"), qsTr("%1 GB")))
                                 : "–",
                          label: (page.stats.played || 0) > 0 && !page.selectMode ? qsTr("Delete played") : qsTr("already played"),
                          tappable: (page.stats.played || 0) > 0 && !page.selectMode,
                          destructive: true }
                    ]
                    if ((page.stats.free || -1) >= 0) {
                        list.push({ value: TimeFormat.size(page.stats.free, qsTr("%1 MB"), qsTr("%1 GB")), label: qsTr("free on the phone"),
                                    // (289) Below the reserve no download starts
                                    warning: (page.stats.reserve || 0) > 0 && page.stats.free < page.stats.reserve })
                    }
                    return list
                }
                // (319) Text with the number and size, like in the selection
                // mode; no second tap while the countdown runs
                onTileClicked: {
                    if (index !== 1 || page.remorseBusy) return
                    page.remorseBusy = true
                    remorse.execute(qsTr("Deleting %n played downloads", "", page.stats.played || 0) + " · "
                                    + TimeFormat.size(page.stats.playedBytes || 0, qsTr("%1 MB"), qsTr("%1 GB")),
                                    function() { core.cleanupDownloads(0) })
                }
            }
            // (217) How much of it is in the Music folder; (314) a little air
            // above it
            Item { width: 1; height: Theme.paddingSmall; visible: (page.stats.music || 0) > 0 }
            Label {
                visible: (page.stats.music || 0) > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                // (256) plainer words
                text: qsTr("%1 of them (%2) are in Music and Videos").arg(page.stats.music)
                      .arg(TimeFormat.size(page.stats.musicBytes || 0, qsTr("%1 MB"), qsTr("%1 GB")))
            }

            Item { width: 1; height: Theme.paddingMedium }

            SectionHeader {
                visible: page.active.length > 0
                text: qsTr("Running")
            }

            Repeater {
                id: activeRepeater
                model: page.active

                delegate: ListItem {
                    id: activeRow
                    onMenuOpenChanged: if (menuOpen) list._keepTop = false
                    property int episodeId: modelData.episodeId
                    property real progress: modelData.progress
                    property bool running: modelData.running
                    // Only the bar of this row - no reload of the whole
                    // section. Here in the row: the Repeater is in the list
                    // header, its id is not visible to the page (39)
                    Connections {
                        target: core
                        onDownloadProgressChanged: {
                            if (episodeId === activeRow.episodeId) {
                                activeRow.progress = progress
                                activeRow.running = true
                            }
                        }
                    }
                    width: list.width
                    contentHeight: Theme.itemSizeLarge
                    onClicked: pageStack.push(Qt.resolvedUrl("EpisodePage.qml"), { episodeId: activeRow.episodeId })

                    menu: Component {
                        ContextMenu {
                            MenuItem {
                                text: qsTr("Cancel download")
                                onClicked: core.cancelDownload(activeRow.episodeId)
                            }
                        }
                    }

                    CoverImage {
                        id: activeArt
                        x: Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        width: Theme.itemSizeMedium
                        height: width
                        source: modelData.imageUrl
                        fallbackText: modelData.podcastTitle
                    }

                    Column {
                        anchors {
                            left: activeArt.right
                            leftMargin: Theme.paddingMedium
                            right: parent.right
                            rightMargin: Theme.horizontalPageMargin
                            verticalCenter: parent.verticalCenter
                        }
                        Label {
                            width: parent.width
                            text: modelData.title
                            truncationMode: TruncationMode.Fade
                            color: activeRow.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            width: parent.width
                            text: activeRow.running
                                  ? modelData.podcastTitle + " · " + Math.round(activeRow.progress * 100) + " %"
                                  : modelData.podcastTitle + " · " + qsTr("waiting")
                            truncationMode: TruncationMode.Fade
                            font.pixelSize: Theme.fontSizeExtraSmall
                            color: activeRow.highlighted ? Theme.secondaryHighlightColor : Theme.secondaryColor
                        }
                        Item { width: 1; height: Theme.paddingSmall }
                        // Progress line as in the episode rows
                        Rectangle {
                            width: parent.width
                            height: Math.max(2, Math.round(Theme.paddingSmall / 2))
                            radius: height / 2
                            color: Theme.rgba(Theme.secondaryColor, 0.3)
                            Rectangle {
                                width: parent.width * Math.max(0, Math.min(1, activeRow.progress))
                                height: parent.height
                                radius: parent.radius
                                color: Theme.highlightColor
                            }
                        }
                    }
                }
            }

            // (274) Filter by episode or podcast title (as on the episode page)
            SearchField {
                visible: page.searching
                width: parent.width
                placeholderText: qsTr("Filter downloads")
                inputMethodHints: Qt.ImhNoPredictiveText
                focus: page.searching
                onTextChanged: page.searchText = text
                onVisibleChanged: if (!visible) text = ""
                EnterKey.iconSource: "image://theme/icon-m-enter-close"
                EnterKey.onClicked: focus = false
            }

            // (285) Shortcut to "Archived" at the end of the page (the header
            // of the section itself is in view when it sits in this header)
            BackgroundItem {
                id: archivedLine
                visible: archivedDownloads.count > 0 && !page.archivedInHeader
                width: parent.width
                height: Theme.itemSizeExtraSmall
                onClicked: {
                    list._keepTop = false
                    // (324) Rows first, then the jump (the target depends on them)
                    page.archivedRowsWanted = true
                    scrollTimer.restart()
                }
                Label {
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: archivedLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: qsTr("Archived: %1 · %2")
                          .arg(archivedDownloads.count === 1 ? qsTr("1 episode") : qsTr("%1 episodes").arg(archivedDownloads.count))
                          .arg(TimeFormat.size(page.stats.archivedBytes || 0, qsTr("%1 MB"), qsTr("%1 GB"))) + " ›"
                }
            }

            // (314) One line: "Downloaded · 27 · 2.2 GB" on the left, the order
            // on the right ("⇅ Last downloaded ›", opens the sort page) - it
            // was a section header and a "Sorted: …" line of its own
            BackgroundItem {
                id: sortLine
                visible: downloaded.count > 0 || (page.stats.count || 0) > 1
                width: parent.width
                // (323, blind test) Large font: the sorting no longer fits next
                // to "Downloaded · 26 · 2.0 GB" (it was cut off) - it goes below
                readonly property bool stacked: sortRow.visible
                    && downloadedLabel.implicitWidth + sortRow.width + 2 * Theme.horizontalPageMargin + Theme.paddingMedium > width
                height: stacked ? Theme.itemSizeSmall + Theme.itemSizeExtraSmall / 2 : Theme.itemSizeSmall
                enabled: (page.stats.count || 0) > 1
                onClicked: pageStack.push(Qt.resolvedUrl("SortDownloadsPage.qml"))
                Label {
                    id: downloadedLabel
                    anchors {
                        left: parent.left
                        leftMargin: Theme.horizontalPageMargin
                        right: sortLine.stacked ? parent.right : sortRow.left
                        rightMargin: sortLine.stacked ? Theme.horizontalPageMargin : Theme.paddingMedium
                    }
                    y: sortLine.stacked ? Theme.paddingSmall : (parent.height - height) / 2
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.highlightColor
                    text: qsTr("Downloaded") + " · " + downloaded.count + " · "
                          + TimeFormat.size(Math.max(0, (page.stats.bytes || 0) - (page.stats.archivedBytes || 0)), qsTr("%1 MB"), qsTr("%1 GB"))
                }
                Row {
                    id: sortRow
                    visible: sortLine.enabled
                    anchors.right: parent.right
                    anchors.rightMargin: Theme.horizontalPageMargin
                    y: sortLine.stacked ? parent.height - height - Theme.paddingSmall : (parent.height - height) / 2
                    spacing: Theme.paddingSmall / 2
                    SortArrows {
                        anchors.verticalCenter: parent.verticalCenter
                        tint: sortLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    }
                    Label {
                        anchors.verticalCenter: parent.verticalCenter
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: sortLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                        text: page.sortShort(page.sortMode) + " ›"
                    }
                }
            }

            // (283) Height of the loaded item only: an unloaded Loader kept
            // its old height as an empty gap
            Loader {
                width: parent.width
                height: item ? item.height : 0
                visible: item !== null
                sourceComponent: page.archivedInHeader ? archivedSection : null
            }
        }

        // (259) An opening menu ends "keep the top" (see HomePage)
        delegate: EpisodeDelegate {
            showFileSize: true
            onMenuOpenChanged: if (menuOpen) list._keepTop = false
            // (313) Selection mode
            selectable: true
            selectMode: page.selectMode
            selected: page.selectMode && page.selected[model.episodeId] === true
            onSelectStart: page.startSelect([model.episodeId])
            onSelectPodcast: page.startSelect(downloaded.episodeIdsOfPodcast(model.podcastId))
            onSelectToggled: page.toggleSelect(model.episodeId)
            onSelectRange: page.rangeSelect(model.episodeId)
        }

        // (275) Archived downloads, below everything. The rows of a footer
        // are no delegates (a menu would open out of view, see PlaylistPage):
        // no context menu here, tap opens the episode page with all actions.
        // (288) Always loaded (it used to be switched on and off with
        // archivedInHeader, and after the model filled the footer stayed
        // empty); only hidden while the header shows the section
        footer: Loader {
            width: list.width
            height: (item && !page.archivedInHeader) ? item.height : 0
            visible: !page.archivedInHeader
            sourceComponent: archivedSection
            onLoaded: {
                item.inFooter = true
                page.checkArchivedRows()
            }
        }

        ViewPlaceholder {
            enabled: downloaded.count === 0 && archivedDownloads.count === 0 && page.active.length === 0
            text: page.searching && page.searchText.length > 0 ? qsTr("No matches") : qsTr("No downloads")
            hintText: page.searching && page.searchText.length > 0 ? ""
                      : qsTr("Download episodes from their menu or page")
        }

        VerticalScrollDecorator {}
    }
}

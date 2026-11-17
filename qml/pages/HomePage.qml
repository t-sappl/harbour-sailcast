// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import harbour.sailcast 1.0
import "../"

// Start page: the newest unplayed episodes of all podcasts; started ones
// show their progress (the episode playing now live). "Continue listening"
// is on the playlist page since build 12.
// Part of the ring navigation (swipe left: podcasts, playlist).
Page {
    id: page
    objectName: "homePage"
    property bool ringMember: false

    // (293) Grouped by podcast (switch on the page since 299, remembered)
    property bool grouped: core.setting("home.grouped", "0") === "1"
    Connections {
        target: core
        onSettingsChanged: page.grouped = core.setting("home.grouped", "0") === "1"
    }

    // (302) Two views: "New" (flag set - the triage list) and "All unplayed"
    // (default). Rows that leave the view (Weglegen, playlist, download)
    // stay until the view changes, with Undo
    property bool emptySettled: false
    function settleEmpty() {
        emptySettled = false
        emptyTimer.restart()
    }
    onGroupedChanged: settleEmpty()
    property bool viewNew: core.setting("home.view", "all") === "new"
    // (335) Download filter: "" all, "downloaded", "notDownloaded"
    property string downloadFilter: core.setting("home.downloadFilter", "")
    readonly property bool filtered: downloadFilter === "downloaded" || downloadFilter === "notDownloaded"
    Connections {
        target: core
        onSettingsChanged: {
            page.viewNew = core.setting("home.view", "all") === "new"
            page.downloadFilter = core.setting("home.downloadFilter", "")
        }
    }
    onDownloadFilterChanged: { countNew(); settleEmpty() }

    EpisodeListModel {
        id: episodes
        filter: page.viewNew ? EpisodeListModel.HomeNew : EpisodeListModel.Home
        groupByPodcast: page.grouped
        downloadFilter: page.downloadFilter
        keepVanished: true
        onCountChanged: page.settleEmpty()
    }
    // (336, review) The filter counts at once (a list that stays empty sends no change)
    Component.onCompleted: { settleEmpty(); countNew() }
    // (304) Real number for the chip (the list model stops at 100)
    property int newTotal: core.homeNewCount()
    // (335) For the filter line: shown of all in the current view
    property int filterShown: 0
    property int filterAll: 0
    // (307) For the pulley: is any group open (read when the menu opens)
    property bool anyOpen: true
    // (340) For the line "n podcasts hidden · m new episodes in them"
    property int hiddenPodcasts: 0
    property int hiddenEpisodes: 0
    // (340, review) Also with a single podcast that is hidden; a page
    // property (the line sits in the list header, its id is not visible here)
    readonly property bool hiddenLineShown: core.podcastCount > 1 || hiddenPodcasts > 0
    function countNew() {
        newTotal = core.homeNewCount()
        hiddenPodcasts = core.hiddenFromNewCount()
        hiddenEpisodes = hiddenPodcasts > 0 ? core.homeHiddenCount(viewNew) : 0
        if (filtered) {
            filterShown = core.homeCount(viewNew, true)
            filterAll = core.homeCount(viewNew, false)
        }
    }
    Connections {
        target: core
        onDataChanged: page.countNew()
        // (337, review) Episode changes come with every saved position while
        // playing: only the chip number then, the filter line on data changes
        onEpisodeChanged: page.newTotal = core.homeNewCount()
    }
    Connections {
        target: episodes
        onCountChanged: page.countNew()
    }

    // Kept rows end with a swipe to another ring page (not when an episode
    // page or a dialog is opened over this one)
    // (304) Once, the first time the view "New" is shown: what it is and
    // where the episodes go that leave it
    function hintNew() {
        if (page.viewNew && page.status === PageStatus.Active) {
            appWindow.showHint("homeNew", qsTr("\"New\" shows what you haven't looked at yet. ➕ (playlist and download) or Set aside take an episode out – it stays in \"All unplayed\"."), page)
        }
    }
    onViewNewChanged: { hintNew(); settleEmpty(); countNew() }

    onStatusChanged: {
        // (310) Before the page slides in: at the top if it was at the top
        // (no visible jump); once shown, a few strict checks
        if (status === PageStatus.Activating && page._topPinned) {
            page.scrollToTop()
        }
        if (status === PageStatus.Active) {
            page.healStrictCheck()
            hintNew()
        }
        if (status === PageStatus.Inactive) {
            var top = pageStack.currentPage
            if (top && top !== page && top.ringMember !== undefined) {
                episodes.clearKept()
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
        // (310) Top = the top edge of the header item (its y), as in Sailwave:
        // positionViewAtBeginning() put the first row at the top in Qt 5.6 and
        // could push the header out of sight
        onContentHeightChanged: if (_keepTop) page.scrollToTop()
        onOriginYChanged: if (_keepTop) page.scrollToTop()
        Component.onCompleted: positionViewAtBeginning()
        onMovementEnded: healHeaderTimer.restart()
        onContentYChanged: {
            if (headerItem && !pinTopTimer.running) {
                page._topPinned = contentY <= headerItem.y + 3
            }
        }
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        model: episodes
        // Keeps a few screens of delegates (and their images) alive
        cacheBuffer: height

        header: Column {
            width: list.width
            // (310, as Sailwave TopStationsPage v129) A header that grows or
            // shrinks (update progress, chips, neighbours) while the view is
            // at its top: the view goes to the new top at once
            onHeightChanged: page.headerHeightChanged()
            PageHeader {
                title: qsTr("New episodes")
                description: core.updating ? qsTr("Updating %1 %").arg(Math.round(core.updateProgress * 100))
                                           : (core.syncing ? qsTr("Synchronizing") : "")
            }
            // (255) Neighbours of the ring, below the header
            RingNeighbours { kind: "home"; ringMember: page.ringMember }
            // (299) Sort by date / group by podcast: a switch on the page
            // (it was in the pulley, where "Sort by date" did nothing: the
            // setting signal already flipped the flag and the click flipped
            // it back)
            // (304, Thomas + Gemini 13 Nov) Left the view (what), right the
            // arrangement (how) as one plain switch without a chip frame -
            // four equal chips read like four choices
            Item {
                visible: core.podcastCount > 0
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                height: visible ? viewChips.height + Theme.paddingMedium : 0
                Row {
                    id: viewChips
                    spacing: Theme.paddingSmall
                    Chip {
                        text: page.newTotal > 0 ? qsTr("New (%1)").arg(page.newTotal) : qsTr("New")
                        on: page.viewNew
                        onClicked: { episodes.clearKept(); core.setSetting("home.view", "new") }
                    }
                    Chip {
                        text: qsTr("All unplayed")
                        on: !page.viewNew
                        onClicked: { episodes.clearKept(); core.setSetting("home.view", "all") }
                    }
                }
                MouseArea {
                    id: arrangeSwitch
                    anchors.right: parent.right
                    anchors.verticalCenter: viewChips.verticalCenter
                    width: arrangeRow.width + Theme.paddingMedium
                    height: Math.max(viewChips.height, Theme.itemSizeExtraSmall * 0.7)
                    onClicked: core.setSetting("home.grouped", page.grouped ? "0" : "1")
                    Row {
                        id: arrangeRow
                        anchors.right: parent.right
                        anchors.verticalCenter: parent.verticalCenter
                        spacing: Theme.paddingSmall / 2
                        // Up and down: "change the order". (310) Two small solid
                        // triangles (a square turned by 45° and cut in half) -
                        // the scaled theme chevrons ran into a grey blob
                        Column {
                            id: arrows
                            anchors.verticalCenter: parent.verticalCenter
                            spacing: Math.max(2, Math.round(Theme.paddingSmall / 2))
                            readonly property real w: Math.round(Theme.iconSizeExtraSmall * 0.55)
                            readonly property color tint: arrangeSwitch.pressed ? Theme.highlightColor : Theme.secondaryHighlightColor
                            Repeater {
                                model: 2
                                Item {
                                    width: arrows.w
                                    height: Math.round(arrows.w / 2)
                                    clip: true
                                    Rectangle {
                                        width: Math.round(arrows.w / Math.SQRT2)
                                        height: width
                                        x: (arrows.w - width) / 2
                                        // up: the top half of the turned square, down: the bottom half
                                        y: index === 0 ? parent.height - height / 2 : -height / 2
                                        rotation: 45
                                        color: arrows.tint
                                    }
                                }
                            }
                        }
                        Label {
                            anchors.verticalCenter: parent.verticalCenter
                            text: page.grouped ? qsTr("By podcast") : qsTr("By date")
                            font.pixelSize: Theme.fontSizeSmall
                            color: arrangeSwitch.pressed ? Theme.highlightColor : Theme.secondaryHighlightColor
                        }
                    }
                }
            }
            // (335, Gemini 16 Nov) While the download filter is on, always
            // visible in highlight colour, so episodes never seem missing.
            // Tap on the text: downloaded <-> not downloaded; ✕: all again
            Item {
                id: filterLine
                visible: page.filtered && core.podcastCount > 0
                width: parent.width
                height: visible ? Math.max(Theme.itemSizeExtraSmall, filterText.height + Theme.paddingSmall) : 0
                BackgroundItem {
                    id: filterSwap
                    anchors { left: parent.left; right: filterClear.left; top: parent.top; bottom: parent.bottom }
                    onClicked: core.setSetting("home.downloadFilter",
                                               page.downloadFilter === "downloaded" ? "notDownloaded" : "downloaded")
                    Label {
                        id: filterText
                        x: Theme.horizontalPageMargin
                        width: parent.width - Theme.horizontalPageMargin
                        anchors.verticalCenter: parent.verticalCenter
                        wrapMode: Text.Wrap
                        font.pixelSize: Theme.fontSizeSmall
                        color: Theme.highlightColor
                        text: (page.downloadFilter === "downloaded"
                               ? qsTr("Only downloaded (%1 of %2)") : qsTr("Only not downloaded (%1 of %2)"))
                              .arg(page.filterShown).arg(page.filterAll)
                    }
                }
                IconButton {
                    id: filterClear
                    anchors { right: parent.right; rightMargin: Theme.horizontalPageMargin - Theme.paddingMedium; verticalCenter: parent.verticalCenter }
                    width: Theme.itemSizeExtraSmall
                    height: Theme.itemSizeExtraSmall
                    icon.source: "image://theme/icon-m-clear"
                    onClicked: {
                        if (clearBubble.takeHeld()) return
                        core.setSetting("home.downloadFilter", "")
                    }
                    HoldNameBubble { id: clearBubble; attached: true; name: qsTr("Show all") }
                }
            }
            // (340, Thomas 17 Nov) Podcasts hidden here, as "n podcasts hidden"
            // on the statistics page: why "7 new episodes" can leave the page
            // empty, and the way to the choice of all podcasts at once.
            // Always there (Thomas: also shows that the choice exists)
            BackgroundItem {
                id: hiddenLine
                visible: page.hiddenLineShown
                width: parent.width
                height: visible ? Math.max(Theme.itemSizeExtraSmall, hiddenText.height + Theme.paddingSmall) : 0
                onClicked: pageStack.push(Qt.resolvedUrl("NewHiddenPage.qml"), { onlyNew: page.viewNew })
                Label {
                    id: hiddenText
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    color: hiddenLine.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: (page.hiddenPodcasts > 0 ? qsTr("%n podcasts hidden", "", page.hiddenPodcasts)
                                                   : qsTr("All podcasts shown"))
                          + (page.hiddenEpisodes > 0
                             ? " · " + (page.viewNew ? qsTr("%n new episodes in them", "", page.hiddenEpisodes)
                                                     : qsTr("%n unplayed episodes in them", "", page.hiddenEpisodes))
                             : "")
                          + " ›"
                }
            }
        }

        PullDownMenu {
            busy: core.updating || core.syncing
            // (253) The download count is read when the menu opens
            onActiveChanged: if (active) { homeDownloads.refresh(); page.anyOpen = episodes.anyGroupOpen() }
            MenuItem {
                text: qsTr("Settings")
                onClicked: pageStack.push(Qt.resolvedUrl("SettingsPage.qml"))
            }
            MenuItem {
                visible: core.podcastCount === 0
                text: qsTr("Import and export")
                onClicked: pageStack.push(Qt.resolvedUrl("ImportExportPage.qml"))
            }
            // Bookmarks: in the playlist pulley since 73, here too since 255
            MenuItem {
                visible: core.bookmarkCount > 0
                text: qsTr("Bookmarks (%1)").arg(core.bookmarkCount)
                onClicked: pageStack.push(Qt.resolvedUrl("BookmarksPage.qml"))
            }
            // (253) Downloads here too (in the playlist pulley since 73,
            // hard to find there - usability audit #10)
            DownloadsMenuItem {
                id: homeDownloads
            }
            // (335, Gemini 16 Nov) A choice page, no blind cycling in the pulley
            MenuItem {
                visible: core.podcastCount > 0
                text: qsTr("Filter by download …")
                onClicked: pageStack.push(Qt.resolvedUrl("DownloadFilterPage.qml"))
            }
            // (293) The list as an inbox: empty it in one go (the flag
            // only, the episodes stay unplayed)
            // (307) Fold or unfold all groups ("All unplayed" by podcast)
            MenuItem {
                visible: page.grouped && !page.viewNew && episodes.count > 0
                text: page.anyOpen ? qsTr("Fold all") : qsTr("Unfold all")
                onClicked: episodes.setAllGroupsCollapsed(page.anyOpen)
            }
            MenuItem {
                visible: page.viewNew && page.newTotal > 0
                // (335) With the download filter only what is shown
                text: page.filtered ? qsTr("Clear %n shown", "", page.newTotal) : qsTr("Clear New")
                onClicked: {
                    // (322) Fixed at the tap: the number in the banner and what is cleared
                    var upTo = core.newEpisodesSnapshot()
                    var total = page.newTotal
                    if (page.filtered) {
                        // (336, review) The filter of the tap
                        var filter = page.downloadFilter
                        removeAllRemorse.execute(qsTr("Clearing %n shown episodes", "", total),
                                                 function() { episodes.clearKept(); core.removeShownFromNew(upTo, filter) })
                    } else {
                        removeAllRemorse.execute(qsTr("Clearing %n new episodes", "", total),
                                                 function() { episodes.clearKept(); core.removeAllFromNew(upTo) })
                    }
                }
            }
            // Search on the device (86); (314) always second from the bottom,
            // above Update (Fold/Clear New come and go above it)
            MenuItem {
                visible: core.podcastCount > 0
                text: qsTr("Search my podcasts")
                onClicked: pageStack.push(Qt.resolvedUrl("LocalSearchPage.qml"))
            }
            // Separate sync entry (86) when "Sync und Update" is off
            MenuItem {
                visible: appWindow.separateSync
                enabled: !core.syncing
                text: qsTr("Synchronize")
                onClicked: appWindow.syncOnly()
            }
            MenuItem {
                visible: core.podcastCount > 0 || core.syncConfigured
                enabled: !core.updating && !core.syncing
                // Sync (if set up and combined, 86) and then all feeds - see
                // appWindow.updateFromPulley
                text: qsTr("Update")
                onClicked: appWindow.updateFromPulley()
            }
        }

        // (258, Thomas 6 Nov: the menu of a row ran down behind the player
        // bar) _keepTop pulled the list back to the top as soon as the
        // opening menu grew the content, so it was never scrolled into
        // view - an opening menu ends the "keep the top" state
        // (293) With grouping the header of the podcast sits above the
        // first row of its group (part of the row, as on the episodes page)
        delegate: Column {
            id: rowColumn
            width: list.width
            // (306) Group header: the podcast's cover and name on the left,
            // the number of its episodes in this view and › on the right (tap:
            // the podcast page). The rows below go without the cover.
            readonly property real coverSize: Theme.itemSizeExtraSmall * 0.75
            // (307) "All unplayed": a tap folds or unfolds the group (the
            // chevron turns, as "› N chapters"; remembered per podcast); in
            // "New" it opens the podcast page. Long press: open the podcast,
            // mark all as played, its settings.
            readonly property bool foldable: page.grouped && !page.viewNew
            readonly property bool folded: model.groupCollapsed === true
            ListItem {
                id: groupHeader
                visible: page.grouped && model.sectionStart === true
                width: parent.width
                contentHeight: Theme.itemSizeSmall
                height: visible ? implicitHeight : 0
                onClicked: {
                    if (rowColumn.foldable) {
                        list._keepTop = false
                        episodes.setGroupCollapsed(model.podcastId, !rowColumn.folded)
                    } else {
                        pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: model.podcastId })
                    }
                }
                onMenuOpenChanged: { if (menuOpen) list._keepTop = false; else healHeaderTimer.restart() }
                menu: Component {
                    ContextMenu {
                        MenuItem {
                            text: qsTr("Open podcast")
                            onClicked: pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: model.podcastId })
                        }
                        MenuItem {
                            // (321, code review) In "New" the head counts the new ones only, the
                            // action would mark all unplayed of the podcast: only where they are the same
                            visible: model.groupCount > 0 && !page.viewNew
                            text: qsTr("Mark all as played")
                            onClicked: appWindow.remorseMarkAllPlayed(model.podcastId, model.podcastTitle)
                        }
                        MenuItem {
                            text: qsTr("Podcast settings")
                            onClicked: pageStack.push(Qt.resolvedUrl("PodcastSettingsPage.qml"),
                                                      { podcastId: model.podcastId, podcastTitle: model.podcastTitle })
                        }
                    }
                }
                CoverImage {
                    id: groupCover
                    x: Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    width: rowColumn.coverSize
                    height: width
                    source: groupHeader.visible ? (model.podcastCover || "") : ""
                    fallbackText: model.podcastTitle
                }
                Label {
                    anchors {
                        left: groupCover.right
                        leftMargin: Theme.paddingMedium
                        right: groupCount.left
                        rightMargin: Theme.paddingMedium
                        verticalCenter: parent.verticalCenter
                    }
                    text: model.section || ""
                    truncationMode: TruncationMode.Fade
                    color: Theme.highlightColor
                }
                Label {
                    id: groupCount
                    anchors {
                        right: rowColumn.foldable ? groupChevron.left : parent.right
                        rightMargin: rowColumn.foldable ? Theme.paddingSmall : Theme.horizontalPageMargin
                        verticalCenter: parent.verticalCenter
                    }
                    text: rowColumn.foldable ? (model.groupCount > 0 ? "" + model.groupCount : "")
                                             : (model.groupCount > 0 ? model.groupCount + " " : "") + "\u203a"
                    font.pixelSize: Theme.fontSizeSmall
                    color: groupHeader.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                }
                Image {
                    id: groupChevron
                    visible: rowColumn.foldable
                    anchors {
                        right: parent.right
                        rightMargin: Theme.horizontalPageMargin - Theme.paddingSmall
                        verticalCenter: parent.verticalCenter
                    }
                    width: Theme.iconSizeExtraSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    source: "image://theme/icon-m-right?" + (groupHeader.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor)
                    rotation: rowColumn.folded ? 0 : 90
                    Behavior on rotation { NumberAnimation { duration: 150 } }
                }
            }
            EpisodeDelegate {
                // (307) A folded group: the row only carries the header
                visible: !rowColumn.folded
                width: parent.width
                newEpisodesList: true
                showMarkOlder: true
                newView: page.viewNew
                // (304, Thomas + Gemini 13 Nov) Downloaded: the real size on the
                // right - the visible reason why the quick button shows ➕
                showFileSize: true
                // (318) Only what was set aside looks set aside: a row that
                // left "New" through ➕ (in the playlist, maybe loading) stays
                // bright with its ✓ and ring (it looked like "Set aside")
                setAside: page.viewNew && model.isNew !== true && model.played !== true
                          && model.queued !== true && model.downloading !== true
                showPodcastTitle: !page.grouped
                showArt: !page.grouped
                textIndent: rowColumn.coverSize + Theme.paddingMedium
                onMenuOpenChanged: { if (menuOpen) list._keepTop = false; else healHeaderTimer.restart() }
            }
            // (306) Shortened group ("All unplayed" by podcast): unfold it here,
            // fold it again; beyond 50 the podcast page
            BackgroundItem {
                id: moreRow
                readonly property int more: model.groupMore || 0
                readonly property bool open: model.groupExpanded === true
                visible: page.grouped && (more > 0 || open)
                width: parent.width
                height: visible ? Theme.itemSizeExtraSmall : 0
                onClicked: {
                    if (open && more > 0) {
                        pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: model.podcastId })
                    } else {
                        list._keepTop = false
                        episodes.unfoldGroup(model.podcastId, !open)
                    }
                }
                // (307) Unfolding here: the chevron on the left turns down, as
                // "› N chapters" in a row; going to another page: › at the end
                readonly property bool toPage: open && more > 0
                Image {
                    id: moreChevron
                    visible: !moreRow.toPage
                    x: Theme.horizontalPageMargin + rowColumn.coverSize + Theme.paddingMedium
                    anchors.verticalCenter: parent.verticalCenter
                    width: Theme.iconSizeExtraSmall
                    height: width
                    sourceSize.width: width
                    sourceSize.height: height
                    source: "image://theme/icon-m-right?" + (moreRow.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor)
                    rotation: moreRow.open ? 90 : 0
                    Behavior on rotation { NumberAnimation { duration: 150 } }
                }
                Label {
                    x: moreRow.toPage ? Theme.horizontalPageMargin + rowColumn.coverSize + Theme.paddingMedium
                                      : moreChevron.x + moreChevron.width + Theme.paddingSmall
                    width: parent.width - x - Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    truncationMode: TruncationMode.Fade
                    font.pixelSize: Theme.fontSizeSmall
                    color: moreRow.highlighted ? Theme.highlightColor : Theme.secondaryHighlightColor
                    text: moreRow.toPage ? qsTr("+ %n more on the podcast page", "", moreRow.more) + " \u203a"
                          : (moreRow.open ? qsTr("Show fewer")
                                       : (page.viewNew ? qsTr("%n more new", "", moreRow.more) : qsTr("%n more unplayed", "", moreRow.more)))
                }
            }
        }

        Timer {
            id: emptyTimer
            interval: 250
            onTriggered: page.emptySettled = episodes.count === 0
        }

        ViewPlaceholder {
            // (219) Without podcasts: GetStarted below
            // (312) Only after the list has stayed empty for a moment: when the
            // view is switched the model reloads a tick later, which flashed
            // "All caught up" over the rows
            enabled: page.emptySettled && episodes.count === 0
                     && (core.podcastCount > 0 || core.databaseError.length > 0)
            // (324, Thomas: switching New / All unplayed still showed "All
            // caught up" over the rows now and then) Silica fades the text out
            // when it is switched off - with rows in the list it goes at once
            visible: (enabled || episodes.count === 0) && opacity > 0
            text: core.databaseError.length > 0 ? qsTr("Database error")
                  // (336) The filter texts only when the filter hides something
                  : page.filtered && page.filterAll > 0 ? (page.downloadFilter === "downloaded" ? qsTr("Nothing downloaded here") : qsTr("Everything here is downloaded"))
                  : (page.viewNew ? qsTr("Nothing new") : qsTr("All caught up"))
            // (335) With the filter: how to see the rest
            hintText: core.databaseError.length > 0 ? core.databaseError
                      : page.filtered && page.filterAll > 0 ? qsTr("Tap ✕ above to show all episodes")
                      // (340) Everything waiting is in hidden podcasts
                      : page.hiddenEpisodes > 0 && page.hiddenLineShown ? qsTr("Tap the line above to choose the podcasts shown here")
                      : (page.viewNew ? qsTr("Episodes you have set aside or started are in \"All unplayed\"")
                                      : qsTr("No unplayed episodes waiting"))
        }

        VerticalScrollDecorator {}
    }

    RemorsePopup { id: removeAllRemorse }

    // --- (310) The page header always whole (taken over from Sailwave's
    // TopStationsPage/TrackHistoryPage, where the same fault was solved) ---
    // The rows have different heights (group headers, folded groups,
    // chapters) and Qt 5.6 only estimates the rows above the view; after
    // swiping between the ring pages the list could think it was at its
    // beginning while the header top lay above the view.

    // All the way up INCLUDING the header: its top edge is the top of the page
    function scrollToTop() {
        if (list.headerItem) {
            list.contentY = list.headerItem.y
        } else {
            list.positionViewAtBeginning()
        }
    }

    // The view is at (or above) the header top; kept up to date on every
    // scroll - a growing header does not change contentY, so the flag is
    // still right when the header changes
    property bool _topPinned: true
    function headerHeightChanged() {
        if (_topPinned) {
            pinTopTimer.restart()
        }
    }

    Timer {
        id: pinTopTimer
        interval: 1
        onTriggered: {
            if (page._topPinned && !list.dragging && !list.moving && list.headerItem
                    && list.contentY > list.headerItem.y + 3) {
                list.contentY = list.headerItem.y
            }
        }
    }

    // After the page became active (swiped to), the first checks are strict:
    // a title partly scrolled out goes back to the top of the header (nobody
    // stops there on purpose). Ends of movements only use the "stuck" rule.
    property int healStrictLeft: 0
    function healStrictCheck() {
        healStrictLeft = 3
        healHeaderTimer.restart()
    }

    function healHeader() {
        if (list.moving || list.dragging || !list.headerItem || list.headerItem.height <= 0
                || (list.pullDownMenu && list.pullDownMenu.active)) {
            return
        }
        var headerTop = list.headerItem.y
        if (healStrictLeft > 0) {
            healStrictLeft--
            if (healStrictLeft > 0) {
                healHeaderTimer.restart()
            }
            if (list.contentY > headerTop + 3 && list.contentY < headerTop + list.headerItem.height - 3) {
                list.contentY = headerTop
                return
            }
        }
        // Stuck: the list thinks it is at its beginning (cannot be pulled
        // down) although the header top lies above the view
        if (list.atYBeginning && list.contentY > headerTop + 3) {
            console.warn("Sailcast scroll: header was hidden (contentY " + list.contentY + ", headerTop "
                         + headerTop + ", originY " + list.originY + ") - back to the top")
            list.forceLayout()
            list.positionViewAtBeginning()
            if (list.contentY > list.headerItem.y + 3) {
                list.contentY = list.headerItem.y
            }
        }
    }

    Timer {
        id: healHeaderTimer
        interval: 150
        onTriggered: page.healHeader()
    }

    // (219) First steps as buttons (as on the Podcasts page)
    GetStarted {
        visible: core.podcastCount === 0 && core.databaseError.length === 0
        anchors.verticalCenter: parent.verticalCenter
    }
}

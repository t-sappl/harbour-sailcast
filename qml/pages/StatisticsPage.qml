// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// (297) Listening statistics: period chips, three tiles, course, year heat
// map, series, weekdays, records, folders, categories and the ranking of the
// podcasts. Numbers come from the listening log (core.listeningStatistics);
// the period is remembered ("statistics.period").
Page {
    id: page
    objectName: "statisticsPage"

    property int period: parseInt(core.setting("statistics.period", "30"))
    // "main" = main category only (default), "split" = "All categories", the
    // time shared equally between the categories of a podcast
    property string categoryMode: core.setting("statistics.categories", "main")
    // (324, code review) Filled once by refresh() (was a binding, plus
    // onCompleted, plus Active: three full calculations on opening)
    property var stats: ({})
    readonly property bool empty: stats.empty === true
    property real maxBar: 1
    property real maxWeekday: 1
    property real maxHeat: 1

    function refresh() {
        stats = core.listeningStatistics(period)
        var bars = stats.bars || []
        var m = 1
        for (var i = 0; i < bars.length; i++) m = Math.max(m, bars[i].seconds)
        maxBar = m
        var w = stats.weekdays || []
        m = 1
        for (i = 0; i < w.length; i++) m = Math.max(m, w[i].seconds)
        maxWeekday = m
        var h = stats.heat || []
        m = 1
        for (i = 0; i < h.length; i++) m = Math.max(m, h[i])
        maxHeat = m
    }
    // (321, code review) After "Reset statistics" / "Delete listening history"
    // the page shows the new state without leaving it
    Connections {
        target: core
        onDataChanged: if (status === PageStatus.Active) refresh()
    }
    function choosePeriod(days) {
        period = days
        core.setSetting("statistics.period", "" + days)
        refresh()
    }
    function chooseCategories(mode) {
        categoryMode = mode
        core.setSetting("statistics.categories", mode)
    }
    // 4980 -> "1 h 23 min", 7200 -> "2 h", 1500 -> "25 min"
    function dur(seconds) {
        var m = Math.round(seconds / 60)
        if (seconds > 0 && m < 1) m = 1
        if (m < 60) return qsTr("%1 min").arg(m)
        var h = Math.floor(m / 60)
        var r = m % 60
        return r === 0 ? qsTr("%1 h").arg(h) : qsTr("%1 h %2 min").arg(h).arg(r)
    }
    // Short value for a tile: "52 h", "1.5 h", "25 min"
    function tileValue(seconds) {
        var hours = seconds / 3600
        if (hours >= 10) return qsTr("%1 h").arg(Math.round(hours))
        if (hours >= 1) return qsTr("%1 h").arg(Number(hours).toLocaleString(Qt.locale(), "f", 1))
        return qsTr("%1 min").arg(Math.round(seconds / 60))
    }
    function heatColor(value) {
        if (value < 0) return "transparent"
        if (value <= 0) return Theme.rgba(Theme.primaryColor, 0.08)
        var level = Math.min(4, Math.max(1, Math.ceil(4 * value / maxHeat)))
        return Theme.rgba(Theme.highlightColor, [0, 0.28, 0.48, 0.72, 1][level])
    }

    // Again on coming back (dataChanged is not followed while not active),
    // not on the first activation: onCompleted has just calculated
    property bool _activatedOnce: false
    onStatusChanged: {
        if (status === PageStatus.Active) {
            if (_activatedOnce) refresh()
            _activatedOnce = true
        }
    }
    Component.onCompleted: refresh()

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        contentHeight: column.height + Theme.paddingLarge
        clip: true

        PullDownMenu {
            MenuItem {
                text: qsTr("Reset statistics")
                visible: !page.empty
                onClicked: appWindow.remorseResetStatistics()
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader { title: qsTr("Listening statistics") }

            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingSmall
                Chip { text: qsTr("7 days"); on: page.period === 7; onClicked: page.choosePeriod(7) }
                Chip { text: qsTr("30 days"); on: page.period === 30; onClicked: page.choosePeriod(30) }
                Chip { text: qsTr("All"); on: page.period === 0; onClicked: page.choosePeriod(0) }
            }
            Item { width: 1; height: Theme.paddingLarge }

            Label {
                visible: page.empty
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: Theme.secondaryHighlightColor
                text: qsTr("Nothing recorded yet. Your listening time appears here after you have played an episode.")
            }

            Column {
                width: parent.width
                visible: !page.empty

                StatTiles {
                    tiles: [
                        { value: page.tileValue(page.stats.seconds || 0), label: qsTr("listened") },
                        { value: page.tileValue(page.stats.saved || 0), label: qsTr("time saved") },
                        { value: "" + (page.stats.episodes || 0), label: qsTr("episodes listened to") }
                    ]
                }
                // (304) No topPadding: Silica's Label does not offer it (Qt 5.6 revision) - the page did not load
                Item { width: 1; height: Theme.paddingSmall }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Listened: your actual listening time. Time saved: how much less time you needed by playing faster. Episodes: listened to for at least one minute.")
                }

                // --- Course: one bar per day (7, 30 days); all: day, week or month
                // after the length of the log (314)
                SectionHeader { text: qsTr("Over time") }
                Item {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeHuge
                    Row {
                        id: barRow
                        anchors.fill: parent
                        spacing: Math.max(1, Math.round(Theme.paddingSmall / ((page.stats.bars || []).length > 20 ? 4 : 2)))
                        Repeater {
                            model: page.stats.bars || []
                            Item {
                                width: (barRow.width - barRow.spacing * (Math.max(1, (page.stats.bars || []).length) - 1))
                                       / Math.max(1, (page.stats.bars || []).length)
                                height: barRow.height
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    height: Math.max(modelData.seconds > 0 ? 2 : 0, parent.height * modelData.seconds / page.maxBar)
                                    radius: Math.min(3, width / 3)
                                    color: Theme.highlightColor
                                    opacity: index === (page.stats.bars || []).length - 1 ? 1 : 0.75
                                }
                            }
                        }
                    }
                }
                Item {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeExtraSmall / 2
                    Label {
                        anchors.left: parent.left
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                        text: ((page.stats.bars || [])[0] || {}).label || ""
                    }
                    Label {
                        anchors.right: parent.right
                        font.pixelSize: Theme.fontSizeTiny
                        color: Theme.secondaryColor
                        text: ((page.stats.bars || [])[(page.stats.bars || []).length - 1] || {}).label || ""
                    }
                }

                // --- Activity of the last year (always, whatever the period)
                SectionHeader { text: qsTr("Activity in the last year") }
                Item {
                    id: heatArea
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    readonly property real cell: width / 53
                    height: cell * 7
                    Repeater {
                        model: page.stats.heat || []
                        Rectangle {
                            x: Math.floor(index / 7) * heatArea.cell
                            y: (index % 7) * heatArea.cell
                            width: heatArea.cell - 1
                            height: heatArea.cell - 1
                            radius: 1
                            color: page.heatColor(modelData)
                        }
                    }
                }
                Item {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeExtraSmall / 2
                    Repeater {
                        model: page.stats.heatMonths || []
                        Label {
                            // Not the month names that would run into the next one
                            visible: modelData.week < 50 && (index === 0 ? ((page.stats.heatMonths || [])[1] || { week: 53 }).week > 3 : true)
                            x: modelData.week * heatArea.cell
                            y: Theme.paddingSmall
                            font.pixelSize: Theme.fontSizeTiny
                            color: Theme.secondaryColor
                            text: modelData.label
                        }
                    }
                }

                // --- Series (322: all time, not the chosen period) and rhythm (the period)
                SectionHeader { text: qsTr("Streak (all time)") }
                StatTiles {
                    tiles: [
                        { value: "" + (page.stats.currentStreak || 0), label: qsTr("days in a row (current)") },
                        { value: "" + (page.stats.longestStreak || 0), label: qsTr("days in a row (record, all time)") }
                    ]
                }
                SectionHeader { text: qsTr("Rhythm") }
                StatTiles {
                    tiles: [
                        { value: "" + (page.stats.activePercent || 0) + " %", label: qsTr("days with listening") },
                        { value: page.dur(page.stats.perActiveDay || 0), label: qsTr("per listening day") }
                    ]
                }
                // (304) No topPadding: Silica's Label does not offer it (Qt 5.6 revision) - the page did not load
                Item { width: 1; height: Theme.paddingSmall }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: qsTr("Days in a row: days one after the other on which you listened. The current count goes up to yesterday if nothing was played today. Both counts ignore the chosen period.")
                }

                // --- Weekdays
                SectionHeader { text: qsTr("Weekdays") }
                Item {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.itemSizeLarge + Theme.itemSizeExtraSmall / 2
                    Row {
                        anchors.fill: parent
                        spacing: Theme.paddingSmall
                        Repeater {
                            model: page.stats.weekdays || []
                            Item {
                                width: (parent.width - 6 * Theme.paddingSmall) / 7
                                height: parent.height
                                Rectangle {
                                    anchors.bottom: dayLabel.top
                                    anchors.bottomMargin: Theme.paddingSmall
                                    width: parent.width
                                    height: Math.max(modelData.seconds > 0 ? 2 : 0,
                                                     (parent.height - dayLabel.height - Theme.paddingSmall) * modelData.seconds / page.maxWeekday)
                                    radius: 3
                                    color: Theme.highlightColor
                                    opacity: modelData.seconds === page.maxWeekday ? 1 : 0.7
                                }
                                Label {
                                    id: dayLabel
                                    anchors.bottom: parent.bottom
                                    width: parent.width
                                    horizontalAlignment: Text.AlignHCenter
                                    font.pixelSize: Theme.fontSizeTiny
                                    color: Theme.secondaryColor
                                    text: modelData.label
                                }
                            }
                        }
                    }
                }

                // --- Records
                SectionHeader { text: qsTr("Records") }
                DetailItem {
                    visible: (page.stats.bestDaySeconds || 0) > 0
                    label: qsTr("Longest listening day")
                    value: page.dur(page.stats.bestDaySeconds || 0) + " · " + (page.stats.bestDay || "")
                }
                // (314) The most listened podcast (Thomas: "most listened
                // episode" was in fact the longest one heard); tap opens it
                BackgroundItem {
                    id: topPodcast
                    readonly property var topRank: (page.stats.ranking || [])[0] || null
                    visible: topRank !== null && topRank.seconds > 0
                    width: parent.width
                    height: topItem.height
                    enabled: visible && topRank.exists === true
                    onClicked: pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: topRank.podcastId })
                    DetailItem {
                        id: topItem
                        label: qsTr("Most listened podcast")
                        // (316) Children of a BackgroundItem sit in its contentItem:
                        // "parent.topRank" was undefined, the value stayed empty
                        value: topPodcast.topRank ? topPodcast.topRank.title + " · " + page.dur(topPodcast.topRank.seconds) : ""
                    }
                }
                DetailItem {
                    visible: (page.stats.favouriteWeekday || "").length > 0
                    label: qsTr("Favourite weekday")
                    value: page.stats.favouriteWeekday || ""
                }

                // --- Folders
                SectionHeader {
                    visible: (page.stats.folders || []).length > 1
                    text: qsTr("Folders")
                }
                Repeater {
                    model: (page.stats.folders || []).length > 1 ? page.stats.folders : []
                    DetailItem {
                        label: modelData.label
                        value: page.dur(modelData.seconds)
                    }
                }

                // --- Categories (main category only, or shared equally)
                SectionHeader { text: qsTr("Categories") }
                Flow {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingSmall
                    Chip { text: qsTr("Main category"); on: page.categoryMode === "main"; onClicked: page.chooseCategories("main") }
                    Chip { text: qsTr("All categories"); on: page.categoryMode === "split"; onClicked: page.chooseCategories("split") }
                }
                // (304) No topPadding: Silica's Label does not offer it (Qt 5.6 revision) - the page did not load
                Item { width: 1; height: Theme.paddingSmall }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: page.categoryMode === "split"
                          ? qsTr("The time of a podcast is shared equally between its categories.")
                          : qsTr("Each podcast counts only for its first category.")
                }
                Item { width: 1; height: Theme.paddingSmall }
                Repeater {
                    readonly property var list: (page.categoryMode === "split" ? page.stats.categoriesAll : page.stats.categoriesMain) || []
                    model: list
                    Item {
                        width: column.width
                        height: Theme.itemSizeSmall
                        // (309) Not "top": that is a final property of every Item (the page did not load)
                        readonly property real topSeconds: ((page.categoryMode === "split" ? page.stats.categoriesAll : page.stats.categoriesMain) || [{ seconds: 1 }])[0].seconds
                        readonly property real total: page.stats.seconds || 1
                        Label {
                            id: catName
                            x: Theme.horizontalPageMargin
                            width: parent.width - 2 * Theme.horizontalPageMargin - catTime.width - Theme.paddingMedium
                            text: modelData.label
                            truncationMode: TruncationMode.Fade
                        }
                        Label {
                            id: catTime
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.horizontalPageMargin
                            color: Theme.secondaryColor
                            text: page.dur(modelData.seconds) + " · " + Math.round(100 * modelData.seconds / parent.total) + " %"
                        }
                        Rectangle {
                            x: Theme.horizontalPageMargin
                            anchors.top: catName.bottom
                            anchors.topMargin: Theme.paddingSmall
                            width: (parent.width - 2 * Theme.horizontalPageMargin) * modelData.seconds / parent.topSeconds
                            height: Theme.paddingSmall / 2
                            radius: height / 2
                            color: Theme.highlightColor
                        }
                    }
                }

                // --- Podcasts
                SectionHeader { text: qsTr("Podcasts") }
                Repeater {
                    model: page.stats.ranking || []
                    ListItem {
                        id: row
                        width: column.width
                        contentHeight: Theme.itemSizeLarge
                        readonly property real topSeconds: ((page.stats.ranking || [{ seconds: 1 }])[0]).seconds
                        enabled: true
                        onClicked: if (modelData.exists) pageStack.push(Qt.resolvedUrl("EpisodesPage.qml"), { podcastId: modelData.podcastId })
                        menu: Component {
                            ContextMenu {
                                MenuItem {
                                    text: qsTr("Delete listening history of this podcast")
                                    onClicked: appWindow.remorseResetListeningOf(modelData.podcastId)
                                }
                            }
                        }
                        CoverImage {
                            id: cover
                            x: Theme.horizontalPageMargin
                            anchors.verticalCenter: parent.verticalCenter
                            width: Theme.itemSizeMedium
                            height: width
                            source: modelData.coverUrl
                            fallbackText: modelData.title
                        }
                        Label {
                            id: rankTitle
                            anchors.left: cover.right
                            anchors.leftMargin: Theme.paddingMedium
                            anchors.right: rankTime.left
                            anchors.rightMargin: Theme.paddingMedium
                            y: Theme.paddingMedium + Theme.paddingSmall
                            text: modelData.title
                            truncationMode: TruncationMode.Fade
                            color: row.highlighted ? Theme.highlightColor : Theme.primaryColor
                        }
                        Label {
                            id: rankTime
                            anchors.right: parent.right
                            anchors.rightMargin: Theme.horizontalPageMargin
                            y: rankTitle.y
                            color: Theme.secondaryColor
                            text: page.dur(modelData.seconds)
                        }
                        Rectangle {
                            anchors.left: rankTitle.left
                            anchors.top: rankTitle.bottom
                            anchors.topMargin: Theme.paddingSmall
                            width: (row.width - rankTitle.x - Theme.horizontalPageMargin) * modelData.seconds / row.topSeconds
                            height: Theme.paddingSmall / 2
                            radius: height / 2
                            color: Theme.highlightColor
                        }
                    }
                }

                Item { width: 1; height: Theme.paddingLarge }
                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    horizontalAlignment: Text.AlignHCenter
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryColor
                    text: {
                        var first = page.stats.firstDay || 0
                        var d = new Date(Math.floor(first / 10000), Math.floor(first / 100) % 100 - 1, first % 100)
                        var t = qsTr("Recorded since %1").arg(Qt.formatDate(d, qsTr("d MMM yyyy")))
                        return (page.stats.noTime || 0) > 0
                               ? t + " · " + qsTr("plus %n episodes marked as played without a time", "", page.stats.noTime)
                               : t
                    }
                }
            }
        }
        VerticalScrollDecorator {}
    }
}

// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../TimeFormat.js" as TimeFormat

// (334) Podcasts hidden from the listening statistics (Thomas 15 Nov:
// instead of deleting their history, e.g. for screenshots). Their time stays
// in the log; switched on here it counts again everywhere (statistics page,
// listening line on the Podcasts page, shared web page). Also for podcasts
// no longer subscribed (they have no settings page any more). The list is
// read once: a podcast switched back on stays in it until the page is left,
// so a mistaken tap can be undone at once.
Page {
    // (338) No player bar here (choice/settings-like page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "statsHiddenPage"

    property var podcasts: core.statsHiddenPodcasts()

    SilicaListView {
        id: list
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true

        header: Column {
            width: list.width
            PageHeader { title: qsTr("Hidden from statistics") }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Their listening time is kept but not counted. Switch a podcast on to count it again.")
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        model: page.podcasts

        delegate: TextSwitch {
            width: list.width
            text: modelData.title
            description: [modelData.seconds > 0 ? qsTr("%1 listened").arg(TimeFormat.length(modelData.seconds, qsTr("%1 h %2 min"), qsTr("%1 min"))) : "",
                          modelData.subscribed ? "" : qsTr("not subscribed")].filter(function(p) { return p !== "" }).join(" · ")
            checked: !core.hiddenFromStats(modelData.podcastId)
            automaticCheck: false
            onClicked: {
                checked = !checked
                core.setHiddenFromStats(modelData.podcastId, !checked)
            }
        }

        ViewPlaceholder {
            enabled: page.podcasts.length === 0
            text: qsTr("No podcast is hidden")
        }

        VerticalScrollDecorator {}
    }
}

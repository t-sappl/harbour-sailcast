// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// Languages of "Für dich" and "Im Trend" (140, a list instead of the menu of
// 139, which did not scroll): "Aus meinen Abos" (every language of at least
// 20 % of the subscriptions) or a choice of one or more.
Page {
    id: page

    property var info: core.interests()
    readonly property bool automatic: info.languagesChosen !== true

    function toggle(code) {
        var list = (info.languages || []).slice(0)
        var i = list.indexOf(code)
        if (i >= 0) {
            if (list.length === 1) {
                return      // at least one language
            }
            list.splice(i, 1)
        } else {
            list.push(code)
        }
        core.setDiscoverLanguages(list)
    }

    Connections {
        target: core
        onInterestsChanged: page.info = core.interests()
    }

    SilicaListView {
        anchors.fill: parent
        anchors.bottomMargin: appWindow.playerBarHeight
        anchors.rightMargin: appWindow.playerPanelWidth
        clip: true
        currentIndex: -1
        model: page.info.available || []

        header: Column {
            width: parent ? parent.width : 0
            PageHeader {
                title: qsTr("Languages")
            }
            TextSwitch {
                text: qsTr("From my subscriptions")
                description: qsTr("Every language of at least a fifth of your podcasts.")
                checked: page.automatic
                automaticCheck: false
                onClicked: core.setDiscoverLanguages(page.automatic ? (page.info.languages || []) : [])
            }
        }

        delegate: TextSwitch {
            text: modelData.name
            checked: (page.info.languages || []).indexOf(modelData.code) >= 0
            automaticCheck: false
            onClicked: page.toggle(modelData.code)
        }

        VerticalScrollDecorator {}
    }
}

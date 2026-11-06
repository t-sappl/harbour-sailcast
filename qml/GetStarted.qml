// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (219) First steps without podcasts (start page and Podcasts page): instead
// of a hint pointing to the pulley, the three ways to get podcasts as
// buttons - Discover, the sync (Nextcloud / gpodder.net, e.g. the
// subscriptions of AntennaPod) and an OPML import. Placed over the empty
// list, centered; the pulley still works around it. Since 228 a line where
// the three are found later.
Column {
    id: root
    width: parent.width
    spacing: Theme.paddingLarge

    readonly property real buttonWidth: Math.min(width - 2 * Theme.horizontalPageMargin, Theme.itemSizeHuge * 2.4)

    Label {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraLarge
        font.family: Theme.fontFamilyHeading
        color: Theme.highlightColor
        text: qsTr("No podcasts yet")
    }

    Label {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        color: Theme.secondaryHighlightColor
        text: qsTr("Find new podcasts, bring your subscriptions from another app, or import a list")
    }

    Item { width: 1; height: Theme.paddingSmall }

    ActionPill {
        kind: "primary"
        x: (parent.width - width) / 2
        text: qsTr("Discover podcasts")
        onClicked: pageStack.push(Qt.resolvedUrl("pages/SearchPage.qml"))
    }

    Column {
        width: parent.width
        spacing: Theme.paddingSmall

        ActionPill {
            kind: "secondary"
            x: (parent.width - width) / 2
            enabled: !core.syncing
            text: !core.syncConfigured ? qsTr("Set up sync")
                                       : (core.syncing ? qsTr("Synchronizing …") : qsTr("Synchronize now"))
            onClicked: {
                if (core.syncConfigured) {
                    appWindow.updateFromPulley()
                } else {
                    pageStack.push(Qt.resolvedUrl("pages/SettingsSyncPage.qml"))
                }
            }
        }

        Label {
            visible: !core.syncConfigured
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryHighlightColor
            text: qsTr("Nextcloud (GPodder Sync) or gpodder.net – subscriptions and listening state from AntennaPod and other apps")
        }
    }

    ActionPill {
        kind: "secondary"
        x: (parent.width - width) / 2
        text: qsTr("Import OPML or gPodder")
        onClicked: pageStack.push(Qt.resolvedUrl("pages/ImportExportPage.qml"))
    }

    // (253, usability audit #3/#26) "OPML" and "gPodder" are insider words;
    // one line under the button says what it actually does
    Label {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryHighlightColor
        text: qsTr("Bring your subscription list (a file) from another podcast app")
    }

    // (228) The buttons go with the first podcast - where to find all
    // three later
    Label {
        x: Theme.horizontalPageMargin
        width: parent.width - 2 * Theme.horizontalPageMargin
        horizontalAlignment: Text.AlignHCenter
        wrapMode: Text.Wrap
        font.pixelSize: Theme.fontSizeExtraSmall
        color: Theme.secondaryColor
        // (231) Discover is a ring page by default (229)
        text: appWindow.discoverRingOn
              ? qsTr("Later: Discover is one of the swipe pages; sync and import: pull down → Settings")
              : qsTr("Later: Podcasts page, pull down → Discover; sync and import in the settings (pull down → Settings)")
    }
}

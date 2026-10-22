// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// About Sailcast (40): icon, version, what the app is, license, source
// code, privacy and credits. Links open in the browser.
Page {
    id: page
    objectName: "aboutPage"

    readonly property string sourceUrl: "https://github.com/t-sappl/harbour-sailcast"
    readonly property string privacyUrl: "https://github.com/t-sappl/harbour-sailcast/blob/main/PRIVACY.md"

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge * 2

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader { title: qsTr("About Sailcast") }

            Image {
                anchors.horizontalCenter: parent.horizontalCenter
                source: "/usr/share/icons/hicolor/172x172/apps/harbour-sailcast.png"
                width: Theme.iconSizeExtraLarge
                height: width
                sourceSize.width: width
                sourceSize.height: height

                // Double tap: show all first-use hints again (52)
                MouseArea {
                    anchors.fill: parent
                    onDoubleClicked: appWindow.resetHints()
                }
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: "Sailcast"
                font.pixelSize: Theme.fontSizeExtraLarge
                color: Theme.highlightColor
            }

            Label {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Version %1").arg(core.version)
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                font.pixelSize: Theme.fontSizeSmall
                text: qsTr("Podcast app for Sailfish OS: subscriptions, playlist, chapters, downloads and archive, synchronized with other podcast apps through Nextcloud (GPodder Sync).")
            }

            SectionHeader { text: qsTr("License and source code") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("© 2026 Thomas Sappl. Free software under the GNU General Public License, version 3 or later.")
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Source code")
                onClicked: Qt.openUrlExternally(page.sourceUrl)
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                text: qsTr("Privacy")
                onClicked: Qt.openUrlExternally(page.privacyUrl)
            }

            SectionHeader { text: qsTr("Thanks") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryHighlightColor
                text: qsTr("Podcast search: Podcast Index (podcastindex.org). Behavior and formats of gPodder (Thomas Perl and contributors, ISC license) served as reference. Lock screen controls: Amber.Mpris.")
            }
        }

        VerticalScrollDecorator {}
    }
}

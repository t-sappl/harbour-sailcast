// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// (366, forum 19 Nov) Unsubscribing a podcast that has downloads: they
// would be deleted without a word. This page says so and offers to keep
// them first ("Save in Music" of all its downloads: moved to the archive
// folder, or - when already there - only marked; they stay after
// unsubscribing). Back = nothing happens. The page is the confirmation - no
// countdown after it (appWindow.askUnsubscribe).
Page {
    id: page
    objectName: "unsubscribeDialog"
    readonly property bool hidesPlayerBar: true

    property string podcastTitle: ""
    property int count: 0
    property real bytes: 0
    // (368, Thomas 19 Nov) With "Downloads in Music" = archive folder most
    // files are already where they would be kept: no "move"
    property int inPlace: 0
    property string keepFolder: ""

    // "delete" or "keep"; the app window acts once the page is gone
    signal chosen(string choice)

    SilicaFlickable {
        anchors.fill: parent
        contentHeight: column.height + Theme.paddingLarge

        Column {
            id: column
            width: parent.width
            spacing: Theme.paddingLarge

            PageHeader {
                title: qsTr("Unsubscribe?")
                description: page.podcastTitle
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                color: Theme.highlightColor
                text: qsTr("%n downloaded episodes (%1) will be deleted.", "", page.count)
                      .arg(TimeFormat.size(page.bytes, qsTr("%1 MB"), qsTr("%1 GB")))
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.secondaryHighlightColor
                // (369, Thomas 19 Nov) Same words as the button in the podcast settings
                text: qsTr("Downloads saved in Music stay after unsubscribing and can be imported again later.")
            }

            ActionPill {
                x: Theme.horizontalPageMargin
                kind: "destructive"
                text: qsTr("Delete downloads and unsubscribe")
                onClicked: {
                    page.chosen("delete")
                    pageStack.pop()
                }
            }

            ActionPill {
                x: Theme.horizontalPageMargin
                kind: "secondary"
                text: qsTr("Save downloads in Music and unsubscribe")
                onClicked: {
                    page.chosen("keep")
                    pageStack.pop()
                }
            }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.WrapAtWordBoundaryOrAnywhere
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: page.inPlace >= page.count
                      ? qsTr("The files are already in %1 and stay there.").arg(page.keepFolder)
                      : qsTr("The files are moved to %1.").arg(page.keepFolder)
            }
        }
        VerticalScrollDecorator {}
    }
}

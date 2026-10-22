// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../TimeFormat.js" as TimeFormat

// Confirms "Archive podcast": all episodes, the feed, the cover and
// episodes.json into one folder (see DESIGN.md "Archive and cloud").
// The size comes from the feed and is often incomplete.
Dialog {
    id: dialog
    objectName: "archiveDialog"
    property var podcast: ({})

    readonly property string folder: (podcast.archiveFolder || "").length > 0
                                     ? podcast.archiveFolder
                                     : core.archiveDirectory() + "/" + (podcast.title || "")

    Column {
        width: parent.width
        spacing: Theme.paddingMedium

        DialogHeader {
            acceptText: qsTr("Start")
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.highlightColor
            font.pixelSize: Theme.fontSizeLarge
            text: dialog.podcast.title || ""
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            color: Theme.secondaryHighlightColor
            text: {
                var text = qsTr("%n episodes", "", dialog.podcast.mediaCount || 0)
                if ((dialog.podcast.declaredBytes || 0) > 0) {
                    text += " · " + qsTr("about %1").arg(TimeFormat.size(dialog.podcast.declaredBytes,
                                                                           qsTr("%1 MB"), qsTr("%1 GB")))
                }
                return text
            }
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.WrapAnywhere
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.secondaryHighlightColor
            text: dialog.folder
        }

        Label {
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeExtraSmall
            color: Theme.secondaryColor
            text: qsTr("Downloads all episodes, the feed, the cover and the listening state into this folder. Episodes already downloaded are moved there, files already in the folder are kept. Can be cancelled and continued later.")
        }

        Label {
            visible: !core.downloadAllowed()
            x: Theme.horizontalPageMargin
            width: parent.width - 2 * Theme.horizontalPageMargin
            wrapMode: Text.Wrap
            font.pixelSize: Theme.fontSizeSmall
            color: Theme.highlightColor
            text: qsTr("No Wi-Fi connection - the episodes would be loaded over the mobile connection.")
        }
    }
}

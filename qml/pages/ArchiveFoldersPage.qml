// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"

// (327, Gemini) The archive folders of Import and export on a page of their
// own: podcast folders in the archive folder (episodes.json) that can be
// taken over without network. One row per podcast pushed "Export" far down.
Page {
    id: page
    objectName: "archiveFoldersPage"

    property var archiveFolders: core.archiveFolders()

    SilicaListView {
        anchors.fill: parent
        model: page.archiveFolders

        header: Column {
            width: parent ? parent.width : page.width
            PageHeader {
                title: qsTr("Archive folders")
            }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Takes over the episodes, files and listening state of an archived podcast – without network.")
            }
            Item { width: 1; height: Theme.paddingMedium }
        }

        delegate: ImportExportItem {
            title: modelData.title
            description: qsTr("%n episodes", "", modelData.episodes)
                         + (modelData.subscribed ? " · " + qsTr("subscribed") : "")
            onClicked: {
                core.importArchiveFolder(modelData.path)
                page.archiveFolders = core.archiveFolders()
            }
        }

        ViewPlaceholder {
            enabled: page.archiveFolders.length === 0
            text: qsTr("No archived podcasts in %1").arg(core.archiveDirectory())
        }

        VerticalScrollDecorator {}
    }
}

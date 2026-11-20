// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// About Sailcast (40): icon, version, what the app is, license, source
// code, privacy and credits. Links open in the browser.
// (350) Updates of the GitHub build: status, Check now / Download /
// Install now, the release text and the switch "Check for updates" -
// not in the Jolla Store build (core.appUpdateSupported).
Page {
    id: page
    objectName: "aboutPage"

    readonly property string sourceUrl: "https://github.com/t-sappl/harbour-sailcast"
    readonly property string privacyUrl: "https://github.com/t-sappl/harbour-sailcast/blob/main/PRIVACY.md"

    readonly property string updState: core.appUpdateState
    readonly property bool newVersion: updState === "available" || updState === "downloading" || updState === "ready"
    property bool notesOpen: newVersion
    // (347) Read once when the page opens
    property var history: []
    Component.onCompleted: history = core.changelog()
    // (358) The running version and the older ones apart
    readonly property var currentHistory: history.filter(function(v) { return v.current === true })
    readonly property var earlierHistory: history.filter(function(v) { return v.current !== true })
    property bool earlierOpen: false
    // (360) The running version's notes were read ("… Show all")
    readonly property bool notesSeen: currentHistory.length > 0
                                      && core.setting("about.notesSeen", "") === currentHistory[0].version

    function mb(bytes) {
        return Number(bytes / 1048576).toLocaleString(Qt.locale(), "f", 1)
    }

    function statusText() {
        if (updState === "downloading") {
            return qsTr("Downloading Sailcast %1 · %2 of %3 MB").arg(core.appUpdateVersion)
                       .arg(mb(core.appUpdateReceived)).arg(mb(core.appUpdateSize))
        }
        if (updState === "ready") {
            return qsTr("Sailcast %1 is ready to install").arg(core.appUpdateVersion)
        }
        if (updState === "available") {
            return core.appUpdateSize > 0
                   ? qsTr("Sailcast %1 is available · %2 MB").arg(core.appUpdateVersion).arg(mb(core.appUpdateSize))
                   : qsTr("Sailcast %1 is available").arg(core.appUpdateVersion)
        }
        if (core.appUpdateChecking) {
            return qsTr("Checking for updates …")
        }
        if (updState === "uptodate") {
            return qsTr("Up to date · checked %1").arg(dateText(core.appUpdateChecked * 1000))
        }
        return core.appUpdateEnabled ? qsTr("Not checked yet") : qsTr("Automatic check is off")
    }

    // (351) As the sync and backup lines of the settings (350): "today 19:26"
    function dateText(ms) {
        var now = new Date()
        var startOfWeek = new Date(now.getFullYear(), now.getMonth(), now.getDate() - 6).getTime()
        var day = TimeFormat.day(ms, qsTr("today"), qsTr("yesterday"))
        return ms >= startOfWeek ? day + " " + Qt.formatTime(new Date(ms), Qt.DefaultLocaleShortDate) : day
    }

    function errorText() {
        var kind = core.appUpdateErrorKind
        return kind === "check" ? qsTr("Update check failed – no connection, or GitHub is not reachable")
             : kind === "download" ? qsTr("Download failed – try again")
             : kind === "space" ? qsTr("Not enough free space for the download")
             : kind === "install" ? qsTr("The installer could not be opened")
             : ""
    }

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
                text: qsTr("Podcast app for Sailfish OS: discover new podcasts – suggestions for you, trending and new shows, categories and similar podcasts – and listen before subscribing. Playlist, chapters, bookmarks, downloads and archive, synchronized with other podcast apps through Nextcloud (GPodder Sync) or gpodder.net.")
            }

            // (350) Updates (GitHub build only)
            Column {
                width: parent.width
                visible: core.appUpdateSupported
                spacing: Theme.paddingMedium

                SectionHeader { text: qsTr("Updates") }

                Label {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeSmall
                    // (351) Thomas: blue on dark was hard to read
                    color: page.newVersion ? Theme.highlightColor : Theme.secondaryColor
                    text: page.statusText()
                }

                // Download progress
                Rectangle {
                    visible: page.updState === "downloading"
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    height: Theme.paddingSmall
                    radius: height / 2
                    color: Theme.rgba(Theme.highlightColor, 0.2)
                    Rectangle {
                        height: parent.height
                        radius: parent.radius
                        width: core.appUpdateSize > 0
                               ? parent.width * Math.min(1, core.appUpdateReceived / core.appUpdateSize) : 0
                        color: Theme.highlightColor
                    }
                }

                Label {
                    visible: text !== ""
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: page.errorText()
                }

                Flow {
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    spacing: Theme.paddingMedium
                    ActionPill {
                        visible: page.updState === "available"
                        kind: "primary"
                        text: qsTr("Download")
                        onClicked: core.downloadAppUpdate()
                    }
                    ActionPill {
                        visible: page.updState === "downloading"
                        kind: "secondary"
                        text: qsTr("Cancel")
                        onClicked: core.cancelAppUpdate()
                    }
                    ActionPill {
                        visible: page.updState === "ready"
                        kind: "primary"
                        text: qsTr("Install now")
                        onClicked: core.installAppUpdate()
                    }
                    ActionPill {
                        visible: !page.newVersion
                        enabled: !core.appUpdateChecking
                        kind: "secondary"
                        text: qsTr("Check now")
                        onClicked: core.checkAppUpdate(true)
                    }
                }

                Label {
                    visible: page.updState === "ready"
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: qsTr("Sailcast closes and the installer opens – start Sailcast again afterwards. Your podcasts, downloads and listening state stay on this phone. Needs “Allow untrusted software” in the Sailfish OS settings.")
                }

                // The release text of the new version, from GitHub (347: the
                // running and older versions come from the CHANGELOG below)
                FoldHeader {
                    visible: page.newVersion && core.appUpdateNotes !== ""
                    title: qsTr("What's new in %1").arg(core.appUpdateVersion)
                    subtitle: qsTr("new")
                    open: page.notesOpen
                    onClicked: page.notesOpen = !page.notesOpen
                }
                Label {
                    visible: page.newVersion && core.appUpdateNotes !== "" && page.notesOpen
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    wrapMode: Text.Wrap
                    textFormat: Text.PlainText
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.secondaryHighlightColor
                    text: core.appUpdateNotes
                }

                TextSwitch {
                    text: qsTr("Check for updates")
                    description: qsTr("Once a day when Sailcast starts, at GitHub (which sees your IP address). Install by hand: the packages are on the GitHub release page.")
                    checked: core.appUpdateEnabled
                    automaticCheck: false
                    onClicked: core.setAppUpdateEnabled(!core.appUpdateEnabled)
                }
            }

            // (347) Changes by version from the installed CHANGELOG.md
            // (also in the Jolla Store build); the running version open
            SectionHeader {
                visible: page.history.length > 0
                text: qsTr("Version history")
            }
            // (review 347) Own column: no page spacing between the versions
            Column {
                width: parent.width
            // (358, Thomas 18 Nov: all versions since 1.0 was too much) The
            // running version as before, the older ones behind one fold
            Component {
                id: versionDelegate
                Column {
                    id: versionColumn
                    width: page.width
                    // (360, Thomas 18 Nov) Open only until read: "… Show all"
                    // marks the running version as read (about.notesSeen),
                    // afterwards it starts folded like the older ones
                    property bool open: modelData.current === true && !page.notesSeen
                    // (357, Thomas 18 Nov) The running version opens with its
                    // first lines only; "… Show all" for the rest
                    property bool full: modelData.current !== true
                    readonly property var allBlocks: modelData.blocks || []
                    readonly property int shortCount: {
                        var lines = 0
                        for (var i = 0; i < allBlocks.length; ++i) {
                            if (allBlocks[i].kind !== "blank") {
                                lines += 1
                                if (lines === 4) {
                                    return i + 1
                                }
                            }
                        }
                        return allBlocks.length
                    }
                    readonly property bool shortened: !full && shortCount < allBlocks.length
                    FoldHeader {
                        title: "Sailcast " + modelData.version
                        subtitle: modelData.current ? (modelData.dateText !== "" ? qsTr("%1 · your version").arg(modelData.dateText)
                                                                                 : qsTr("your version"))
                                                    : modelData.dateText
                        open: parent.open
                        onClicked: parent.open = !parent.open
                    }
                    // (351, Thomas: only pale blue, hard to read) Laid out
                    // line by line: headings in the highlight colour, text
                    // in the primary colour, items with a hanging indent.
                    // Built only when open.
                    Column {
                        visible: parent.open
                        x: Theme.horizontalPageMargin
                        width: parent.width - 2 * Theme.horizontalPageMargin
                        Repeater {
                            model: parent.visible ? (versionColumn.shortened ? versionColumn.allBlocks.slice(0, versionColumn.shortCount)
                                                                             : versionColumn.allBlocks) : []
                            Item {
                                readonly property string kind: modelData.kind
                                readonly property bool isItem: kind === "item" || kind === "subitem"
                                readonly property real indent: kind === "subitem" ? 2 * Theme.paddingLarge : 0
                                width: parent.width
                                height: kind === "blank" ? Theme.paddingMedium
                                                         : blockText.height + (kind === "heading" ? Theme.paddingSmall : Theme.paddingSmall / 2)
                                Label {
                                    id: bulletMark
                                    visible: parent.isItem
                                    x: parent.indent
                                    text: parent.kind === "subitem" ? "\u25e6" : "\u2022"
                                    font.pixelSize: Theme.fontSizeSmall
                                    color: Theme.highlightColor
                                }
                                Label {
                                    id: blockText
                                    visible: parent.kind !== "blank"
                                    x: parent.isItem ? parent.indent + Theme.paddingLarge : 0
                                    width: parent.width - x
                                    wrapMode: Text.Wrap
                                    textFormat: Text.PlainText
                                    font.pixelSize: parent.kind === "heading" ? Theme.fontSizeMedium : Theme.fontSizeSmall
                                    color: parent.kind === "heading" ? Theme.highlightColor : Theme.primaryColor
                                    text: parent.kind === "blank" ? "" : modelData.text
                                }
                            }
                        }
                    }
                    // (357) "… Show all" / "Show less" for the running version
                    BackgroundItem {
                        visible: parent.open && modelData.current === true && versionColumn.shortCount < versionColumn.allBlocks.length
                        width: parent.width
                        height: Theme.itemSizeExtraSmall
                        onClicked: {
                            versionColumn.full = !versionColumn.full
                            if (versionColumn.full) {
                                core.setSetting("about.notesSeen", modelData.version)
                            }
                        }
                        Label {
                            x: Theme.horizontalPageMargin
                            anchors.verticalCenter: parent.verticalCenter
                            font.pixelSize: Theme.fontSizeSmall
                            color: Theme.highlightColor
                            text: versionColumn.full ? qsTr("Show less") : "\u2026 " + qsTr("Show all")
                        }
                    }
                    // No padding on a Silica Label (304): a spacer
                    Item { visible: parent.open; width: 1; height: Theme.paddingMedium }
                }
            }
            Repeater {
                id: historyRepeater
                model: page.currentHistory
                delegate: versionDelegate
            }
            FoldHeader {
                visible: page.earlierHistory.length > 0
                title: qsTr("Earlier versions (%1)").arg(page.earlierHistory.length)
                open: page.earlierOpen
                onClicked: page.earlierOpen = !page.earlierOpen
            }
            Repeater {
                model: page.earlierOpen ? page.earlierHistory : []
                delegate: versionDelegate
            }
            }

            // (351) The app's log for problem reports - no terminal needed
            SectionHeader { text: qsTr("Report a problem") }

            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("When something goes wrong, the log shows what the player and the updates did. It stays on this phone and is never sent by the app – only you can share it, after reading it.")
            }

            // (372, Thomas 20 Nov) On unless switched off; off deletes it at
            // once and nothing is written any more (review 372: no countdown -
            // a RemorsePopup dies with the page if the user leaves it)
            TextSwitch {
                id: logSwitch
                automaticCheck: false
                checked: core.logEnabled
                text: qsTr("Keep a log")
                description: qsTr("Off: the log is deleted and nothing is written. Copies you saved or shared are not affected.")
                onClicked: {
                    core.logEnabled = !core.logEnabled
                    if (!core.logEnabled) {
                        appWindow.showMessage(qsTr("Log deleted"))
                    }
                }
            }

            // (354) One page: read, edit, share, save
            ActionPill {
                visible: core.logEnabled
                kind: "secondary"
                x: Theme.horizontalPageMargin
                text: qsTr("Show log")
                onClicked: pageStack.push(Qt.resolvedUrl("LogPage.qml"))
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

            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
                text: qsTr("Source code")
                onClicked: Qt.openUrlExternally(page.sourceUrl)
            }

            ActionPill {
                kind: "secondary"
                x: Theme.horizontalPageMargin
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
                text: qsTr("Search and Discover: Podcast Index (podcastindex.org) and fyyd (fyyd.de); new podcasts from Hörsuppe's list \"Nullnummern\" on fyyd and Podnews' \"New Podcasts\". Behavior and formats of gPodder (Thomas Perl and contributors, ISC license) served as reference. Lock screen controls: Amber.Mpris.")
            }
        }

        VerticalScrollDecorator {}
    }
}

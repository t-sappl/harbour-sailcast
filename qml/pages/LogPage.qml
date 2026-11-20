// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0
import "../"
import "../TimeFormat.js" as TimeFormat

// (354, Thomas 18 Nov) The app's log before it is shared: exactly the text
// that goes out (header with the player settings + the last lines of the
// log), editable - a line the user does not want to send can be deleted.
// Edits change only what is shared or saved here, never the log itself
// ("Reset" loads it again). Where to send it: forum thread, GitHub issue,
// sailcast@proton.me.
Page {
    // No player bar here (tool page, PlayerBar.isHiddenPage)
    readonly property bool hidesPlayerBar: true
    id: page
    objectName: "logPage"

    readonly property string forumUrl: "https://forum.sailfishos.org/t/sailcast-podcast-client-with-podcast-index-integration/35669"
    readonly property string mailAddress: "sailcast@proton.me"
    readonly property string issueUrl: "https://github.com/t-sappl/harbour-sailcast/issues/new?title="
        + encodeURIComponent("Problem: ")
        + "&body=" + encodeURIComponent("What happened, and what did you do before?\n\n\n"
                                        + "Sailcast " + core.version + "\n\n"
                                        + "Log: attach the file from Downloads (sailcast-log-…) or paste it here.\n")

    // (355) Since when the shown part goes back: the first dated line
    property string since: ""
    function load() {
        var text = core.logText()
        var m = /\n(\d{4})-(\d\d)-(\d\d) (\d\d):(\d\d)/.exec(text)
        if (m) {
            var d = new Date(parseInt(m[1], 10), parseInt(m[2], 10) - 1, parseInt(m[3], 10),
                             parseInt(m[4], 10), parseInt(m[5], 10))
            since = TimeFormat.day(d.getTime(), qsTr("today"), qsTr("yesterday")) + " "
                    + Qt.formatTime(d, Qt.DefaultLocaleShortDate)
        } else {
            since = ""
        }
        logArea.text = text
        replaced = false
        replacedInfo = ""
    }

    // (371, Thomas 20 Nov) "Replace names": the shared log tells what
    // happened, not what someone listens to. Works on the text as shown
    // (edits stay), "Reset" brings the original back; nothing is stored.
    property bool replaced: false
    property bool replacing: false
    property string replacedInfo: ""
    function replaceNames() {
        replacing = true
        // (review 371) the busy line is at the top
        flick.scrollToTop()
        // a moment for the busy line to show (24 000 episode names)
        replaceTimer.start()
    }
    Timer {
        id: replaceTimer
        interval: 50
        onTriggered: {
            var r = core.anonymizedLog(logArea.text)
            logArea.text = r.text
            page.replaced = true
            page.replacing = false
            page.replacedInfo = (r.podcasts + r.episodes) > 0
                    ? qsTr("Names replaced: %1, %2. Web addresses show only the domain. \"Reset\" shows the original.")
                      .arg(qsTr("%n podcast(s)", "", r.podcasts)).arg(qsTr("%n episode(s)", "", r.episodes))
                    : qsTr("No podcast or episode names found. Web addresses show only the domain. \"Reset\" shows the original.")
            flick.scrollToTop()
        }
    }
    Component.onCompleted: load()

    function save() {
        var path = core.saveLogText(logArea.text)
        if (path === "") {
            appWindow.showMessage(qsTr("The log could not be saved"))
        }
        return path
    }

    RemorsePopup { id: resetRemorse }

    // (357, Thomas 18 Nov) Opens at the top: hint and header first, and the
    // pull-down menu (Share, Save) can only be pulled there

    SilicaFlickable {
        id: flick
        anchors.fill: parent
        anchors.rightMargin: appWindow.playerPanelWidth
        contentHeight: column.height + Theme.paddingLarge

        // The main action next to the finger: Share at the bottom
        PullDownMenu {
            MenuItem {
                text: qsTr("Reset")
                // (review 354) Edits are dropped only after a countdown
                onClicked: resetRemorse.execute(qsTr("Resetting"), function() { page.load() })
            }
            MenuItem {
                text: qsTr("Replace names")
                visible: !page.replaced
                enabled: !page.replacing
                onClicked: page.replaceNames()
            }
            MenuItem {
                text: qsTr("Copy to clipboard")
                onClicked: {
                    Clipboard.text = logArea.text
                    appWindow.showMessage(qsTr("Log copied"))
                }
            }
            MenuItem {
                text: qsTr("Save to Downloads")
                onClicked: {
                    var path = page.save()
                    if (path !== "") {
                        appWindow.showMessage(qsTr("Saved to Downloads: %1").arg(path.substring(path.lastIndexOf("/") + 1)))
                    }
                }
            }
            MenuItem {
                text: qsTr("Share")
                onClicked: {
                    var path = page.save()
                    if (path !== "") {
                        appWindow.shareLogFile(path)
                    }
                }
            }
        }

        Column {
            id: column
            width: parent.width

            PageHeader {
                title: qsTr("Log")
                description: qsTr("What the player did")
            }

            // (355, Thomas 18 Nov) What it is for, that it stays here
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Only for finding errors: Sailcast notes what the player and the updates did. It stays on this phone – nothing is sent unless you share it.")
                      + " " + qsTr("You can switch it off on the About page.")
                      + "\n" + qsTr("You can edit the text before you share or save it. Passwords and tokens in web addresses and e-mail addresses are masked as far as they can be recognised.")
                      + "\n" + qsTr("\"Replace names\" in the pull-down menu turns the names of your podcasts and episodes into numbers – the log still shows what happened, but not what you listen to.")
            }

            // (357, Thomas 18 Nov) E-mail first (tap copies), then the links;
            // (358) the heading left like the lines below (a right-aligned
            // section header looked lost over them); (359) more air between
            // the three blocks: hint / send / log
            Item { width: 1; height: 2 * Theme.paddingLarge }
            Label {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeSmall
                color: Theme.highlightColor
                text: qsTr("Send it to the developer")
            }

            BackgroundItem {
                width: parent.width
                height: mailColumn.height + 2 * Theme.paddingSmall
                onClicked: {
                    Clipboard.text = page.mailAddress
                    appWindow.showMessage(qsTr("Address copied: %1").arg(page.mailAddress))
                }
                Column {
                    id: mailColumn
                    x: Theme.horizontalPageMargin
                    width: parent.width - 2 * Theme.horizontalPageMargin
                    anchors.verticalCenter: parent.verticalCenter
                    Label {
                        width: parent.width
                        truncationMode: TruncationMode.Fade
                        font.pixelSize: Theme.fontSizeSmall
                        color: highlighted ? Theme.highlightColor : Theme.primaryColor
                        text: qsTr("E-mail: %1").arg(page.mailAddress)
                    }
                    Label {
                        width: parent.width
                        font.pixelSize: Theme.fontSizeExtraSmall
                        color: Theme.secondaryColor
                        text: qsTr("Tap to copy the address")
                    }
                }
            }

            Flow {
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                Label {
                    height: forumPill.height
                    verticalAlignment: Text.AlignVCenter
                    font.pixelSize: Theme.fontSizeSmall
                    color: Theme.secondaryColor
                    text: qsTr("or:")
                }
                ActionPill {
                    id: forumPill
                    kind: "secondary"
                    text: qsTr("Forum thread")
                    onClicked: Qt.openUrlExternally(page.forumUrl)
                }
                ActionPill {
                    kind: "secondary"
                    text: qsTr("GitHub issue")
                    onClicked: Qt.openUrlExternally(page.issueUrl)
                }
            }

            Item { width: 1; height: 2 * Theme.paddingLarge }

            // (359, Thomas 18 Nov: the page looked crowded) How far back as a
            // grey caption of the log itself - one blue heading only
            Label {
                visible: page.since !== ""
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.secondaryColor
                text: qsTr("Shown: the last part, since %1.").arg(page.since)
            }

            // (371) While names are replaced / what was replaced
            Row {
                visible: page.replacing
                x: Theme.horizontalPageMargin
                spacing: Theme.paddingMedium
                height: visible ? implicitHeight + Theme.paddingSmall : 0
                BusyIndicator {
                    size: BusyIndicatorSize.ExtraSmall
                    running: page.replacing
                    anchors.verticalCenter: parent.verticalCenter
                }
                Label {
                    font.pixelSize: Theme.fontSizeExtraSmall
                    color: Theme.highlightColor
                    text: qsTr("Replacing names …")
                    anchors.verticalCenter: parent.verticalCenter
                }
            }
            Label {
                visible: page.replaced && !page.replacing
                x: Theme.horizontalPageMargin
                width: parent.width - 2 * Theme.horizontalPageMargin
                wrapMode: Text.Wrap
                font.pixelSize: Theme.fontSizeExtraSmall
                color: Theme.highlightColor
                text: page.replacedInfo
            }
            Item { width: 1; height: page.replaced && !page.replacing ? Theme.paddingSmall : 0 }

            TextArea {
                id: logArea
                width: parent.width
                font.family: "monospace"
                // (357) Monospace looks larger than the same size in the
                // Sailfish font: a bit smaller, more of a line fits
                font.pixelSize: Math.round(Theme.fontSizeTiny * 0.8)
            }
        }

        VerticalScrollDecorator {}
    }
}

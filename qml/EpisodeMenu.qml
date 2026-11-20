// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// (348) The context menu of an episode - one for every list (EpisodeDelegate,
// search results, later the bookmarks page), so the items and their order
// stay the same everywhere. `ep` is anything with the episode's status:
// the row's `model` in a list, or core.episodeStatus(id) elsewhere.
ContextMenu {
    id: menu

    property var ep: ({})
    // "Set aside" (only on New episodes, view "New")
    property bool setAsideOffered: false
    property bool inPlaylist: false
    // Downloads page: file before status, selection items
    property bool selectable: false
    property bool showMarkOlder: false
    property bool showPodcastTitle: true
    readonly property bool isCurrent: ep !== undefined && ep !== null
                                      && appWindow.player.episodeId === ep.episodeId
    signal selectStart()
    signal selectPodcast()
    // (review 348) The episode is gone (e.g. removed by a sync while the
    // search results were shown): no actions on it
    readonly property bool known: ep !== undefined && ep !== null && ep.episodeId > 0
    MenuItem {
        visible: !menu.known
        enabled: false
        text: qsTr("This episode is no longer there")
    }

    // (304, Thomas 13 Nov: first item) (253, usability audit #35) Out of "New episodes" without
    // pretending it was heard: only the "new" flag goes
    MenuItem {
        visible: menu.known && (menu.setAsideOffered && ep.isNew === true && !ep.played)
        text: qsTr("Set aside")
        onClicked: core.removeFromNew(ep.episodeId)
    }
    // Playlist
    MenuItem {
        visible: menu.known && (!menu.inPlaylist && !ep.queued)
        text: qsTr("Play next")
        onClicked: core.queueAddPlaced(ep.episodeId, true)
    }
    MenuItem {
        visible: menu.known && (!menu.inPlaylist && !ep.queued)
        text: qsTr("Add to end of playlist")
        onClicked: core.queueAddPlaced(ep.episodeId, false)
    }
    // (334, forum 16 Nov) ➕ with the other download choice: steer
    // "playlist" and "download" apart without the setting. Where ➕
    // puts it (end or next), only for episodes not downloaded
    MenuItem {
        visible: menu.known && (!menu.inPlaylist && !ep.queued && ep.downloaded !== true && ep.downloading !== true)
        text: core.setting("playlist.download", "1") === "1" ? qsTr("Add to playlist without downloading")
                                                             : qsTr("Add to playlist and download")
        onClicked: core.queueAddDefaultOtherDownload(ep.episodeId)
    }
    MenuItem {
        visible: menu.known && (ep.queued)
        text: qsTr("Remove from playlist")
        onClicked: core.queueRemove(ep.episodeId)
    }
    // (277) Lets go of the pin of this episode alone
    MenuItem {
        visible: menu.known && (ep.queued && ep.pinned === true)
        text: qsTr("Release pin")
        onClicked: core.queueUnpin(ep.episodeId)
    }
    // (314) Status before the file (Gemini 14 Nov: with ➕ loading by
    // itself "Download" is rare, marking as played is the clean-up);
    // on the Downloads page (selectable) the file comes first, the
    // status follows below.
    // Status; started episodes offer both: finish them or start
    // over (22)
    MenuItem {
        visible: menu.known && (!menu.selectable && !ep.played)
        text: qsTr("Mark as played")
        onClicked: {
            if (menu.isCurrent) {
                appWindow.player.pause()
            }
            core.markPlayed(ep.episodeId)
        }
    }
    MenuItem {
        visible: menu.known && (!menu.selectable && (ep.played || ep.inProgress))
        text: qsTr("Mark as unplayed")
        onClicked: {
            if (menu.isCurrent) {
                appWindow.player.pause()
                appWindow.player.seek(0)
            }
            core.markUnplayed(ep.episodeId)
        }
    }
    // Catching up in the middle of a podcast (53); countdown in the
    // app window (not in this menu)
    // (320) Only where the list runs by publication date (podcast
    // page, New episodes): "older" means the feed's date, and it
    // reaches episodes the page may not show
    MenuItem {
        visible: menu.known && (menu.showMarkOlder)
        text: qsTr("Mark older ones as played")
        onClicked: appWindow.remorseMarkOlderPlayed(ep.episodeId)
    }
    // File
    MenuItem {
        visible: menu.known && (!ep.downloaded && !ep.downloading)
        text: qsTr("Download")
        onClicked: appWindow.requestDownload(ep.episodeId)
    }
    MenuItem {
        visible: menu.known && (ep.downloading)
        text: qsTr("Cancel download")
        onClicked: core.cancelDownload(ep.episodeId)
    }
    MenuItem {
        visible: menu.known && (ep.downloaded)
        text: qsTr("Delete download")
        // The countdown and its action live in the app window: a
        // function created in this menu loses its context when the
        // menu is destroyed - the countdown ran, the row went blank,
        // but nothing was deleted (25)
        onClicked: appWindow.remorseDeleteDownload(ep.episodeId)
    }
    // (313) Starts the selection mode with this row marked; (314)
    // named after what it is for. "… of this podcast": all its
    // downloads marked, to look over before "Delete"
    MenuItem {
        visible: menu.known && (menu.selectable && ep.downloaded)
        text: qsTr("Delete several …")
        onClicked: menu.selectStart()
    }
    MenuItem {
        visible: menu.known && (menu.selectable && ep.downloaded)
        text: qsTr("Delete all of this podcast …")
        onClicked: menu.selectPodcast()
    }
    // (314) Status on the Downloads page: after the file
    MenuItem {
        visible: menu.known && (menu.selectable && !ep.played)
        text: qsTr("Mark as played")
        onClicked: {
            if (menu.isCurrent) {
                appWindow.player.pause()
            }
            core.markPlayed(ep.episodeId)
        }
    }
    MenuItem {
        visible: menu.known && (menu.selectable && (ep.played || ep.inProgress))
        text: qsTr("Mark as unplayed")
        onClicked: {
            if (menu.isCurrent) {
                appWindow.player.pause()
                appWindow.player.seek(0)
            }
            core.markUnplayed(ep.episodeId)
        }
    }
    // Other. (156) Its podcast is not subscribed (147)
    MenuItem {
        visible: menu.known && (ep.provisional === true)
        text: qsTr("Subscribe to the podcast")
        onClicked: appWindow.subscribeWithFolder(core.podcast(ep.podcastId).url || "", ep.podcastTitle || "")
    }
    // (256) Where the podcast name is shown - not on its own page
    MenuItem {
        visible: menu.known && (menu.showPodcastTitle)
        text: qsTr("Show podcast")
        onClicked: pageStack.push(Qt.resolvedUrl("pages/EpisodesPage.qml"), { podcastId: ep.podcastId })
    }
    // (83, 259) Link, file or download: the share page
    MenuItem {
        visible: menu.known
        text: qsTr("Share...")
        onClicked: pageStack.push(Qt.resolvedUrl("pages/ShareChoicePage.qml"), { episodeId: ep.episodeId })
    }
}

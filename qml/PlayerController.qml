// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import QtMultimedia 5.6
import "TimeFormat.js" as TimeFormat

// Playback of one episode at a time (appWindow.player).
//
// - Sources in order: local file, original URL, cloud archive
//   (core.episode().sources); on a playback error the next one is tried at
//   the same position. If the last one fails too, the podcast's feed is
//   updated once and the episode tried again with the fresh links - media
//   links with tokens or tracking parts expire (as in the DownloadManager).
// - Starts at the stored position; a played episode starts from the
//   beginning.
// - The listening state is saved on pause, after seeking, every 30 seconds
//   while playing and when the app closes (core.recordPlayback) - it goes
//   into the sync outbox, so other devices continue at the same spot.
// - At the end: marked as played (which also removes it from the playlist).
//   What follows depends on the playback context (24): an episode started
//   on the playlist page (queueMode) is followed by the next playlist
//   episode; one started anywhere else stops, and - setting
//   "player.afterSingle" - the next playlist episode is loaded paused
//   ("load", default), started ("play") or nothing happens ("stop").
// - Speed and skip times are stored settings; the last episode is restored
//   when the app starts - shown with its stored position, but the media is
//   only loaded on the first "play" (loading it paused in advance left the
//   player in a state where several presses were needed).
// - next(): next chapter; after the last one the next playlist episode,
//   else the most recently played episode in progress. The lock screen and
//   the headset use remoteNext()/remotePrevious(), which follow the setting
//   "player.remotePrev"/"player.remoteNext" (99); the cover's second button coverAction()
//   ("player.coverButton", 87).
Item {
    id: controller

    property int episodeId: -1
    // The core never deletes the file of this episode (immediate clean-up)
    onEpisodeIdChanged: core.setCurrentEpisode(episodeId)
    property var info: ({})
    readonly property bool hasEpisode: episodeId > 0
    // (231) The media player's own state - and "playing" as shown: not
    // while the playback hangs (state "playing", but the position stands
    // still without buffering - seen with streams on a weak network after
    // jumping to the stored position; the bar showed pause and nothing
    // played)
    readonly property bool _playingState: player.playbackState === MediaPlayer.PlayingState
    readonly property bool playing: _playingState && !stuck
    property bool stuck: false
    property real _watchPos: -1
    property int _watchTicks: 0
    property bool _autoRetried: false
    // (239) The media player takes a new rate only with the next seek (the
    // speed stayed at the old value, also for downloaded files): jump to
    // the position right after a change while playing
    onRateChanged: {
        // Set here as well: the order of this handler and the binding is
        // not defined (the seek must come after the new rate)
        player.playbackRate = rate
        // (240) Half a second back: a seek to the same position is ignored
        // (239), a real one applies the rate
        if (!_needsLoad && _playingState && player.seekable) {
            player.seek(Math.max(0, player.position - 500))
        }
    }
    // (233) The last jump: when, from where, and whether a data error after
    // it was already handled
    property real _beforeSeekMs: -1
    property real _seekAt: 0
    property bool _seekRecovered: false
    // (232) Waiting for data as shown (spinner): the media player says
    // "playing" and the position really stands (≥ 1 s) or the media is
    // still loading - streams kept reporting "buffering" while they played
    property real _stillPos: -1
    property int _stillTicks: 0
    readonly property bool waiting: _playingState && !stuck
                                    && (player.status === MediaPlayer.Loading || _stillTicks >= 1)
    readonly property bool buffering: player.status === MediaPlayer.Loading || player.status === MediaPlayer.Buffering
                                      || player.status === MediaPlayer.Stalled
    readonly property string title: info.title || ""
    readonly property string podcastTitle: info.podcastTitle || ""
    readonly property string imageUrl: info.imageUrl || ""
    readonly property string episodeImageUrl: info.episodeImageUrl || ""
    readonly property string coverUrl: info.coverUrl || ""
    // Only replaced when the chapters really change: the info is refreshed
    // with every saved position, and a new array would reset the chapter
    // lists (scroll position)
    property var chapters: []

    function _setInfo(data) {
        info = data
        var fresh = data.chapters || []
        if (JSON.stringify(fresh) !== JSON.stringify(chapters)) {
            chapters = fresh
        }
    }

    // Restored but not loaded yet (see restore())
    property bool _needsLoad: false
    property int _restoreSeconds: 0

    // Milliseconds / seconds
    // (231) While a jump is pending (loading a stream), its target - not
    // 0:00 until the media can seek
    readonly property real positionMs: _needsLoad ? _restoreSeconds * 1000
                                                  : (_pendingSeek >= 0 ? _pendingSeek : player.position)
    readonly property int position: Math.floor(positionMs / 1000)
    // (233) A stream's length is estimated from its start; some files give
    // far too much (Zwischenwasser: 59 min for a 5-minute episode - seeking
    // there failed with "Internal data stream error"). While the player's
    // length is more than 1.5 times the feed's and playback has not passed
    // the feed's length, the feed's counts (bar and seeking); once playback
    // gets past it, the feed was wrong and the player's counts.
    readonly property int _playerSeconds: !_needsLoad && player.duration > 0 ? Math.floor(player.duration / 1000) : 0
    readonly property int _feedSeconds: info.duration || 0
    readonly property bool _lengthUnsure: _playerSeconds > 0 && _feedSeconds > 0
                                          && _playerSeconds > 1.5 * _feedSeconds
                                          && player.position < _feedSeconds * 1000
    readonly property int duration: _playerSeconds > 0 && !_lengthUnsure ? _playerSeconds : _feedSeconds

    // Speed: the podcast's own (35) or the global one
    property real globalRate: parseFloat(core.setting("player.rate", "1.0"))
    readonly property real rate: podcastRate > 0 ? podcastRate : globalRate

    // Per-podcast settings (35): own speed (0 = global), seconds skipped at
    // the start (intro) and before the end (outro); stored as settings
    // "podcast.<id>.rate/.skipIntro/.skipOutro" (PodcastSettingsPage)
    readonly property int podcastId: info.podcastId || 0
    property real podcastRate: 0
    property int skipIntro: 0
    property int skipOutro: 0
    property bool _outroDone: false
    onPodcastIdChanged: reloadPodcastSettings()

    function reloadPodcastSettings() {
        podcastRate = _podcastNumber(podcastId, "rate")
        skipIntro = _podcastNumber(podcastId, "skipIntro")
        skipOutro = _podcastNumber(podcastId, "skipOutro")
    }

    function _podcastNumber(id, name) {
        if (!(id > 0)) {
            return 0
        }
        var value = parseFloat(core.setting("podcast." + id + "." + name, "0"))
        return isNaN(value) || value < 0 ? 0 : value
    }

    // Where an episode starts: its stored position (0 when played), but
    // not inside the podcast's intro
    function _startFor(data) {
        // (189) A skipped episode played again starts where it was left
        var seconds = data.played ? (data.skippedAt || 0) : (data.position || 0)
        var intro = _podcastNumber(data.podcastId, "skipIntro")
        return intro > 0 && seconds < intro ? intro : seconds
    }
    property int skipBackSeconds: parseInt(core.setting("player.skipBack", "30"))
    property int skipForwardSeconds: parseInt(core.setting("player.skipForward", "30"))
    // Sleep timer option "end of episode"
    property bool stopAtEnd: false
    // Playback context: true while playing through the playlist (started on
    // the playlist page) - stored, so it survives a restart
    property bool queueMode: core.setting("player.queueMode", "0") === "1"

    function _setQueueMode(value) {
        if (queueMode !== value) {
            queueMode = value
            core.setSetting("player.queueMode", value ? "1" : "0")
        }
    }

    // Current chapter (index into chapters, -1 if none)
    readonly property int chapterIndex: {
        var ms = positionMs
        var index = -1
        for (var i = 0; i < chapters.length; i++) {
            if (chapters[i].start <= ms + 500) {
                index = i
            } else {
                break
            }
        }
        return index
    }
    readonly property string chapterTitle: chapterIndex >= 0 ? chapters[chapterIndex].title : ""

    property var _sources: []
    property int _sourceIndex: 0
    property real _pendingSeek: -1     // ms, applied once the media is seekable
    property int _startedAt: 0          // position at the start of this playback (s)
    property bool _autoPlay: false
    // (206) The trailer's follow-up was loaded from the end-of-media
    // handler, where play() on the new source was lost (it stood paused at
    // 0:00): played when the source has loaded, a timer as a fallback
    property bool _playWhenLoaded: false
    Timer {
        id: followPlayTimer
        interval: 800
        onTriggered: {
            if (controller._playWhenLoaded && !controller._playingState) {
                controller._playWhenLoaded = false
                player.play()
            }
        }
    }

    signal error(string message)

    // For the sleep timer (fades the volume out)
    property alias mediaPlayer: player

    MediaPlayer {
        id: player
        autoPlay: false
        playbackRate: controller.rate

        onSeekableChanged: controller._applyPendingSeek()
        // Skip the outro (35): end the episode that many seconds early -
        // only for episodes clearly longer than intro + outro
        onPositionChanged: {
            var outro = controller.skipOutro * 1000
            if (outro > 0 && !controller._outroDone && duration > 2 * outro + controller.skipIntro * 1000
                    && playbackState === MediaPlayer.PlayingState && position >= duration - outro) {
                controller._outroDone = true
                controller._finished()
            }
        }
        onStatusChanged: {
            if (status === MediaPlayer.Loaded || status === MediaPlayer.Buffered) {
                controller._applyPendingSeek()
                // (206) Started from the end of the previous one: play
                // once the new source is there
                if (controller._playWhenLoaded) {
                    controller._playWhenLoaded = false
                    player.play()
                }
            } else if (status === MediaPlayer.EndOfMedia) {
                controller._finished()
            }
        }
        onError: {
            console.warn("Playback error: " + errorString + " (" + source + ")")
            // (233) Right after a jump (e.g. beyond the real end of a stream
            // whose length was estimated too long): back to where it was,
            // with a message - not "Cannot play"
            if (Date.now() - controller._seekAt < 10000 && !controller._seekRecovered
                    && controller._beforeSeekMs >= 0) {
                controller._seekRecovered = true
                var back = controller._beforeSeekMs
                controller._pendingSeek = back
                controller._restart()
                appWindow.showMessage(qsTr("This position cannot be reached yet – continuing at %1")
                                      .arg(TimeFormat.clock(Math.floor(back / 1000))))
                return
            }
            controller._tryNextSource()
        }
        onPlaybackStateChanged: {
            if (playbackState !== MediaPlayer.PlayingState) {
                controller.stuck = false
                controller._watchTicks = 0
            }
            // "started" of the sync action: where this stretch of listening
            // began (not before a pending seek has been applied)
            if (playbackState === MediaPlayer.PlayingState && controller._pendingSeek < 0) {
                controller._startedAt = controller.position
            }
        }
    }

    // (231) Watchdog: once a second while the media player says "playing";
    // 4 s without progress and without buffering = hanging (shown as not
    // playing, "play" loads the source again), after 6 s one automatic
    // new attempt per loaded episode
    Timer {
        id: watchdog
        interval: 1000
        repeat: true
        running: controller._playingState && !controller._needsLoad
        onRunningChanged: {
            controller._watchTicks = 0
            controller._watchPos = -1
            controller._stillTicks = 0
            controller._stillPos = -1
        }
        onTriggered: {
            // (232) Standing still at all (also while "buffering")
            if (player.position !== controller._stillPos) {
                controller._stillPos = player.position
                controller._stillTicks = 0
            } else {
                controller._stillTicks += 1
            }
            if (controller.buffering || player.position !== controller._watchPos) {
                controller._watchPos = player.position
                controller._watchTicks = 0
                controller.stuck = false
                return
            }
            controller._watchTicks += 1
            if (controller._watchTicks >= 4 && !controller.stuck) {
                console.warn("Playback hangs at " + Math.round(player.position / 1000) + " s")
                controller.stuck = true
            }
            if (controller._watchTicks >= 6 && !controller._autoRetried) {
                controller._autoRetried = true
                controller._restart()
            }
        }
    }

    // (231) Loads the same source again at the position (or the pending
    // jump) and plays - for a hanging stream
    function _restart() {
        var ms = _pendingSeek >= 0 ? _pendingSeek : player.position
        var src = player.source
        console.warn("Loading the source again at " + Math.round(ms / 1000) + " s")
        stuck = false
        _watchTicks = 0
        _watchPos = -1
        _stillTicks = 0
        _stillPos = -1
        player.stop()
        player.source = ""
        _pendingSeek = ms > 0 ? ms : -1
        player.source = src
        player.play()
    }

    // Saves the position every 30 seconds while playing
    Timer {
        interval: 30000
        repeat: true
        running: controller.playing
        onTriggered: controller.save()
    }

    // Saves once after seeking has settled (dragging the slider seeks often)
    Timer {
        id: seekSaveTimer
        interval: 2000
        onTriggered: controller.save()
    }

    function _applyPendingSeek() {
        if (_pendingSeek >= 0 && player.seekable) {
            player.seek(_pendingSeek)
            _pendingSeek = -1
        }
    }

    // Feed update after a failed playback: once per episode and load
    property bool _refreshTried: false
    property int _refreshPodcastId: -1
    property real _refreshSeconds: 0

    // (193) "Neu" in English: after a new show's trailer, its first episode
    // (once - another episode started forgets it)
    property int _followTrailer: -1
    property int _followUp: -1
    // (207) The episode that follows the trailer playing now (-1 = none),
    // shown first under "Als Nächstes"
    readonly property int followUpId: _followTrailer === episodeId && _followUp > 0 ? _followUp : -1

    function playThen(id, followUp) {
        playOrToggle(id, false)
        _followTrailer = followUp > 0 ? id : -1
        _followUp = followUp > 0 ? followUp : -1
    }

    function _load(id, startSeconds, autoPlay, isRetry) {
        if (id !== _followTrailer) {
            _followTrailer = -1
            _followUp = -1
        }
        if (!isRetry) {
            _refreshTried = false
            _refreshPodcastId = -1
        }
        var data = core.episode(id)
        if (!data.episodeId) {
            return false
        }
        if (data.sources.length === 0) {
            error(qsTr("No source to play this episode"))
            return false
        }
        if (hasEpisode && id !== episodeId) {
            save()
        }
        _needsLoad = false
        _outroDone = false
        _autoRetried = false
        stuck = false
        _setInfo(data)
        episodeId = id
        _sources = data.sources
        _sourceIndex = 0
        _pendingSeek = startSeconds > 0 ? startSeconds * 1000 : -1
        _startedAt = Math.max(0, startSeconds)
        player.source = _sources[0]
        core.setSetting("player.last", "" + id)
        if (autoPlay) {
            player.play()
        }
        return true
    }

    // Plays an episode from its stored position, or toggles pause if it is
    // the current one. fromPlaylist: started on the playlist page (playlist
    // context); for the current episode it can only switch the context on.
    function playOrToggle(id, fromPlaylist) {
        if (id === episodeId && hasEpisode) {
            if (fromPlaylist === true) {
                _setQueueMode(true)
            }
            if (!_needsLoad) {
                toggle()
                return
            }
        } else {
            _setQueueMode(fromPlaylist === true)
        }
        var data = core.episode(id)
        _load(id, _startFor(data), true)
    }

    // Plays an episode from a given second (chapters)
    function playFrom(id, seconds, fromPlaylist) {
        if (id === episodeId && hasEpisode) {
            if (fromPlaylist === true) {
                _setQueueMode(true)
            }
        } else {
            _setQueueMode(fromPlaylist === true)
        }
        if (id === episodeId && hasEpisode && !_needsLoad) {
            seek(seconds)
            if (stuck) {
                _restart()
            } else if (!_playingState) {
                player.play()
            }
            return
        }
        _load(id, seconds, true)
    }

    function toggle() {
        if (playing) {
            pause()
        } else {
            resume()
        }
    }

    // Smart resume (53): after a pause of more than 5 minutes (or after a
    // restart) playback continues a few seconds earlier, to get back into
    // it. Setting "player.smartResume" in seconds, 0 = off.
    property real _pausedAt: 0

    function _rewindSeconds(longPause) {
        var back = parseInt(core.setting("player.smartResume", "10"))
        return longPause && back > 0 ? back : 0
    }

    function resume() {
        if (!hasEpisode || playing) {
            return
        }
        // (231) Hanging: load the source again (play() would do nothing,
        // the media player is in "playing" already)
        if (stuck) {
            _restart()
            return
        }
        var longPause = _pausedAt > 0 && Date.now() - _pausedAt > 5 * 60000
        _pausedAt = 0
        if (_needsLoad) {
            // Restored or prepared: always a long pause
            _load(episodeId, Math.max(0, _restoreSeconds - _rewindSeconds(_restoreSeconds > 0)), true)
        } else {
            var back = _rewindSeconds(longPause)
            if (back > 0 && player.position > 0) {
                seek(Math.max(0, position - back))
            }
            player.play()
        }
    }

    function pause() {
        _playWhenLoaded = false
        if (_playingState) {
            player.pause()
            _pausedAt = Date.now()
        }
        save()
    }

    function seek(seconds) {
        if (!hasEpisode) {
            return
        }
        if (_needsLoad) {
            _restoreSeconds = Math.max(0, duration > 0 ? Math.min(seconds, duration - 1) : seconds)
            return
        }
        // (233) Limited by the length as shown (the feed's while the player's
        // is unsure)
        var ms = Math.max(0, Math.min(seconds * 1000, duration > 0 ? duration * 1000 - 1000 : seconds * 1000))
        // (233) Where to go back to if the jump fails with a data error
        _beforeSeekMs = player.position
        _seekAt = Date.now()
        _seekRecovered = false
        if (player.seekable) {
            player.seek(ms)
        } else {
            _pendingSeek = ms
        }
        seekSaveTimer.restart()
    }

    function skip(seconds) {
        seek(position + seconds)
    }

    function nextChapter() {
        if (chapterIndex + 1 < chapters.length) {
            seek(chapters[chapterIndex + 1].start / 1000)
        }
    }

    // In-app "next": next chapter, after the last one the next episode
    // (playlist, else the latest one in progress)
    function next() {
        if (!hasEpisode) {
            return
        }
        if (chapterIndex + 1 < chapters.length) {
            nextChapter()
            return
        }
        nextEpisode()
    }

    // Next episode: playlist, else the latest one in progress
    function nextEpisode() {
        if (!hasEpisode) {
            return
        }
        // (193) A trailer from "Neu": on with its show's first episode
        var id = episodeId === _followTrailer && _followUp > 0 ? _followUp : core.nextEpisode(episodeId)
        // (169) Left in its last chapter or in the last 2 minutes: heard -
        // played and out of the playlist (it came back as the next one)
        var last = (chapters.length > 0 && chapterIndex === chapters.length - 1)
                   || (duration > 0 && duration - position < 120)
        var left = (id > 0 && last && !_needsLoad) ? episodeId : -1
        // (186) Left in the middle (without chapters, or before the last
        // one): 188 - skipped means "not to be heard": played as well (out
        // of the playlist); "Recently heard" on the playlist page brings it
        // back
        var skipped = (id > 0 && !last && !_needsLoad) ? episodeId : -1
        var skippedAt = position
        if (id > 0) {
            // A playlist episode continues the playlist; an episode in
            // progress is a single one
            _setQueueMode(core.isQueued(id))
            var data = core.episode(id)
            // After _load, which saves the old position first (and puts
            // the new episode at the top of the playlist, 186)
            if (_load(id, _startFor(data), true)) {
                if (left > 0) {
                    core.noteHeard(left)
                    core.markPlayed(left)
                } else if (skipped > 0) {
                    // (208) The most surprising rule of 1.2, said once
                    appWindow.showHint("skip", qsTr("Skipped episodes count as played - \"Recently heard\" on the playlist page brings them back, where you left them"))
                    core.noteSkipped(skipped, skippedAt)
                    core.noteHeard(skipped)
                    core.markPlayed(skipped)
                }
            }
        }
    }

    // Second button of the app cover (87, setting "player.coverButton"):
    // "next" (default: chapter, then episode - as next()), "back" (skip
    // back), "forward" (skip forward), "sleep" (sleep timer 15 min / off -
    // handled by the cover), "bookmark" (93: an unnamed bookmark at the
    // current position - handled by the cover)
    property string coverButton: core.setting("player.coverButton", "next")

    function coverAction() {
        if (!hasEpisode) {
            return
        }
        if (coverButton === "back") {
            skip(-skipBackSeconds)
        } else if (coverButton === "forward") {
            skip(skipForwardSeconds)
        } else {
            next()
        }
    }

    // The two buttons of the lock screen and the headset (83, each on its
    // own since 99; the app cover has its own setting since 87). Only
    // actions that fit the arrows the system draws (101 - a bookmark or the
    // sleep timer behind an arrow looked like a fault; they are on the cover):
    //  "player.remotePrev": "chapter" (default: chapter start / previous
    //    chapter, without chapters skip back), "back" (skip back)
    //  "player.remoteNext": "chapters" (default: next chapter, then the next
    //    episode), "episodes" (next episode), "forward" (skip forward)
    // Taken over from "player.remoteButtons" (83-98) when not set yet;
    // "bookmark"/"sleep" of 99-100 fall back to the defaults.
    readonly property string _oldRemote: core.setting("player.remoteButtons", "chapters")
    property string remotePrev: _validPrev(core.setting("player.remotePrev",
                                                        _oldRemote === "chapters" ? "chapter" : "back"))
    property string remoteNextAction: _validNext(core.setting("player.remoteNext",
                                                              _oldRemote === "skip" ? "forward"
                                                              : (_oldRemote === "episodes" ? "episodes" : "chapters")))

    function _validPrev(value) {
        return value === "back" ? "back" : "chapter"
    }

    function _validNext(value) {
        return value === "forward" || value === "episodes" ? value : "chapters"
    }

    function remoteNext() {
        if (!hasEpisode) {
            return
        }
        if (remoteNextAction === "forward") {
            skip(skipForwardSeconds)
        } else if (remoteNextAction === "episodes") {
            nextEpisode()
        } else {
            next()
        }
    }

    function remotePrevious() {
        if (!hasEpisode) {
            return
        }
        if (remotePrev === "back") {
            skip(-skipBackSeconds)
        } else {
            previous()
        }
    }

    // Lock screen "previous": chapter start / previous chapter, without
    // chapters the usual skip back
    function previous() {
        if (chapters.length > 0) {
            previousChapter()
        } else {
            skip(-skipBackSeconds)
        }
    }

    function previousChapter() {
        if (chapterIndex < 0) {
            seek(0)
            return
        }
        // Within the first seconds of a chapter: to the previous one
        var start = chapters[chapterIndex].start / 1000
        if (position - start > 3 || chapterIndex === 0) {
            seek(start)
        } else {
            seek(chapters[chapterIndex - 1].start / 1000)
        }
    }

    // Changes the podcast's own speed when it has one, else the global one
    function setRate(value) {
        if (podcastRate > 0 && podcastId > 0) {
            podcastRate = value
            core.setSetting("podcast." + podcastId + ".rate", "" + value)
        } else {
            globalRate = value
            core.setSetting("player.rate", "" + value)
        }
    }

    function save() {
        if (hasEpisode && !_needsLoad && player.duration > 0 && player.position > 0) {
            core.recordPlayback(episodeId, _startedAt, position, duration)
        }
    }

    function _finished() {
        var finished = episodeId
        // (193) The trailer of a new show: its first episode follows
        if (finished === _followTrailer && _followUp > 0) {
            var follow = _followUp
            core.markPlayed(finished)
            _setQueueMode(false)
            if (_load(follow, _startFor(core.episode(follow)), true)) {
                _playWhenLoaded = true
                followPlayTimer.restart()
            }
            return
        }
        // (169) The next one first: it is the one after the finished
        // episode's place in the playlist, which markPlayed removes
        var next = core.queueNext(finished)
        // Also removes it from the playlist (AppCore::markPlayed)
        core.noteHeard(finished)
        core.markPlayed(finished)
        if (stopAtEnd) {
            stopAtEnd = false
            next = -1
        }
        // Playlist context: on with the playlist. Otherwise as set.
        var mode = queueMode ? "play" : core.setting("player.afterSingle", "load")
        if (next > 0 && mode === "play") {
            _setQueueMode(true)
            var data = core.episode(next)
            // (206) As for the trailer's follow-up: play once loaded
            if (_load(next, _startFor(data), true)) {
                _playWhenLoaded = true
                followPlayTimer.restart()
            }
        } else if (next > 0 && mode === "load") {
            // Ready in the player bar, one tap continues the playlist
            _setQueueMode(true)
            _prepare(next)
        } else {
            // Stays selected (player bar, lock screen) at the beginning
            _setInfo(core.episode(finished))
            _pendingSeek = -1
            player.stop()
        }
    }

    // Shows an episode paused at its stored position without loading the
    // media yet (as after a restart, see restore())
    function _prepare(id) {
        var data = core.episode(id)
        if (!data.episodeId) {
            return
        }
        player.stop()
        _pendingSeek = -1
        _setInfo(data)
        _restoreSeconds = _startFor(data)
        _needsLoad = true
        episodeId = id
        core.setSetting("player.last", "" + id)
    }

    function _tryNextSource() {
        if (_sourceIndex + 1 < _sources.length) {
            _pendingSeek = player.position > 0 ? player.position : _pendingSeek
            _sourceIndex += 1
            player.source = _sources[_sourceIndex]
            player.play()
        } else if (!_refreshTried && info.podcastId > 0) {
            // Maybe an expired link: fresh links from the feed, then again
            _refreshTried = true
            _refreshPodcastId = info.podcastId
            _refreshSeconds = player.position > 0 ? player.position / 1000
                                                  : (_pendingSeek >= 0 ? _pendingSeek / 1000 : 0)
            console.warn("Playback failed, updating the feed of podcast " + info.podcastId)
            core.refreshPodcast(info.podcastId)
        } else {
            error(qsTr("Cannot play: %1").arg(player.errorString))
        }
    }

    Connections {
        target: core
        onPodcastRefreshed: {
            if (podcastId !== controller._refreshPodcastId) {
                return
            }
            controller._refreshPodcastId = -1
            if (ok) {
                controller._load(controller.episodeId, controller._refreshSeconds, true, true)
            } else {
                controller.error(qsTr("Cannot play: %1").arg(controller.mediaPlayer.errorString))
            }
        }
    }

    // Refresh the shown data (chapters loaded, downloaded, ...) without
    // touching the running playback
    Connections {
        target: core
        onEpisodeChanged: {
            if (episodeId === controller.episodeId) {
                controller._setInfo(core.episode(controller.episodeId))
                // Position changed elsewhere (sync) before the first play
                if (controller._needsLoad) {
                    controller._restoreSeconds = controller._startFor(controller.info)
                }
            }
        }
    }

    // Last episode of the previous session: shown at its position, loaded
    // on the first play
    function restore() {
        var last = parseInt(core.setting("player.last", "-1"))
        if (last > 0) {
            var data = core.episode(last)
            if (data.episodeId && data.sources.length > 0) {
                _setInfo(data)
                _restoreSeconds = _startFor(data)
                _needsLoad = true
                episodeId = last
            }
        }
    }
}

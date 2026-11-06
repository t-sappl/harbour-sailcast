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
    onEpisodeIdChanged: {
        core.setCurrentEpisode(episodeId)
        // (243) Video from the feed's MIME type (see hasVideo)
        _videoSeen = false
        pictureOff = false
        _refreshVideo()
    }
    // (245) Checked on every load, also of the same episode (the version
    // chosen in the podcast settings changes the type)
    function _refreshVideo() {
        // The video version chosen and played, or a video file
        // (249) HLS (application/x-mpegURL) counts as video from the start,
        // so the picture area and its switch are there without waiting for
        // a picture of the player
        var mime = ("" + (info.mimeType || "")).toLowerCase()
        hasVideo = episodeId > 0 && (info.videoVersion === true
                                     || mime.indexOf("video/") === 0
                                     || mime.indexOf("mpegurl") >= 0 || _videoSeen)
    }
    // (245) "Sound only" for this episode (switch on the episode page): the
    // page takes its output away; with a video version in the feed the
    // audio version is played instead (setVersion)
    property bool pictureOff: false
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
    // (261) Diagnostic lines with a time (the device log has none) - for the
    // video problems: speed, loading, crash
    function _dlog(text) {
        console.warn(new Date().toISOString().substr(11, 12) + " " + text)
    }
    function _state() {
        return "ep=" + episodeId + " video=" + hasVideo + " outputs=" + videoOutputs
                + " state=" + player.playbackState + " status=" + player.status
                + " seekable=" + player.seekable + " pos=" + Math.round(player.position / 1000)
                + "s dur=" + Math.round(player.duration / 1000) + "s rate=" + player.playbackRate
    }
    property real _watchPos: -1
    property int _watchTicks: 0
    property bool _autoRetried: false
    // (239) The media player takes a new rate only with the next seek (the
    // speed stayed at the old value, also for downloaded files): jump to
    // the position right after a change while playing
    onRateChanged: {
        _dlog("Rate changed to " + rate + "x: " + _state())
        // Set here as well: the order of this handler and the binding is
        // not defined (the seek must come after the new rate)
        // (258) Not while a video picture is attached: the extra rate set and
        // the seek each start a flushing seek in the backend, three at once
        // crashed with a video output (Thomas 6 Nov); there the binding
        // alone sets the rate
        // (260, log of 6 Nov: the speed did not change at all) the one seek
        // follows after a pause, once
        // (262, log of 6 Nov: "Rate changed to 2x ... rate=1" a second later)
        // The binding "playbackRate: controller.rate" is gone once the
        // handler below has assigned the rate (an assignment breaks a
        // binding), and this branch assigned nothing - so the player never
        // got the new rate. Assign it once here; the seek follows later.
        if (hasVideo && videoOutputs > 0) {
            player.playbackRate = rate
            videoRateTimer.restart()
            return
        }
        player.playbackRate = rate
        // (240) Half a second back: a seek to the same position is ignored
        // (239), a real one applies the rate
        if (!_needsLoad && _playingState && player.seekable) {
            player.seek(Math.max(0, player.position - 500))
        }
    }
    // (262) One small jump after a start at a speed other than 1x - the
    // media player takes the rate only with a seek (239)
    Timer {
        id: rateKickTimer
        interval: 1200
        onTriggered: {
            if (!controller._needsLoad && controller._playingState && player.seekable
                    && controller._pendingSeek < 0
                    && Math.abs(player.playbackRate - controller.rate) < 0.01
                    && controller._kickedSource !== player.source.toString()) {
                controller._kickedSource = player.source.toString()
                controller._dlog("Rate kick: " + controller._state())
                player.seek(player.position + 100)
            }
        }
    }
    property string _kickedSource: ""
    Timer {
        id: videoRateTimer
        interval: 400
        onTriggered: {
            controller._dlog("Speed " + controller.rate + "x during a video: " + controller._state())
            if (controller._needsLoad || !controller._playingState) {
                return
            }
            if (player.seekable) {
                player.seek(Math.max(0, player.position - 500))
            } else {
                // (261, log of 6 Nov: the speed stayed as at the start) a
                // stream that cannot jump takes the rate with the next load
                controller._restart()
            }
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
        // (256, Thomas: played episodes at the end of the playlist ended at
        // once and the player ran through them) a played episode never
        // starts in its last 2 minutes - from the beginning instead
        if (data.played && (data.duration || 0) > 0 && seconds > data.duration - 120) {
            seconds = 0
        }
        var intro = _podcastNumber(data.podcastId, "skipIntro")
        return intro > 0 && seconds < intro ? intro : seconds
    }
    // (252) Counts as played when paused, left or closed with at most this
    // many seconds left (0 = off); setting "player.countPlayedBelow"
    property int countPlayedBelow: parseInt(core.setting("player.countPlayedBelow", "0"))
    property int skipBackSeconds: parseInt(core.setting("player.skipBack", "30"))
    property int skipForwardSeconds: parseInt(core.setting("player.skipForward", "30"))
    // (246) Press and hold on the skip buttons
    property int skipLargeSeconds: parseInt(core.setting("player.skipLarge", "60"))
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

    // For the sleep timer (fades the volume out); (241) the VideoView
    // attaches its output to it
    property alias mediaPlayer: player
    // (241) The episode has a picture. (243) From the feed's MIME type,
    // or once the media player reported one for this episode - not the
    // player's flag alone: it drops while the source is loaded again,
    // which would take the output away and load again in a loop
    property bool hasVideo: false
    property bool _videoSeen: false
    Connections {
        target: player
        onHasVideoChanged: {
            if (player.hasVideo && !controller._videoSeen) {
                controller._videoSeen = true
                controller.hasVideo = true
            }
        }
    }

    // (243) Video outputs attached to the media player (VideoView). The
    // Sailfish media player hangs when an output is attached or detached
    // while it plays (241/242: stopped, buffered for ever) - so when the
    // number changes between none and some, the source is loaded again at
    // the position (with or without picture) and plays on if it played.
    // Moving the same output (full screen) is not a change.
    property int videoOutputs: 0
    property bool _outputsApplied: false
    // (245) Every new output counts, also when one went and another came
    // within the 400 ms (unlocking the screen: black picture, "neither
    // renderer nor window control") - generation of the outputs at the
    // last load
    property int _outputGeneration: 0
    property int _appliedGeneration: 0
    function _markOutputsApplied() {
        _outputsApplied = videoOutputs > 0
        _appliedGeneration = _outputGeneration
        _videoReloadPending = false
    }
    function videoOutputAdded() {
        videoOutputs += 1
        _outputGeneration += 1
        videoReloadTimer.restart()
    }
    function videoOutputRemoved() {
        videoOutputs = Math.max(0, videoOutputs - 1)
        videoReloadTimer.restart()
    }
    // Several changes at once (a page closed, another opened) count once
    Timer {
        id: videoReloadTimer
        interval: 400
        // (257, Thomas: the screen jerked when changing to or from the
        // episode page while a video played) not while a page animation
        // runs - the reload (pipeline built again) blocked the animation;
        // checked again until the animation has ended
        onTriggered: {
            if (appWindow.pageStack.busy) {
                restart()
                return
            }
            controller._applyVideoOutputs()
        }
    }
    function _applyVideoOutputs() {
        var want = videoOutputs > 0
        var fresh = want && _outputGeneration !== _appliedGeneration
        if (want === _outputsApplied && !fresh) {
            return
        }
        // Nothing loaded (yet): the next load picks the outputs up
        if (!hasEpisode || _needsLoad || !hasVideo || ("" + player.source) === ""
                || player.status === MediaPlayer.NoMedia || player.status === MediaPlayer.UnknownStatus) {
            _outputsApplied = want
            _appliedGeneration = _outputGeneration
            return
        }
        _outputsApplied = want
        _appliedGeneration = _outputGeneration
        if (!_playingState) {
            // Paused: loaded again on the next "play" (loading paused left
            // the player needing several presses, see the header)
            console.warn("Video output " + (want ? "on" : "off") + " while paused, reloading on play")
            _videoReloadPending = true
            return
        }
        console.warn("Video output " + (want ? (fresh && videoOutputs > 0 ? "new" : "on") : "off")
                     + ", reloading at " + Math.round((_pendingSeek >= 0 ? _pendingSeek : player.position) / 1000) + " s")
        _restart()
    }
    property bool _videoReloadPending: false

    MediaPlayer {
        id: player
        autoPlay: false
        playbackRate: controller.rate
        // (251) Silent while a reload is checked (the start of an HLS stream
        // was audible until the jump); the check always ends (15 s at most)
        muted: controller._verifyTarget >= 0 && controller._playingState && !controller._needsLoad

        onSeekableChanged: {
            controller._dlog("Seekable " + seekable + ": " + controller._state())
            controller._applyPendingSeek()
        }
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
            controller._dlog("Status " + status + ": " + controller._state())
            if (status === MediaPlayer.Loaded || status === MediaPlayer.Buffered) {
                controller._applyPendingSeek()
                // (206) Started from the end of the previous one: play
                // once the new source is there
                if (controller._playWhenLoaded) {
                    controller._playWhenLoaded = false
                    player.play()
                }
            } else if (status === MediaPlayer.EndOfMedia) {
                // (260) A reload of a stream reported "end" while the jump
                // to the old position was checked: the episode was marked
                // played and the next one started (log of 6 Nov, video). Not
                // the end when the target is far from the real end.
                if (controller._verifyTarget >= 0 && controller._verifyTries < 4
                        && controller._verifyTarget < (controller.duration - 15) * 1000) {
                    controller._verifyTries += 1
                    console.warn("End of media while the reload is checked - jumping to "
                                 + Math.round(controller._verifyTarget / 1000) + " s instead of finishing")
                    player.seek(controller._verifyTarget)
                    player.play()
                    return
                }
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
            controller._dlog("State " + playbackState + ": " + controller._state())
            if (playbackState !== MediaPlayer.PlayingState) {
                controller.stuck = false
                controller._watchTicks = 0
            }
            // "started" of the sync action: where this stretch of listening
            // began (not before a pending seek has been applied)
            // (262, Thomas 6 Nov: the first play ran at the wrong speed, only
            // a jump applied it) The seek of _applyPendingSeek can come
            // before the pipeline plays and is lost then; once more, shortly
            // after the playback has really started
            if (playbackState === MediaPlayer.PlayingState && controller._pendingSeek < 0) {
                if (Math.abs(controller.rate - 1.0) > 0.01) {
                    rateKickTimer.restart()
                }
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
            // (244) Listening log: a second really listened when the
            // position moved (wall clock - at the speed; seeking and
            // standing still do not count)
            if (player.position !== controller._listenPos) {
                // (245) also the episode time: the progress of the position
                // (at 2x about 2 s per second; jumps left out)
                var step = player.position - controller._listenPos
                if (controller._listenPos >= 0 && step > 0 && step < 5000) {
                    controller._listenSecs += 1
                    controller._listenMediaMs += step
                }
                controller._listenPos = player.position
            }
            // (245) After a reload: playing at the target? Else jump again -
            // the jump got lost now and then (243), an HLS stream starts at
            // its beginning and ignores the first one.
            // (251) The sound is off while this runs (the start of the
            // stream was audible for a moment); it waits for a seekable
            // stream and jumps up to 4 times, 15 s at most (before: gave up
            // after 2 s / 2 jumps and stayed at the beginning)
            if (controller._verifyTarget >= 0) {
                controller._verifyTicks += 1
                controller._verifyTotal += 1
                if (controller._verifyTicks >= 2) {
                    var off = Math.abs(player.position - controller._verifyTarget)
                    if (off <= 5000) {
                        controller._verifyTarget = -1
                    } else if (controller._verifyTries < 4 && player.seekable) {
                        controller._verifyTries += 1
                        console.warn("Reload landed at " + Math.round(player.position / 1000) + " s instead of "
                                     + Math.round(controller._verifyTarget / 1000) + " s - jumping again ("
                                     + controller._verifyTries + ")")
                        player.seek(controller._verifyTarget)
                        controller._verifyTicks = 0
                    } else if (controller._verifyTotal >= 15 || controller._verifyTries >= 4) {
                        console.warn("Reload: position " + Math.round(controller._verifyTarget / 1000)
                                     + " s not reached, giving up")
                        controller._verifyTarget = -1
                    }
                }
            }
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
        player.playbackRate = rate
        player.stop()
        player.source = ""
        _pendingSeek = ms > 0 ? ms : -1
        _markOutputsApplied()
        // (245) Check where it really starts (the jump got lost now and then)
        _verifyTarget = _pendingSeek
        _verifyTries = 0
        _verifyTotal = 0
        _verifyTicks = 0
        _rateApplied = false
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
            _rateApplied = true
        } else if (!_rateApplied && player.seekable && Math.abs(rate - 1.0) > 0.01) {
            // (254) An episode that starts at 0:00 has no jump, and the media
            // player takes a new source's rate only with a seek (239): the
            // next episode of the playlist played at 1.0x until the first
            // skip (forum, 5 Nov). A tiny jump forward applies it.
            _rateApplied = true
            player.seek(player.position + 100)
        }
    }
    // The current source has had a seek (or the rate was applied otherwise)
    property bool _rateApplied: false

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
        // (260) The check of a reload belongs to the source it was made for:
        // a stale target jumped the next episode to the old position (log of
        // 6 Nov: "landed at 986 s instead of 415 s")
        _verifyTarget = -1
        _verifyTries = 0
        _verifyTotal = 0
        _verifyTicks = 0
        var data = core.episode(id)
        if (!data.episodeId) {
            return false
        }
        if (data.sources.length === 0) {
            error(qsTr("No source to play this episode"))
            return false
        }
        // (245) A video stream without Wi-Fi: setting "video.mobile" -
        // ask (default), always, never; asked once per episode
        var firstIsStream = ("" + data.sources[0]).indexOf("file:") !== 0
        var videoStream = firstIsStream && (data.videoVersion === true
                                            || ("" + (data.mimeType || "")).indexOf("video/") === 0)
        if (autoPlay && videoStream && _mobileVideoOk !== id && !core.onWifi()) {
            var mode = core.setting("video.mobile", "ask")
            if (mode === "never" || (mode === "ask" && Qt.application.state !== Qt.ApplicationActive)) {
                if (data.videoVersion === true && data.sources.length > 1) {
                    // The audio version instead (no question in the background)
                    data.sources = data.sources.slice(1)
                    data.videoVersion = false
                    appWindow.showMessage(qsTr("No Wi-Fi: playing the audio version"))
                } else {
                    error(qsTr("Videos only on Wi-Fi – download it or watch it later"))
                    return false
                }
            } else if (mode === "ask") {
                appWindow.askMobileVideo(Math.round((data.videoBytes || 0) / 1048576), function() {
                    controller._mobileVideoOk = id
                    controller._load(id, startSeconds, autoPlay, isRetry)
                })
                return false
            }
        }
        if (hasEpisode && id !== episodeId) {
            save()
            countAsPlayedIfNearEnd()
            // (253) The episode left goes into "Recently heard" at once
            // (before only at its end or when skipped) - "Previous" in the
            // player bar comes back to it
            core.noteHeard(episodeId)
        }
        _needsLoad = false
        _rateApplied = false
        _playedEnd = false
        _outroDone = false
        _autoRetried = false
        stuck = false
        _setInfo(data)
        episodeId = id
        _refreshVideo()
        _sources = data.sources
        _sourceIndex = 0
        _pendingSeek = startSeconds > 0 ? startSeconds * 1000 : -1
        _startedAt = Math.max(0, startSeconds)
        _markOutputsApplied()
        _logSource()
        // (261, log of 6 Nov: the app died right after loading a video
        // stream over a playing file, GStreamer pad assertions before) As in
        // _restart: stop and clear the old source first
        // (262, Thomas 6 Nov: only a podcast's own speed was taken, not the
        // global one) The player gets the rate with every load, not only when
        // it changes: the global rate stays the same between two episodes,
        // so no change event came, and the binding is gone after the first
        // assignment
        player.playbackRate = rate
        _dlog("Load " + id + " (" + (videoStream ? "video stream" : "file/audio") + "), old: " + _state())
        if (("" + player.source) !== "") {
            player.stop()
            player.source = ""
            _dlog("Old source cleared")
        }
        player.source = _sources[0]
        _dlog("New source set")
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
                // (256) Standing at its end (played, "0 left"): play starts
                // it from the beginning instead of ending again at once
                if (!playing && player.duration > 0 && player.duration - player.position < 2000) {
                    seek(0)
                }
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
        // (243) The video output changed while paused: load again here
        if (_videoReloadPending && !_needsLoad) {
            _videoReloadPending = false
            var rewind = _rewindSeconds(_pausedAt > 0 && Date.now() - _pausedAt > 5 * 60000)
            _pausedAt = 0
            if (rewind > 0 && player.position > 0) {
                _pendingSeek = Math.max(0, player.position - rewind * 1000)
            }
            _restart()
            return
        }
        var longPause = _pausedAt > 0 && Date.now() - _pausedAt > 5 * 60000
        _pausedAt = 0
        if (_needsLoad) {
            // Restored or prepared: always a long pause
            var from = _playedEnd ? 0 : Math.max(0, _restoreSeconds - _rewindSeconds(_restoreSeconds > 0))
            _playedEnd = false
            _load(episodeId, from, true)
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
        // (253) Counted as played: the bar shows the end (0 left) like a
        // played episode; the next "play" starts it again from the beginning
        if (countAsPlayedIfNearEnd()) {
            var end = duration
            player.stop()
            _pendingSeek = -1
            _restoreSeconds = end
            _playedEnd = true
            _needsLoad = true
        }
    }
    property bool _playedEnd: false

    // (252) "Count as played when less than … remains": the episode is
    // marked played (and leaves the playlist) without the next one following.
    // Only for episodes clearly longer than the limit; the real end goes
    // through _finished()
    function countAsPlayedIfNearEnd() {
        var limit = countPlayedBelow * 1000
        if (limit <= 0 || !hasEpisode || _needsLoad || player.duration <= 0 || player.position <= 0) {
            return false
        }
        if (player.duration <= 4 * limit || player.duration - player.position > limit) {
            return false
        }
        console.warn("Episode " + episodeId + " counts as played (" + Math.round((player.duration - player.position) / 1000) + " s left)")
        core.noteHeard(episodeId)
        core.markPlayed(episodeId)
        return true
    }

    function seek(seconds) {
        if (!hasEpisode) {
            return
        }
        _dlog("Seek to " + Math.round(seconds) + " s: " + _state())
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
        // (244) Listening log; (245) with the episode time
        if (hasEpisode && _listenSecs > 0) {
            core.logListening(episodeId, _listenSecs, Math.round(_listenMediaMs / 1000))
        }
        _listenSecs = 0
        _listenMediaMs = 0
    }
    // (245) Episode allowed to stream video over mobile data (asked once)
    property int _mobileVideoOk: -1
    property int _listenSecs: 0
    property real _listenMediaMs: 0
    property real _listenPos: -1
    // (245) Position check after a reload (watchdog)
    property real _verifyTarget: -1
    property int _verifyTries: 0
    property int _verifyTotal: 0
    property int _verifyTicks: 0

    // (245) Which source plays (log): video version, download, original, archive
    function _logSource() {
        var src = "" + (_sources[_sourceIndex] || "")
        var kind = src.indexOf("file:") === 0 ? "download"
                 : (_sourceIndex === 0 && info.videoVersion ? "video version"
                 : (_sourceIndex === _sources.length - 1 && _sources.length > 2 ? "archive" : "original"))
        console.warn("Playing source " + (_sourceIndex + 1) + "/" + _sources.length + " (" + kind + ")")
    }

    // (245) The version (podcast settings or the switch) changed: the
    // episode playing loads again at its position in the other version
    function reloadVersion(podcastId) {
        if (!hasEpisode || _needsLoad || (podcastId > 0 && info.podcastId !== podcastId)) {
            return
        }
        var data = core.episode(episodeId)
        if (!data.episodeId) {
            return
        }
        _setInfo(data)
        _videoSeen = false
        _refreshVideo()
        var wasPlaying = _playingState
        var ms = _pendingSeek >= 0 ? _pendingSeek : player.position
        if (!wasPlaying) {
            // Paused: loaded in the new version on the next "play", as a
            // restored episode (loading paused needed several presses)
            console.warn("Version changed while paused, loading on play")
            player.stop()
            _pendingSeek = -1
            _restoreSeconds = Math.floor(ms / 1000)
            _needsLoad = true
            return
        }
        _sources = data.sources
        _sourceIndex = 0
        console.warn("Version changed, reloading at " + Math.round(ms / 1000) + " s")
        _logSource()
        player.stop()
        player.source = ""
        _pendingSeek = ms > 0 ? ms : -1
        _verifyTarget = _pendingSeek
        _verifyTries = 0
        _verifyTotal = 0
        _verifyTicks = 0
        _markOutputsApplied()
        _rateApplied = false
        player.source = _sources[0]
        player.play()
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
        _playedEnd = false
        _needsLoad = true
        episodeId = id
        core.setSetting("player.last", "" + id)
    }

    function _tryNextSource() {
        if (_sourceIndex + 1 < _sources.length) {
            _pendingSeek = player.position > 0 ? player.position : _pendingSeek
            _sourceIndex += 1
            _markOutputsApplied()
            _logSource()
            _rateApplied = false
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

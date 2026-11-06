// SPDX-License-Identifier: GPL-3.0-or-later
.pragma library

// Formatting of durations and dates for the lists and the player.
// Texts are translated by the calling QML file (qsTr does not work in a
// .pragma library), so the functions take the translated patterns.

// 3725 -> "1:02:05", 125 -> "2:05"
function clock(seconds) {
    if (seconds === undefined || seconds < 0) {
        return "--:--"
    }
    seconds = Math.floor(seconds)
    var h = Math.floor(seconds / 3600)
    var m = Math.floor(seconds / 60) % 60
    var s = seconds % 60
    var mm = (h > 0 && m < 10 ? "0" : "") + m
    var ss = (s < 10 ? "0" : "") + s
    return (h > 0 ? h + ":" : "") + mm + ":" + ss
}

// Rough length: 3725 -> "1 h 2 min", 1500 -> "25 min", 40 -> "1 min".
// hourPattern e.g. "%1 h %2 min", minutePattern "%1 min"
function length(seconds, hourPattern, minutePattern) {
    if (seconds === undefined || seconds <= 0) {
        return ""
    }
    var minutes = Math.max(1, Math.round(seconds / 60))
    if (minutes >= 60) {
        return hourPattern.arg(Math.floor(minutes / 60)).arg(minutes % 60)
    }
    return minutePattern.arg(minutes)
}

// Date of publication: "today", "yesterday", weekday within a week,
// else the short date of the locale
function day(ms, today, yesterday) {
    if (!ms || ms <= 0) {
        return ""
    }
    var date = new Date(ms)
    var now = new Date()
    var startOfToday = new Date(now.getFullYear(), now.getMonth(), now.getDate()).getTime()
    var dayMs = 24 * 3600 * 1000
    if (ms >= startOfToday) {
        return today
    }
    if (ms >= startOfToday - dayMs) {
        return yesterday
    }
    if (ms >= startOfToday - 6 * dayMs) {
        return Qt.formatDate(date, "dddd")
    }
    return Qt.formatDate(date, Qt.DefaultLocaleShortDate)
}

// File size: 734003200 -> "700 MB", 4724464025 -> "4,4 GB" (decimal
// separator of the locale). Patterns e.g. "%1 MB", "%1 GB"
function size(bytes, megabytePattern, gigabytePattern) {
    if (bytes === undefined || bytes <= 0) {
        return megabytePattern.arg(0)
    }
    var mb = bytes / (1024 * 1024)
    if (mb >= 1024) {
        return gigabytePattern.arg(Number(mb / 1024).toLocaleString(Qt.locale(), "f", 1))
    }
    return megabytePattern.arg(Math.max(1, Math.round(mb)))
}

// (256) Time stamps in show notes ("12:34", "1:02:03") as links
// "sailcast-seek:<seconds>" - outside links, not after "Uhr"/"h"/"am"/"pm",
// not past the length (lengthSeconds; 0 = unknown). From the episode page
// (53), shared with the Notes tab of the player.
function linkTimestamps(html, lengthSeconds) {
    var parts = ("" + (html || "")).split(/(<[^>]*>)/)
    var inLink = false
    var length = lengthSeconds > 0 ? lengthSeconds : 24 * 3600
    for (var i = 0; i < parts.length; i++) {
        var part = parts[i]
        if (part.charAt(0) === "<") {
            if (/^<a[\s>]/i.test(part)) {
                inLink = true
            } else if (/^<\/a\s*>/i.test(part)) {
                inLink = false
            }
            continue
        }
        if (inLink) {
            continue
        }
        parts[i] = part.replace(/\b(?:(\d{1,2}):)?([0-5]?\d):([0-5]\d)\b(?!\s*(Uhr|h\b|o'clock|am\b|pm\b))/gi, function(match, h, m, s) {
            var seconds = (h ? parseInt(h, 10) * 3600 : 0) + parseInt(m, 10) * 60 + parseInt(s, 10)
            if (seconds > length) {
                return match
            }
            return "<a href=\"sailcast-seek:" + seconds + "\">" + match + "</a>"
        })
    }
    return parts.join("")
}

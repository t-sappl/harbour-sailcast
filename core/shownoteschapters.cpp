// SPDX-License-Identifier: GPL-3.0-or-later
#include "shownoteschapters.h"

#include <QHash>
#include <QRegularExpression>
#include <QVector>

namespace {

// One time stamp: M:SS, MM:SS, MMM:SS (up to 199:59) or H:MM:SS, optional
// milliseconds; not part of a longer number, not followed by "Uhr", "h",
// "AM"/"PM", "min", "Minuten" or a full stop (a date, the end of a sentence)
const QString kStamp = QStringLiteral(
    "(?<![\\d:.,])\\d{1,3}:[0-5]\\d(?::[0-5]\\d)?(?:[.,]\\d{1,3})?(?![\\d:])"
    "(?!\\s*(?:[Uu]hr\\b|h\\b|AM\\b|PM\\b|[Mm]in\\b|[Mm]inuten\\b))(?!\\.(?!\\d))");
// Dashes: - en dash, em dash
const QString kDash = QStringLiteral("[-\\x{2013}\\x{2014}]");

struct Candidate {
    int seconds;    // no default: a C++11 aggregate for {secs, title}
    QString title;
};

// Seconds of a time stamp, -1 if out of range
int secondsOf(const QString &stamp)
{
    QString s = stamp;
    const int frac = s.indexOf(QRegularExpression(QStringLiteral("[.,]\\d{1,3}$")));
    if (frac > 0) {
        s.truncate(frac);
    }
    const QStringList parts = s.split(QLatin1Char(':'));
    if (parts.size() == 2) {
        const int m = parts.at(0).toInt();
        const int sec = parts.at(1).toInt();
        if (m > 199 || sec > 59) {
            return -1;
        }
        return m * 60 + sec;
    }
    if (parts.size() == 3) {
        const int h = parts.at(0).toInt();
        const int m = parts.at(1).toInt();
        const int sec = parts.at(2).toInt();
        if (h > 99 || m > 59 || sec > 59) {
            return -1;
        }
        return h * 3600 + m * 60 + sec;
    }
    return -1;
}

QString cleanTitle(QString title)
{
    static const QRegularExpression lead(QStringLiteral("^[\\s\\-\\x{2013}\\x{2014}:|.)\\]*_]+"));
    static const QRegularExpression trail(QStringLiteral("[\\s\\-\\x{2013}\\x{2014}:|(\\[*_]+$"));
    title.remove(lead);
    title.remove(trail);
    return title.simplified();
}

// Without URLs at least two letters or digits, one of them a letter
bool validTitle(const QString &title)
{
    static const QRegularExpression url(QStringLiteral("(?:https?://|www\\.)\\S+"),
                                        QRegularExpression::CaseInsensitiveOption);
    QString rest = title;
    rest.remove(url);
    int alnum = 0;
    bool letter = false;
    for (const QChar c : rest) {
        if (c.isLetterOrNumber()) {
            ++alnum;
        }
        if (c.isLetter()) {
            letter = true;
        }
    }
    return alnum >= 2 && letter;
}

// The longest series of rising times (a stray time - a typo, the list
// repeated - drops out instead of blocking every chapter after it)
QVector<Candidate> longestRising(const QVector<Candidate> &list)
{
    const int n = list.size();
    if (n == 0) {
        return {};
    }
    QVector<int> length(n, 1);
    QVector<int> previous(n, -1);
    int best = 0;
    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < i; ++j) {
            if (list.at(j).seconds < list.at(i).seconds && length.at(j) + 1 > length.at(i)) {
                length[i] = length.at(j) + 1;
                previous[i] = j;
            }
        }
        if (length.at(i) > length.at(best)) {
            best = i;
        }
    }
    QVector<Candidate> result;
    for (int i = best; i >= 0; i = previous.at(i)) {
        result.prepend(list.at(i));
    }
    return result;
}

// (344, review) One pass over all entities (a decoded "&" is not decoded
// again), named ones from a table of the usual Latin-1 and typographic ones
QString decodeEntities(const QString &text)
{
    static QHash<QString, uint> named;
    if (named.isEmpty()) {
        const struct { const char *name; uint code; } table[] = {
            {"nbsp", 0x20}, {"amp", '&'}, {"lt", '<'}, {"gt", '>'}, {"quot", '"'}, {"apos", '\''},
            {"ndash", 0x2013}, {"mdash", 0x2014}, {"bull", 0x2022}, {"middot", 0xB7}, {"hellip", 0x2026},
            {"lsquo", 0x2018}, {"rsquo", 0x2019}, {"sbquo", 0x201A}, {"ldquo", 0x201C}, {"rdquo", 0x201D},
            {"bdquo", 0x201E}, {"laquo", 0xAB}, {"raquo", 0xBB}, {"lsaquo", 0x2039}, {"rsaquo", 0x203A},
            {"auml", 0xE4}, {"ouml", 0xF6}, {"uuml", 0xFC}, {"Auml", 0xC4}, {"Ouml", 0xD6}, {"Uuml", 0xDC},
            {"szlig", 0xDF}, {"eacute", 0xE9}, {"egrave", 0xE8}, {"ecirc", 0xEA}, {"aacute", 0xE1},
            {"agrave", 0xE0}, {"acirc", 0xE2}, {"oacute", 0xF3}, {"ocirc", 0xF4}, {"uacute", 0xFA},
            {"iacute", 0xED}, {"ccedil", 0xE7}, {"ntilde", 0xF1}, {"Eacute", 0xC9}, {"aring", 0xE5},
            {"oslash", 0xF8}, {"aelig", 0xE6}, {"euro", 0x20AC}, {"copy", 0xA9}, {"reg", 0xAE},
            {"trade", 0x2122}, {"deg", 0xB0}, {"times", 0xD7}, {"shy", 0xAD}, {"thinsp", 0x2009},
            {"ensp", 0x2002}, {"emsp", 0x2003}, {"rarr", 0x2192}, {"larr", 0x2190},
        };
        for (const auto &e : table) {
            named.insert(QString::fromLatin1(e.name), e.code);
        }
    }
    static const QRegularExpression entity(QStringLiteral("&(#[xX][0-9a-fA-F]{1,6}|#\\d{1,7}|[a-zA-Z]{2,8});"));
    QString result;
    int last = 0;
    QRegularExpressionMatchIterator it = entity.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        const QString code = m.captured(1);
        uint value = 0;
        bool ok = false;
        if (code.startsWith(QLatin1String("#x")) || code.startsWith(QLatin1String("#X"))) {
            value = code.mid(2).toUInt(&ok, 16);
        } else if (code.startsWith(QLatin1Char('#'))) {
            value = code.mid(1).toUInt(&ok);
        } else {
            ok = named.contains(code);
            value = named.value(code);
        }
        // Surrogate halves and values beyond Unicode are not characters
        if (!ok || value == 0 || value >= 0x110000 || (value >= 0xD800 && value <= 0xDFFF)) {
            continue;
        }
        result += text.midRef(last, m.capturedStart() - last);
        result += QString::fromUcs4(&value, 1);
        last = m.capturedEnd();
    }
    result += text.midRef(last);
    return result;
}

} // namespace

namespace ShownotesChapters {

QStringList lines(const QString &notes)
{
    QString text = notes;
    // HTML: block ends and line breaks become new lines, the rest of the
    // tags goes
    static const QRegularExpression breaks(QStringLiteral(
        "<\\s*br\\s*/?\\s*>|<\\s*/\\s*(?:p|li|div|h[1-6]|tr|ul|ol|blockquote)\\s*>|<\\s*(?:p|li|div|tr|ul|ol)\\b[^>]*>"),
        QRegularExpression::CaseInsensitiveOption);
    // (344, review) Only real tags: "<3" or "a > b" in a plain text stay
    static const QRegularExpression tags(QStringLiteral("<[a-zA-Z/!][^>]*>"));
    text.replace(breaks, QStringLiteral("\n"));
    text.remove(tags);
    text = decodeEntities(text);
    // (344, review) Markdown links: "[00:00](https://…?t=0) Intro",
    // "* [00:00 Intro](https://…)" - the link text stays
    static const QRegularExpression markdownLink(QStringLiteral("\\[([^\\]\\n]*)\\]\\([^)\\s]*\\)"));
    text.replace(markdownLink, QStringLiteral("\\1"));
    text.replace(QStringLiteral("\r\n"), QStringLiteral("\n"));
    text.replace(QLatin1Char('\r'), QLatin1Char('\n'));
    // "00:00 Intro | 05:20 News": "|" splits only in front of a time stamp
    static const QRegularExpression pipe(QStringLiteral("\\s*\\|\\s*(?=[(\\[]?\\s*") + kStamp + QLatin1Char(')'),
                                         QRegularExpression::UseUnicodePropertiesOption);
    text.replace(pipe, QStringLiteral("\n"));
    // (344, review) and after a stamp in brackets: "Intro (00:00) | News (05:20)"
    static const QRegularExpression pipeAfter(QStringLiteral("(?<=\\d\\d[)\\]])\\s*\\|\\s*"));
    text.replace(pipeAfter, QStringLiteral("\n"));
    QStringList result;
    const QStringList raw = text.split(QLatin1Char('\n'));
    for (const QString &line : raw) {
        // (344, review) simplified(): every kind of space (thin, no-break)
        // becomes one plain space - no runs for the patterns to chew on
        const QString t = line.simplified();
        if (!t.isEmpty()) {
            result.append(t);
        }
    }
    return result;
}

QList<ParsedChapter> parse(const QString &notes, int durationSeconds, QStringList *log)
{
    // At the start: optional bullet, number ("1.", "1)", "#2", "Kapitel 1:"),
    // the stamp (in brackets or bold), optional "– end stamp", the title
    static const QRegularExpression prefix(
        QStringLiteral("^\\s*(?:[^\\p{L}\\p{N}\\s(\\[#]{1,4}\\s*)?"
                       "(?:(?:\\d{1,3}[.)]|#\\d{1,3}|(?i:kapitel|chapter|teil|part)\\s*\\d{1,3}\\s*[:.]?)\\s*)?"
                       "[(\\[]?\\s*[*_]*\\s*(") + kStamp + QStringLiteral(")\\s*[*_]*\\s*(?:[)\\]]\\s*)?"
                       "(?:") + kDash + QStringLiteral("\\s*[(\\[]?\\s*(?:") + kStamp + QStringLiteral(")\\s*[)\\]]?)?"
                       "(.*)$"), QRegularExpression::UseUnicodePropertiesOption);
    // At the end: the title, then the stamp in brackets or after a separator
    // (" – ", " | ", ":"), optional "– end stamp"
    static const QRegularExpression suffix(
        QStringLiteral("^(.*?)\\s*(?:[(\\[]\\s*(") + kStamp + QStringLiteral(")(?:\\s*") + kDash
        + QStringLiteral("\\s*(?:") + kStamp + QStringLiteral("))?\\s*[)\\]]"
        "|(?:\\s[-\\x{2013}\\x{2014}|]|:)\\s*(") + kStamp + QStringLiteral(")(?:\\s*") + kDash
        + QStringLiteral("\\s*(?:") + kStamp + QStringLiteral("))?)\\s*$"), QRegularExpression::UseUnicodePropertiesOption);

    const QStringList all = lines(notes);
    QVector<Candidate> starts;
    QVector<Candidate> ends;
    int droppedTitle = 0;
    int droppedLength = 0;
    for (const QString &line : all) {
        const QRegularExpressionMatch p = prefix.match(line);
        if (p.hasMatch()) {
            const int secs = secondsOf(p.captured(1));
            // (344, review) "00:00 Intro (05:00)": the bracketed stamp at the end
            // is not part of the title
            static const QRegularExpression trailingStamp(QStringLiteral("\\s*[(\\[]\\s*") + kStamp + QStringLiteral("\\s*[)\\]]\\s*$"),
                                                          QRegularExpression::UseUnicodePropertiesOption);
            QString rawTitle = p.captured(2);
            rawTitle.remove(trailingStamp);
            const QString title = cleanTitle(rawTitle);
            if (secs < 0) {
                // out of range: not a chapter
            } else if (!validTitle(title)) {
                ++droppedTitle;
            } else if (durationSeconds > 0 && secs >= durationSeconds) {
                ++droppedLength;
            } else {
                starts.append({secs, title});
            }
        }
        const QRegularExpressionMatch s = suffix.match(line);
        if (s.hasMatch()) {
            const QString stamp = s.captured(2).isEmpty() ? s.captured(3) : s.captured(2);
            const int secs = secondsOf(stamp);
            const QString title = cleanTitle(s.captured(1));
            if (secs < 0) {
                // out of range
            } else if (!validTitle(title)) {
                ++droppedTitle;
            } else if (durationSeconds > 0 && secs >= durationSeconds) {
                ++droppedLength;
            } else {
                ends.append({secs, title});
            }
        }
    }
    // (344, review) The search below is quadratic: a sane limit for very long
    // notes (no real chapter list is that long)
    if (starts.size() > 500 || ends.size() > 500) {
        if (log) {
            log->append(QStringLiteral("only the first 500 stamps per group"));
        }
        starts.resize(qMin(starts.size(), 500));
        ends.resize(qMin(ends.size(), 500));
    }
    const QVector<Candidate> startChain = longestRising(starts);
    const QVector<Candidate> endChain = longestRising(ends);
    const bool useEnds = endChain.size() > startChain.size();
    const QVector<Candidate> &chain = useEnds ? endChain : startChain;
    if (log) {
        log->append(QStringLiteral("%1 lines, %2 stamps at the start (%3 rising), %4 at the end (%5 rising)")
                        .arg(all.size()).arg(starts.size()).arg(startChain.size())
                        .arg(ends.size()).arg(endChain.size()));
        if (droppedTitle > 0 || droppedLength > 0) {
            log->append(QStringLiteral("dropped: %1 without a title, %2 after the end").arg(droppedTitle).arg(droppedLength));
        }
    }
    QList<ParsedChapter> result;
    if (chain.size() < 3) {
        if (log && !chain.isEmpty()) {
            log->append(QStringLiteral("no chapters: fewer than 3"));
        }
        return result;
    }
    if (chain.first().seconds > 600) {
        if (log) {
            log->append(QStringLiteral("no chapters: the first is after 10 minutes"));
        }
        return result;
    }
    for (const Candidate &c : chain) {
        ParsedChapter chapter;
        chapter.startMs = qint64(c.seconds) * 1000;
        chapter.title = c.title;
        result.append(chapter);
    }
    if (log) {
        log->append(QStringLiteral("%1 chapters from the stamps at the %2").arg(result.size())
                        .arg(useEnds ? QStringLiteral("end") : QStringLiteral("start")));
    }
    return result;
}

}

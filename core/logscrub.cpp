// SPDX-License-Identifier: GPL-3.0-or-later
#include "logscrub.h"

#include <algorithm>
#include <QHash>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>

namespace {

// Replacements go in as private-use placeholders first, so a later name
// ("Podcast", a title word) cannot match inside "Podcast #12"
const QChar Open(0xE000);
const QChar Close(0xE001);

QString placeholder(QStringList &found, const QString &replacement)
{
    found << replacement;
    return QString(Open) + QString::number(found.size() - 1) + QString(Close);
}

bool isWordChar(const QChar &c)
{
    return c.isLetterOrNumber() || c == QLatin1Char('_');
}

// Character runs of the text, for a quick "can this name occur at all":
// ~24 000 episode titles would otherwise each be searched in the whole text
class Grams
{
public:
    explicit Grams(const QString &text) : m_text(text) {}
    bool mayContain(const QString &needle)
    {
        const int n = qMin(needle.length(), 8);
        QSet<uint> &set = m_sets[n];
        if (set.isEmpty() && m_text.length() >= n) {
            // hashes only (a collision just means one search more)
            set.reserve(m_text.length());
            for (int i = 0; i + n <= m_text.length(); ++i) {
                set.insert(qHash(m_text.midRef(i, n)));
            }
        }
        // start, middle and end: addresses share "https://", titles a prefix
        const int len = needle.length();
        return set.contains(qHash(needle.leftRef(n)))
               && set.contains(qHash(needle.midRef((len - n) / 2, n)))
               && set.contains(qHash(needle.rightRef(n)));
    }
private:
    QString m_text;
    QHash<int, QSet<uint> > m_sets;
};

// Domain kept, the rest of a web address dropped: "https://parlando.podigee.io/
// feed/mp3" -> "https://…podigee.io/…" (a subdomain often is the podcast)
QString shortAddress(const QString &scheme, const QString &host, const QString &rest)
{
    QString h = host;
    const int at = h.lastIndexOf(QLatin1Char('@'));
    if (at >= 0) {
        h = h.mid(at + 1);
    }
    QString port;
    const int colon = h.indexOf(QLatin1Char(':'));
    if (colon >= 0) {
        port = h.mid(colon);
        h = h.left(colon);
    }
    const QStringList labels = h.split(QLatin1Char('.'));
    bool numeric = true;
    for (const QString &l : labels) {
        bool ok = false;
        l.toInt(&ok);
        numeric = numeric && ok;
    }
    if (!numeric && labels.size() > 2) {
        // "co.uk", "com.au": three labels stay
        const bool shortSecond = labels.at(labels.size() - 2).length() <= 3 && labels.last().length() == 2;
        const int keep = shortSecond ? 3 : 2;
        if (labels.size() > keep) {
            h = QStringLiteral("…") + QStringList(labels.mid(labels.size() - keep)).join(QLatin1Char('.'));
        }
    }
    const bool hasRest = !rest.isEmpty() && rest != QLatin1String("/");
    return scheme + QStringLiteral("://") + h + port + (hasRest ? QStringLiteral("/…") : rest);
}

// Folders whose names say nothing about the user
bool isStandardFolder(const QString &segment)
{
    static const QSet<QString> names {
        QStringLiteral("Music"), QStringLiteral("Videos"), QStringLiteral("Downloads"),
        QStringLiteral("Documents"), QStringLiteral("Pictures"), QStringLiteral("Podcasts"),
        QStringLiteral("Sailcast"), QStringLiteral(".local"), QStringLiteral("share"),
        QStringLiteral(".cache"), QStringLiteral(".config"), QStringLiteral("harbour-sailcast"),
        QStringLiteral("android_storage")
    };
    return names.contains(segment)
           || (segment.startsWith(Open) && segment.endsWith(Close) && segment.count(Open) == 1);
}

// Words of the app's own log lines: a podcast called "Update" must not turn
// "Update: … failed" into "Podcast #3: … failed"
// Only names with a letter: a title "2026" or "100" would hit every
// timestamp and position
bool hasLetter(const QString &name)
{
    for (const QChar &c : name) {
        if (c.isLetter()) {
            return true;
        }
    }
    return false;
}

bool isLogWord(const QString &name)
{
    static const QSet<QString> words {
        QStringLiteral("Update"), QStringLiteral("Player"), QStringLiteral("Playback"),
        QStringLiteral("Playing"), QStringLiteral("Episode"), QStringLiteral("Podcast"),
        QStringLiteral("Podcasts"), QStringLiteral("Archive"), QStringLiteral("Seek"),
        QStringLiteral("Resume"), QStringLiteral("Load"), QStringLiteral("Reload"),
        QStringLiteral("Video"), QStringLiteral("Watchdog"), QStringLiteral("Share"),
        QStringLiteral("Sailcast"), QStringLiteral("Database"), QStringLiteral("Listening"),
        QStringLiteral("Version"), QStringLiteral("Error"), QStringLiteral("Timeout"),
        QStringLiteral("Download"), QStringLiteral("Downloads"), QStringLiteral("Music"),
        QStringLiteral("Videos"), QStringLiteral("Feed"), QStringLiteral("Search"),
        QStringLiteral("Sync"), QStringLiteral("Not"), QStringLiteral("End"),
        QStringLiteral("Unknown"), QStringLiteral("File"), QStringLiteral("Image"),
        QStringLiteral("Podnews"), QStringLiteral("News"), QStringLiteral("Found"),
        QStringLiteral("Gateway"), QStringLiteral("Media"), QStringLiteral("Stream"),
        QStringLiteral("Host"), QStringLiteral("Server"), QStringLiteral("Network"),
        QStringLiteral("Connection"), QStringLiteral("Removed"),
        QStringLiteral("Saved"), QStringLiteral("Source"), QStringLiteral("Original"),
        QStringLiteral("Chapters"), QStringLiteral("Bookmarks"), QStringLiteral("Playlist"),
        QStringLiteral("Resource"), QStringLiteral("Duplicate"), QStringLiteral("Unsubscribe")
    };
    if (name.contains(QLatin1Char(' '))) {
        return false;
    }
    // (review 371) a short lower-case word ("the", "source") is a word of
    // the text rather than a title; "freakshow", "logbuch:netzpolitik" stay names
    bool plainWord = name.length() <= 7;
    for (const QChar &c : name) {
        plainWord = plainWord && c.isLetter() && c.isLower();
    }
    return words.contains(name) || plainWord;
}

} // namespace

QString LogScrub::scrub(const QString &input, const QList<Name> &names, QStringList *used)
{
    // (review 371) placeholder characters of our own never come from the log
    QString text = input;
    text.remove(Open);
    text.remove(Close);
    QStringList found;

    // 1 + 2: known addresses and names, longest first (a title that contains
    // another one wins)
    QList<Name> sorted;
    sorted.reserve(names.size());
    for (const Name &n : names) {
        const QString t = n.text.trimmed();
        if (t.length() >= (n.address ? 6 : 3) && (n.address || (hasLetter(t) && !isLogWord(t)))) {
            Name copy = n;
            copy.text = t;
            sorted << copy;
        }
    }
    std::stable_sort(sorted.begin(), sorted.end(), [](const Name &a, const Name &b) {
        if (a.address != b.address) {
            return a.address;   // addresses before names
        }
        return a.text.length() > b.text.length();
    });
    Grams grams(text);
    QHash<QString, QString> tokenFor;   // same replacement, same placeholder
    for (const Name &n : sorted) {
        if (!grams.mayContain(n.text)) {
            continue;
        }
        int from = text.indexOf(n.text);
        while (from >= 0) {
            const int end = from + n.text.length();
            // (review 371) a host name ("sorf.at") not inside another one
            // ("orf.at" in "sorf.at"); a full address starts anywhere
            const bool hostBoundary = !n.address || n.text.contains(QStringLiteral("://")) || from == 0
                    || !(text.at(from - 1).isLetterOrNumber() || text.at(from - 1) == QLatin1Char('-'));
            const bool whole = (n.address && hostBoundary)
                    || !n.address && (((from == 0 || !isWordChar(text.at(from - 1)) || !isWordChar(n.text.at(0)))
                        && (end >= text.length() || !isWordChar(text.at(end)) || !isWordChar(n.text.at(n.text.length() - 1)))));
            if (whole) {
                QString &token = tokenFor[n.replacement];
                if (token.isEmpty()) {
                    token = placeholder(found, n.replacement);
                }
                // (review 371) an address goes with the rest of its URL
                // (".../wp-content/uploads/<episode slug>.jpg")
                int stop = end;
                if (n.address) {
                    static const QString endChars = QStringLiteral("\"'<>()[]");
                    while (stop < text.length() && !text.at(stop).isSpace()
                           && !endChars.contains(text.at(stop)) && text.at(stop) != Open) {
                        ++stop;
                    }
                }
                const QString piece = stop > end ? token + QStringLiteral("/…") : token;
                text.replace(from, stop - from, piece);
                from = text.indexOf(n.text, from + piece.length());
            } else {
                from = text.indexOf(n.text, end);
            }
        }
    }

    // 3: every other web address down to its domain
    static const QRegularExpression address(QStringLiteral("\\b(https?|ftp|rtsp)://([^/\\s\"'<>()\\[\\]]+)([^\\s\"'<>()\\[\\]]*)"),
                                            QRegularExpression::CaseInsensitiveOption);
    QString out;
    int last = 0;
    QRegularExpressionMatchIterator it = address.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        out += shortAddress(m.captured(1), m.captured(2), m.captured(3));
        last = m.capturedEnd();
    }
    out += text.mid(last);
    text = out;

    // 4: paths of the user's files: standard folders stay, the rest goes.
    // File names may contain spaces and brackets: a path ends at a quote, the
    // end of the line or " - " (error messages)
    static const QRegularExpression path(QStringLiteral(
        "(/home/[^/\\s\"']+|/run/media/[^/\\s\"']+/[^/\\s\"']+|/media/sdcard/[^/\\s\"']+)((?:/[^/\"'\\n]*)*)"));
    out.clear();
    last = 0;
    it = path.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        QString rest = m.captured(2);
        QString tail;
        // (review 371) a file name ends at its extension: ") at 12 s",
        // ", retrying" after it stay
        // - the first one in the last part of the path (review 371: "Radio.fm
        // Talks/ep.mp3" - a dot in a folder name is no extension)
        static const QRegularExpression extension(QStringLiteral("\\.[A-Za-z0-9]{2,5}(?=[)\\s,;]|$)"));
        QRegularExpressionMatchIterator exts = extension.globalMatch(rest);
        while (exts.hasNext()) {
            const QRegularExpressionMatch ext = exts.next();
            int stop = rest.indexOf(QLatin1Char(')'), ext.capturedEnd());
            if (stop < 0) {
                stop = rest.length();
            }
            if (rest.midRef(ext.capturedEnd(), stop - ext.capturedEnd()).contains(QLatin1Char('/'))) {
                continue;
            }
            tail = rest.mid(ext.capturedEnd());
            rest = rest.left(ext.capturedEnd());
            break;
        }
        const int dash = rest.indexOf(QStringLiteral(" - "));
        if (dash >= 0) {
            tail = rest.mid(dash) + tail;
            rest = rest.left(dash);
        }
        // "(file:///home/…/x.mp3)": the closing bracket of the message stays
        // (folder and file names may contain brackets of their own)
        while (rest.endsWith(QLatin1Char(')')) && rest.count(QLatin1Char(')')) > rest.count(QLatin1Char('('))) {
            tail.prepend(QLatin1Char(')'));
            rest.chop(1);
        }
        const QStringList segments = rest.split(QLatin1Char('/'), QString::SkipEmptyParts);
        const bool home = m.captured(1).startsWith(QLatin1String("/home/"));
        QString kept = home ? QStringLiteral("~") : QStringLiteral("<SD card>");
        for (int i = 0; i < segments.size(); ++i) {
            if (isStandardFolder(segments.at(i).trimmed())) {
                kept += QLatin1Char('/') + segments.at(i).trimmed();
            } else {
                kept += QStringLiteral("/…");
                break;
            }
        }
        out += kept + tail;
        last = m.capturedEnd();
    }
    out += text.mid(last);
    text = out;

    // 4b (review 371): host names without an address ("Host
    // parlando.podigee.io not found") down to the domain, like step 3
    static const QRegularExpression hostLine(QStringLiteral("\\b(Host )([A-Za-z0-9.-]+\\.[A-Za-z]{2,})"));
    out.clear();
    last = 0;
    it = hostLine.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString host = shortAddress(QStringLiteral("x"), m.captured(2), QString());
        out += m.captured(1) + host.mid(4);   // without "x://"
        last = m.capturedEnd();
    }
    out += text.mid(last);
    text = out;

    // 4c (review 371): what was searched at fyyd, and fyyd's own podcast ids
    // (review 371) up to the last " - " of the line - a term may contain one
    static const QRegularExpression fyydTerm(QStringLiteral("(fyyd: (?:search|lookup term|lookup)) [^\\n]* - ([^\\n]*)$"),
                                             QRegularExpression::MultilineOption);
    text.replace(fyydTerm, QStringLiteral("\\1 … - \\2"));
    static const QRegularExpression fyydBare(QStringLiteral("(fyyd: (?:search|lookup term|lookup)) (?!… - )[^\\n]+$"),
                                             QRegularExpression::MultilineOption);
    text.replace(fyydBare, QStringLiteral("\\1 …"));
    static const QRegularExpression fyydId(QStringLiteral("(fyyd: (?:similar|collections|podcast|episodes)) \\d+"));
    text.replace(fyydId, QStringLiteral("\\1 #…"));

    // 5: lines naming a folder that is no longer known (an unsubscribed
    // podcast's empty folder)
    static const QRegularExpression folderLine(QStringLiteral("(empty folder |the duplicate )([^\\n" "\\x{E000}" "]+)"));
    out.clear();
    last = 0;
    it = folderLine.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString name = m.captured(2);
        const QString bare = name.startsWith(QLatin1Char('"')) ? name.mid(1) : name;
        out += m.captured(1) + (bare.startsWith(QLatin1Char('~')) || bare.startsWith(QLatin1Char('<'))
                                ? name : QStringLiteral("…"));
        last = m.capturedEnd();
    }
    out += text.mid(last);
    text = out;

    // Placeholders back to their replacements
    static const QRegularExpression token(QStringLiteral("\\x{E000}(\\d+)\\x{E001}"));
    out.clear();
    last = 0;
    it = token.globalMatch(text);
    while (it.hasNext()) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const int index = m.captured(1).toInt();
        out += index >= 0 && index < found.size() ? found.at(index) : QString();
        last = m.capturedEnd();
    }
    out += text.mid(last);
    if (used) {
        *used = found;
    }
    return out;
}

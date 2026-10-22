// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedparser.h"

#include <QByteArray>
#include <QHash>
#include <QStringList>
#include <QXmlStreamAttributes>
#include <QXmlStreamReader>

#include "timeparse.h"

namespace {

const QString NsAtom = QStringLiteral("http://www.w3.org/2005/Atom");
const QString NsRss1 = QStringLiteral("http://purl.org/rss/1.0/");
const QString NsItunes = QStringLiteral("http://www.itunes.com/dtds/podcast-1.0.dtd");
const QString NsContent = QStringLiteral("http://purl.org/rss/1.0/modules/content/");
const QString NsMedia = QStringLiteral("http://search.yahoo.com/mrss/");
const QString NsDc = QStringLiteral("http://purl.org/dc/elements/1.1/");
const QString NsPodcast = QStringLiteral("https://podcastindex.org/namespace/1.0");

bool isPodlove(const QStringRef &ns)
{
    return ns.startsWith(QLatin1String("http://podlove.org/simple-chapters"));
}

// RSS elements have no namespace (RSS 2.0) or the RSS 1.0 namespace
bool isPlain(const QStringRef &ns)
{
    return ns.isEmpty() || ns == NsRss1;
}

bool isMedia(const QString &mimeType)
{
    return mimeType.startsWith(QLatin1String("audio/"))
            || mimeType.startsWith(QLatin1String("video/"))
            || mimeType.isEmpty()
            || mimeType == QLatin1String("application/octet-stream")
            || mimeType == QLatin1String("application/ogg");
}

QString readText(QXmlStreamReader &xml)
{
    return xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
}

void setIfEmpty(QString &target, const QString &value)
{
    if (target.isEmpty() && !value.isEmpty()) {
        target = value;
    }
}

} // namespace

namespace {

ParsedFeed parseOnce(const QByteArray &data, QString *error)
{
    ParsedFeed feed;
    QXmlStreamReader xml(data);
    QStringList stack;          // local names of the open elements
    bool inItem = false;
    ParsedEpisode item;
    QString channelImageUrl;    // <image><url> - fallback for the cover
    QString channelSummary;     // itunes:summary - fallback for the description

    while (!xml.atEnd()) {
        xml.readNext();

        if (xml.isEndElement()) {
            if (!stack.isEmpty()) {
                stack.removeLast();
            }
            const QStringRef name = xml.name();
            if (inItem && (name == QLatin1String("item") || name == QLatin1String("entry"))) {
                inItem = false;
                if (!item.url.isEmpty()) {
                    if (item.guid.isEmpty()) {
                        item.guid = item.url;
                    }
                    feed.episodes.append(item);
                }
            }
            continue;
        }
        if (!xml.isStartElement()) {
            continue;
        }

        const QStringRef ns = xml.namespaceUri();
        const QStringRef name = xml.name();
        const QXmlStreamAttributes attrs = xml.attributes();
        const QString parent = stack.isEmpty() ? QString() : stack.last();

        // --- Item / entry start ---
        if ((isPlain(ns) && name == QLatin1String("item"))
                || (ns == NsAtom && name == QLatin1String("entry"))) {
            inItem = true;
            item = ParsedEpisode();
            stack.append(name.toString());
            continue;
        }

        // --- Elements valid on both levels ---
        if (ns == NsAtom && name == QLatin1String("link")) {
            const QString rel = attrs.value(QLatin1String("rel")).toString();
            const QString href = attrs.value(QLatin1String("href")).toString();
            if (inItem && rel == QLatin1String("enclosure")) {
                const QString type = attrs.value(QLatin1String("type")).toString();
                if (item.url.isEmpty() && isMedia(type)) {
                    item.url = href;
                    item.mimeType = type;
                    item.fileSize = attrs.value(QLatin1String("length")).toLongLong();
                }
            } else if (rel.isEmpty() || rel == QLatin1String("alternate")) {
                setIfEmpty(inItem ? item.link : feed.link, href);
            }
            // rel="self", "payment", ... are ignored
            stack.append(name.toString());
            continue;
        }
        if (ns == NsItunes && name == QLatin1String("image")) {
            const QString href = attrs.value(QLatin1String("href")).toString();
            if (inItem) {
                setIfEmpty(item.imageUrl, href);
            } else if (!href.isEmpty()) {
                feed.coverUrl = href;   // preferred over <image><url>
            }
            stack.append(name.toString());
            continue;
        }

        if (inItem) {
            // --- Item level ---
            const bool itemChild = parent == QLatin1String("item") || parent == QLatin1String("entry");
            if (isPlain(ns) && name == QLatin1String("enclosure")) {
                const QString type = attrs.value(QLatin1String("type")).toString();
                if (item.url.isEmpty() && isMedia(type)) {
                    item.url = attrs.value(QLatin1String("url")).toString().trimmed();
                    item.mimeType = type;
                    item.fileSize = attrs.value(QLatin1String("length")).toLongLong();
                }
                stack.append(name.toString());
            } else if (ns == NsMedia && name == QLatin1String("content")) {
                const QString type = attrs.value(QLatin1String("type")).toString();
                if (item.url.isEmpty() && isMedia(type)) {
                    item.url = attrs.value(QLatin1String("url")).toString().trimmed();
                    item.mimeType = type;
                    item.fileSize = attrs.value(QLatin1String("fileSize")).toLongLong();
                    if (item.duration <= 0) {
                        item.duration = TimeParse::durationSeconds(
                                    attrs.value(QLatin1String("duration")).toString());
                    }
                }
                stack.append(name.toString());
            } else if (isPodlove(ns) && name == QLatin1String("chapter")) {
                const qint64 start = TimeParse::durationMs(attrs.value(QLatin1String("start")).toString());
                const QString title = attrs.value(QLatin1String("title")).toString();
                if (start >= 0 && !title.isEmpty()) {
                    ParsedChapter chapter;
                    chapter.startMs = start;
                    chapter.title = title;
                    chapter.href = attrs.value(QLatin1String("href")).toString();
                    chapter.image = attrs.value(QLatin1String("image")).toString();
                    item.chapters.append(chapter);
                }
                stack.append(name.toString());
            } else if (ns == NsPodcast && name == QLatin1String("chapters")) {
                setIfEmpty(item.chaptersUrl, attrs.value(QLatin1String("url")).toString());
                stack.append(name.toString());
            } else if (itemChild && (isPlain(ns) || ns == NsAtom) && name == QLatin1String("title")) {
                item.title = readText(xml);
            } else if (itemChild && isPlain(ns) && name == QLatin1String("link")) {
                setIfEmpty(item.link, readText(xml));
            } else if (itemChild && ((isPlain(ns) && name == QLatin1String("guid"))
                                     || (ns == NsAtom && name == QLatin1String("id")))) {
                item.guid = readText(xml);
            } else if (itemChild && ((isPlain(ns) && name == QLatin1String("description"))
                                     || (ns == NsAtom && name == QLatin1String("summary")))) {
                item.description = readText(xml);
            } else if (ns == NsItunes && name == QLatin1String("summary")) {
                setIfEmpty(item.description, readText(xml));
            } else if ((ns == NsContent && name == QLatin1String("encoded"))
                       || (ns == NsAtom && name == QLatin1String("content"))) {
                item.descriptionHtml = readText(xml);
            } else if (itemChild && ((isPlain(ns) && name == QLatin1String("pubDate"))
                                     || (ns == NsAtom && name == QLatin1String("published"))
                                     || (ns == NsDc && name == QLatin1String("date")))) {
                const qint64 date = TimeParse::pubDate(readText(xml));
                if (date > 0) {
                    item.published = date;
                }
            } else if (itemChild && ns == NsAtom && name == QLatin1String("updated")) {
                const qint64 date = TimeParse::pubDate(readText(xml));
                if (item.published <= 0) {
                    item.published = date;
                }
            } else if (ns == NsItunes && name == QLatin1String("duration")) {
                const int seconds = TimeParse::durationSeconds(readText(xml));
                if (seconds > 0) {
                    item.duration = seconds;
                }
            } else {
                stack.append(name.toString());
            }
        } else {
            // --- Channel / feed level ---
            const bool channelChild = parent == QLatin1String("channel") || parent == QLatin1String("feed");
            if (channelChild && (isPlain(ns) || ns == NsAtom) && name == QLatin1String("title")) {
                feed.title = readText(xml);
            } else if (channelChild && isPlain(ns) && name == QLatin1String("link")) {
                setIfEmpty(feed.link, readText(xml));
            } else if (channelChild && ((isPlain(ns) && name == QLatin1String("description"))
                                        || (ns == NsAtom && name == QLatin1String("subtitle")))) {
                feed.description = readText(xml);
            } else if (channelChild && ns == NsItunes && name == QLatin1String("summary")) {
                channelSummary = readText(xml);
            } else if (channelChild && ns == NsItunes && name == QLatin1String("author")) {
                feed.author = readText(xml);
            } else if (ns == NsItunes && name == QLatin1String("new-feed-url")) {
                feed.newUrl = readText(xml);
            } else if (parent == QLatin1String("image") && isPlain(ns) && name == QLatin1String("url")) {
                channelImageUrl = readText(xml);
            } else if (parent == QLatin1String("author") && ns == NsAtom && name == QLatin1String("name")) {
                setIfEmpty(feed.author, readText(xml));
            } else {
                stack.append(name.toString());
            }
        }
    }

    if (xml.hasError()) {
        *error = QStringLiteral("XML error in line %1: %2")
                .arg(xml.lineNumber()).arg(xml.errorString());
    }
    // An item that was open when the XML broke off is still usable
    if (inItem && !item.url.isEmpty()) {
        if (item.guid.isEmpty()) {
            item.guid = item.url;
        }
        feed.episodes.append(item);
    }

    setIfEmpty(feed.coverUrl, channelImageUrl);
    setIfEmpty(feed.description, channelSummary);
    return feed;
}

} // namespace

namespace FeedParser {

namespace {

struct NamedEntity
{
    const char *name;
    int codePoint;
};

// HTML entities that hand-made feeds use although XML does not define them:
// the whole Latin-1 range plus the usual typographic and a few other signs
const NamedEntity HtmlEntities[] = {
        { "nbsp", 160 },
        { "iexcl", 161 },
        { "cent", 162 },
        { "pound", 163 },
        { "curren", 164 },
        { "yen", 165 },
        { "brvbar", 166 },
        { "sect", 167 },
        { "uml", 168 },
        { "copy", 169 },
        { "ordf", 170 },
        { "laquo", 171 },
        { "not", 172 },
        { "shy", 173 },
        { "reg", 174 },
        { "macr", 175 },
        { "deg", 176 },
        { "plusmn", 177 },
        { "sup2", 178 },
        { "sup3", 179 },
        { "acute", 180 },
        { "micro", 181 },
        { "para", 182 },
        { "middot", 183 },
        { "cedil", 184 },
        { "sup1", 185 },
        { "ordm", 186 },
        { "raquo", 187 },
        { "frac14", 188 },
        { "frac12", 189 },
        { "frac34", 190 },
        { "iquest", 191 },
        { "Agrave", 192 },
        { "Aacute", 193 },
        { "Acirc", 194 },
        { "Atilde", 195 },
        { "Auml", 196 },
        { "Aring", 197 },
        { "AElig", 198 },
        { "Ccedil", 199 },
        { "Egrave", 200 },
        { "Eacute", 201 },
        { "Ecirc", 202 },
        { "Euml", 203 },
        { "Igrave", 204 },
        { "Iacute", 205 },
        { "Icirc", 206 },
        { "Iuml", 207 },
        { "ETH", 208 },
        { "Ntilde", 209 },
        { "Ograve", 210 },
        { "Oacute", 211 },
        { "Ocirc", 212 },
        { "Otilde", 213 },
        { "Ouml", 214 },
        { "times", 215 },
        { "Oslash", 216 },
        { "Ugrave", 217 },
        { "Uacute", 218 },
        { "Ucirc", 219 },
        { "Uuml", 220 },
        { "Yacute", 221 },
        { "THORN", 222 },
        { "szlig", 223 },
        { "agrave", 224 },
        { "aacute", 225 },
        { "acirc", 226 },
        { "atilde", 227 },
        { "auml", 228 },
        { "aring", 229 },
        { "aelig", 230 },
        { "ccedil", 231 },
        { "egrave", 232 },
        { "eacute", 233 },
        { "ecirc", 234 },
        { "euml", 235 },
        { "igrave", 236 },
        { "iacute", 237 },
        { "icirc", 238 },
        { "iuml", 239 },
        { "eth", 240 },
        { "ntilde", 241 },
        { "ograve", 242 },
        { "oacute", 243 },
        { "ocirc", 244 },
        { "otilde", 245 },
        { "ouml", 246 },
        { "divide", 247 },
        { "oslash", 248 },
        { "ugrave", 249 },
        { "uacute", 250 },
        { "ucirc", 251 },
        { "uuml", 252 },
        { "yacute", 253 },
        { "thorn", 254 },
        { "yuml", 255 },
        { "OElig", 338 },
        { "oelig", 339 },
        { "Scaron", 352 },
        { "scaron", 353 },
        { "Yuml", 376 },
        { "fnof", 402 },
        { "circ", 710 },
        { "tilde", 732 },
        { "ensp", 8194 },
        { "emsp", 8195 },
        { "thinsp", 8201 },
        { "zwnj", 8204 },
        { "zwj", 8205 },
        { "lrm", 8206 },
        { "rlm", 8207 },
        { "ndash", 8211 },
        { "mdash", 8212 },
        { "lsquo", 8216 },
        { "rsquo", 8217 },
        { "sbquo", 8218 },
        { "ldquo", 8220 },
        { "rdquo", 8221 },
        { "bdquo", 8222 },
        { "dagger", 8224 },
        { "Dagger", 8225 },
        { "bull", 8226 },
        { "hellip", 8230 },
        { "permil", 8240 },
        { "prime", 8242 },
        { "Prime", 8243 },
        { "lsaquo", 8249 },
        { "rsaquo", 8250 },
        { "oline", 8254 },
        { "frasl", 8260 },
        { "euro", 8364 },
        { "trade", 8482 },
        { "larr", 8592 },
        { "uarr", 8593 },
        { "rarr", 8594 },
        { "darr", 8595 },
        { "harr", 8596 },
        { "minus", 8722 },
        { "le", 8804 },
        { "ge", 8805 },
        { "ne", 8800 },
        { "asymp", 8776 },
        { "infin", 8734 },
        { "hearts", 9829 },
        { "alpha", 945 },
        { "beta", 946 },
        { "gamma", 947 },
        { "delta", 948 },
        { "mu", 956 },
        { "pi", 960 },
        { "Omega", 937 },
        { "check", 10003 },
        { nullptr, 0 }
};

int htmlEntity(const QByteArray &name)
{
    // Built once; function-local statics are initialized thread-safely
    static const QHash<QByteArray, int> table = []() {
        QHash<QByteArray, int> result;
        for (int i = 0; HtmlEntities[i].name; ++i) {
            result.insert(QByteArray(HtmlEntities[i].name), HtmlEntities[i].codePoint);
        }
        return result;
    }();
    return table.value(name, 0);
}

bool isXmlEntity(const QByteArray &name)
{
    return name == "amp" || name == "lt" || name == "gt" || name == "quot" || name == "apos";
}

bool isNameChar(char c)
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9');
}

bool isValidNumericReference(const QByteArray &body)
{
    // body is what follows "&#", without the ";"
    if (body.isEmpty()) {
        return false;
    }
    bool ok = false;
    const uint value = (body.at(0) == 'x' || body.at(0) == 'X') ? body.mid(1).toUInt(&ok, 16)
                                                               : body.toUInt(&ok, 10);
    // XML 1.0 Char production
    return ok && (value == 0x9 || value == 0xA || value == 0xD
                  || (value >= 0x20 && value <= 0xD7FF)
                  || (value >= 0xE000 && value <= 0xFFFD)
                  || (value >= 0x10000 && value <= 0x10FFFF));
}

// Copies data[from...] up to and including the terminator; returns the
// index after it (or the end if the terminator is missing)
int copyThrough(const QByteArray &data, int from, const char *terminator, QByteArray &out)
{
    const int end = data.indexOf(terminator, from);
    const int stop = end < 0 ? data.size() : end + static_cast<int>(qstrlen(terminator));
    out.append(data.constData() + from, stop - from);
    return stop;
}

const int MaxEntityNameLength = 32;

} // namespace

QByteArray sanitize(const QByteArray &data)
{
    QByteArray out;
    out.reserve(data.size() + data.size() / 50);
    const int size = data.size();
    int i = 0;
    while (i < size) {
        const char c = data.at(i);
        if (c == '<') {
            if (data.mid(i, 9) == "<![CDATA[") {
                i = copyThrough(data, i, "]]>", out);
                continue;
            }
            if (data.mid(i, 4) == "<!--") {
                i = copyThrough(data, i, "-->", out);
                continue;
            }
            out.append(c);
            ++i;
            continue;
        }
        const uchar u = static_cast<uchar>(c);
        if (u < 0x20 && c != '\t' && c != '\n' && c != '\r') {
            ++i;    // control character forbidden in XML
            continue;
        }
        if (c != '&') {
            out.append(c);
            ++i;
            continue;
        }

        // An entity or character reference: find the ";"
        int j = i + 1;
        const bool numeric = j < size && data.at(j) == '#';
        if (numeric) {
            ++j;
        }
        const int nameStart = j;
        while (j < size && j - nameStart <= MaxEntityNameLength
               && (isNameChar(data.at(j)) || (numeric && j == nameStart && (data.at(j) == 'x' || data.at(j) == 'X')))) {
            ++j;
        }
        const bool terminated = j < size && data.at(j) == ';' && j > nameStart;
        const QByteArray name = data.mid(nameStart, j - nameStart);

        if (terminated && numeric && isValidNumericReference(name)) {
            out.append(data.constData() + i, j + 1 - i);   // keep as is
            i = j + 1;
        } else if (terminated && !numeric && isXmlEntity(name)) {
            out.append(data.constData() + i, j + 1 - i);
            i = j + 1;
        } else if (terminated && !numeric && htmlEntity(name) > 0) {
            out.append("&#");
            out.append(QByteArray::number(htmlEntity(name)));
            out.append(';');
            i = j + 1;
        } else {
            // Bare "&" or unknown/invalid reference: escape the "&", the rest
            // is copied as normal text in the next iterations
            out.append("&amp;");
            ++i;
        }
    }
    return out;
}

ParsedFeed parse(const QByteArray &data, QString *error)
{
    QString firstError;
    ParsedFeed feed = parseOnce(data, &firstError);
    if (firstError.isEmpty()) {
        return feed;
    }

    // Broken XML: repair and try again (costs nothing for valid feeds)
    QString secondError;
    ParsedFeed repaired = parseOnce(sanitize(data), &secondError);
    if (secondError.isEmpty()) {
        repaired.repaired = true;
        return repaired;
    }

    // Still broken: return the more complete of the two partial results
    // ("repaired" stays false - the feed is incomplete)
    if (repaired.episodes.size() >= feed.episodes.size()) {
        if (error) {
            *error = secondError;
        }
        return repaired;
    }
    if (error) {
        *error = firstError;
    }
    return feed;
}

} // namespace FeedParser

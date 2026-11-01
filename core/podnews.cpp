// SPDX-License-Identifier: GPL-3.0-or-later
#include "podnews.h"

#include <algorithm>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QStringList>
#include <QTimer>
#include <QUrl>
#include <QXmlStreamReader>

#include "netutil.h"
#include "timeparse.h"

namespace {

const char FeedAddress[] = "https://podnews.net/sitemap/trailers.xml";
const int TimeoutMs = 30000;
const char NsItunes[] = "http://www.itunes.com/dtds/podcast-1.0.dtd";
const char NsContent[] = "http://purl.org/rss/1.0/modules/content/";

// HTML entities outside CDATA make the XML invalid for QXmlStreamReader
// (the description reads "History &middot; History Hit"): the common ones
// become numeric references before parsing
QByteArray numericEntities(const QByteArray &xml)
{
    static const char *const Names[][2] = {
        { "&middot;", "&#183;" }, { "&nbsp;", "&#160;" }, { "&mdash;", "&#8212;" }, { "&ndash;", "&#8211;" },
        { "&rsquo;", "&#8217;" }, { "&lsquo;", "&#8216;" }, { "&ldquo;", "&#8220;" }, { "&rdquo;", "&#8221;" },
        { "&hellip;", "&#8230;" }, { "&copy;", "&#169;" }, { "&eacute;", "&#233;" }, { "&uuml;", "&#252;" }
    };
    QByteArray result = xml;
    for (size_t i = 0; i < sizeof(Names) / sizeof(Names[0]); ++i) {
        result.replace(Names[i][0], Names[i][1]);
    }
    return result;
}

// Text of HTML: tags out, the usual entities decoded, spaces tidied
QString plainText(const QString &html)
{
    QString text = html;
    text.replace(QRegularExpression(QStringLiteral("<br\\s*/?>"), QRegularExpression::CaseInsensitiveOption),
                 QStringLiteral("\n"));
    text.remove(QRegularExpression(QStringLiteral("<[^>]*>")));
    text.replace(QStringLiteral("&middot;"), QStringLiteral("·"));
    text.replace(QStringLiteral("&nbsp;"), QStringLiteral(" "));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&rsquo;"), QStringLiteral("’"));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text;
}

// content:encoded: genre<br>Official website: …<br>Subscribe in all apps: …
// <br>the blurb<br>© author - only the blurb
QString blurb(const QString &html, const QString &genre)
{
    // Lines by <br> and by line breaks (195: the genre stayed in front of
    // the text on the phone - the parts are not always separated by <br>)
    const QStringList parts = plainText(html).split(QRegularExpression(QStringLiteral("[\\r\\n]+")));
    QStringList kept;
    for (int i = 0; i < parts.size(); ++i) {
        QString part = parts.at(i).simplified();
        // The links, wherever they stand
        part.remove(QRegularExpression(QStringLiteral("(Official website|Subscribe in all apps):\\s*\\S*"),
                                       QRegularExpression::CaseInsensitiveOption));
        part = part.simplified();
        if (part.isEmpty() || part == genre || part.startsWith(QStringLiteral("©"))) {
            continue;
        }
        kept.append(part);
    }
    QString text = kept.join(QLatin1Char(' ')).simplified();
    if (!genre.isEmpty() && text.startsWith(genre + QLatin1Char(' '))) {
        text = text.mid(genre.size() + 1);
    }
    return text;
}

} // namespace

Podnews::Podnews(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
    // Updated daily on the server; a few hours keep "Neu" fresh enough
    m_cache.setMaxAge(6 * 3600);
}

QString Podnews::feedUrl()
{
    return QString::fromLatin1(FeedAddress);
}

void Podnews::trailers()
{
    QByteArray body;
    if (m_cache.fresh(QUrl(feedUrl()), &body)) {
        QTimer::singleShot(0, this, [this, body]() { emit trailersReady(parse(body)); });
        return;
    }
    send(false);
}

void Podnews::send(bool retried)
{
    if (m_reply) {
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    QNetworkRequest request{QUrl(feedUrl())};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    QTimer::singleShot(TimeoutMs, reply, SLOT(abort()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, retried]() { onFinished(reply, retried); });
}

void Podnews::onFinished(QNetworkReply *reply, bool retried)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;
    }
    m_reply = nullptr;
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    if (reply->error() == QNetworkReply::NoError && status < 400) {
        const QList<PodnewsTrailer> list = parse(body);
        if (!list.isEmpty()) {
            m_cache.store(QUrl(feedUrl()), body);
            emit trailersReady(list);
            return;
        }
    }
    const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError;
    const bool retryable = !timedOut && (status == 0 || status == 429 || status >= 500);
    if (retryable && !retried) {
        QTimer::singleShot(2000, this, [this]() { send(true); });
        return;
    }
    // Offline or Podnews down: an older list is better than nothing
    QByteArray old;
    if (m_cache.stale(QUrl(feedUrl()), &old)) {
        emit trailersReady(parse(old));
        return;
    }
    QString message;
    if (timedOut) {
        message = QStringLiteral("no answer from Podnews (timeout)");
    } else if (status >= 400) {
        message = QStringLiteral("Podnews answered HTTP %1").arg(status);
    } else if (status > 0) {
        message = QStringLiteral("Podnews sent no readable list");
    } else {
        message = QStringLiteral("no connection to Podnews (network error %1)").arg(int(reply->error()));
    }
    qWarning("Podnews: %s", qPrintable(message));
    emit failed(message);
}

QList<PodnewsTrailer> Podnews::parse(const QByteArray &data)
{
    QList<PodnewsTrailer> list;
    QXmlStreamReader xml(numericEntities(data));
    PodnewsTrailer item;
    QString content;
    QString genre;
    bool inItem = false;
    while (!xml.atEnd()) {
        const QXmlStreamReader::TokenType token = xml.readNext();
        if (token == QXmlStreamReader::StartElement) {
            const QStringRef name = xml.name();
            const QStringRef ns = xml.namespaceUri();
            if (name == QLatin1String("item") && ns.isEmpty()) {
                inItem = true;
                item = PodnewsTrailer();
                content.clear();
                genre.clear();
                continue;
            }
            if (!inItem) {
                continue;
            }
            if (ns.isEmpty() && name == QLatin1String("title")) {
                item.title = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            } else if (ns.isEmpty() && name == QLatin1String("link")) {
                item.link = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            } else if (ns.isEmpty() && name == QLatin1String("guid")) {
                item.id = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            } else if (ns.isEmpty() && name == QLatin1String("pubDate")) {
                item.published = TimeParse::pubDate(xml.readElementText(QXmlStreamReader::IncludeChildElements));
            } else if (ns.isEmpty() && name == QLatin1String("description")) {
                // "History · History Hit": the genre, then the author
                // (195) Also with the entity left as text ("&amp;middot;"
                // decodes to "&middot;" only once)
                const QString text = plainText(xml.readElementText(QXmlStreamReader::IncludeChildElements));
                genre = text.split(QRegularExpression(QStringLiteral("\\s*(·|&middot;|&#183;)\\s*"))).first().trimmed();
            } else if (ns.isEmpty() && name == QLatin1String("source")) {
                item.feedUrl = xml.attributes().value(QStringLiteral("url")).toString().trimmed();
            } else if (ns.isEmpty() && name == QLatin1String("enclosure")) {
                item.audioUrl = xml.attributes().value(QStringLiteral("url")).toString().trimmed();
            } else if (ns == QLatin1String(NsContent) && name == QLatin1String("encoded")) {
                content = xml.readElementText(QXmlStreamReader::IncludeChildElements);
            } else if (ns == QLatin1String(NsItunes) && name == QLatin1String("author")) {
                item.author = xml.readElementText(QXmlStreamReader::IncludeChildElements).trimmed();
            } else if (ns == QLatin1String(NsItunes) && name == QLatin1String("image")) {
                item.imageUrl = xml.attributes().value(QStringLiteral("href")).toString().trimmed();
            } else if (ns == QLatin1String(NsItunes) && name == QLatin1String("duration")) {
                item.duration = TimeParse::durationSeconds(xml.readElementText(QXmlStreamReader::IncludeChildElements));
            }
        } else if (token == QXmlStreamReader::EndElement && inItem && xml.name() == QLatin1String("item")
                   && xml.namespaceUri().isEmpty()) {
            inItem = false;
            item.genre = genre;
            item.description = blurb(content, genre);
            // Only shows with their own feed can be heard and subscribed
            if (!item.feedUrl.isEmpty() && !item.title.isEmpty()) {
                list.append(item);
            }
        }
    }
    if (xml.hasError()) {
        qWarning("Podnews: XML error at line %lld: %s (%d items read)", xml.lineNumber(),
                 qPrintable(xml.errorString()), list.size());
    }
    std::stable_sort(list.begin(), list.end(), [](const PodnewsTrailer &a, const PodnewsTrailer &b) {
        return a.published > b.published;
    });
    // A show featured twice: once, the newest
    QList<PodnewsTrailer> unique;
    QStringList feeds;
    for (int i = 0; i < list.size(); ++i) {
        const QString key = list.at(i).feedUrl.toLower();
        if (!feeds.contains(key)) {
            feeds.append(key);
            unique.append(list.at(i));
        }
    }
    return unique;
}

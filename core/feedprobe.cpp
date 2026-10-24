// SPDX-License-Identifier: GPL-3.0-or-later
#include "feedprobe.h"

#include <algorithm>

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include "feedparser.h"
#include "netutil.h"

namespace {
const int TimeoutMs = 30000;
}

FeedProbe::FeedProbe(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

QString FeedProbe::normalize(const QString &url)
{
    QString text = url.trimmed();
    if (text.startsWith(QLatin1String("feed://"), Qt::CaseInsensitive)) {
        text = QStringLiteral("https://") + text.mid(7);
    } else if (text.startsWith(QLatin1String("feed:"), Qt::CaseInsensitive)) {
        text = text.mid(5);     // feed:https://...
    }
    if (!text.isEmpty() && !text.contains(QLatin1String("://"))) {
        text.prepend(QStringLiteral("https://"));
    }
    return text;
}

QStringList FeedProbe::discoverFeeds(const QByteArray &html, const QUrl &base)
{
    const QString text = QString::fromUtf8(html.left(512 * 1024));
    QRegularExpression linkTag(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression href(QStringLiteral("href\\s*=\\s*[\"']([^\"']+)[\"']"),
                            QRegularExpression::CaseInsensitiveOption);
    QStringList preferred;
    QStringList normal;
    QStringList last;
    QRegularExpressionMatchIterator it = linkTag.globalMatch(text);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        const QString lower = tag.toLower();
        if (!lower.contains(QLatin1String("alternate"))
                || !(lower.contains(QLatin1String("application/rss+xml"))
                     || lower.contains(QLatin1String("application/atom+xml")))) {
            continue;
        }
        const QRegularExpressionMatch m = href.match(tag);
        if (!m.hasMatch()) {
            continue;
        }
        QString value = m.captured(1);
        value.replace(QLatin1String("&amp;"), QLatin1String("&"));
        const QString url = base.resolved(QUrl(value)).toString();
        if (preferred.contains(url) || normal.contains(url) || last.contains(url)) {
            continue;
        }
        // Title and address tell a podcast feed (Podlove: "… (MPEG-4 AAC
        // Audio)", "/feed/mp3/") from a blog or comment feed
        static const char *const audio[] = { "audio", "podcast", "mp3", "m4a", "aac", "opus", "ogg", "itunes" };
        bool isAudio = false;
        for (const char *word : audio) {
            if (lower.contains(QLatin1String(word))) {
                isAudio = true;
                break;
            }
        }
        if (lower.contains(QLatin1String("comment")) || lower.contains(QLatin1String("kommentar"))) {
            last << url;
        } else if (isAudio) {
            preferred << url;
        } else {
            normal << url;
        }
    }
    // Links that look like a feed (as Sailfeed's FeedFinder): "/feed/",
    // "rss.xml", "?format=rss" - after the announced ones (102)
    QStringList linked;
    QRegularExpression anchor(QStringLiteral("<a\\b[^>]*href\\s*=\\s*[\"']([^\"'#]+)[\"']"),
                              QRegularExpression::CaseInsensitiveOption);
    QRegularExpression feedLike(QStringLiteral("(^|[/.?=&_-])(rss2?|atom|feeds?)([/.?#_-]|\\.xml|\\.rss|$)"
                                               "|\\.(rss|atom)$|/(rss|feed|index)\\.xml$"),
                                QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator ait = anchor.globalMatch(text);
    while (ait.hasNext()) {
        QString value = ait.next().captured(1);
        value.replace(QLatin1String("&amp;"), QLatin1String("&"));
        const QUrl resolved = base.resolved(QUrl(value));
        const QString path = resolved.path() + (resolved.hasQuery() ? QLatin1Char('?') + resolved.query() : QString());
        if (!feedLike.match(path).hasMatch()
                || (resolved.scheme() != QLatin1String("http") && resolved.scheme() != QLatin1String("https"))) {
            continue;
        }
        const QString url = resolved.toString();
        if (preferred.contains(url) || normal.contains(url) || last.contains(url) || linked.contains(url)
                || url == base.toString()) {
            continue;
        }
        const QString lower = url.toLower();
        if (lower.contains(QLatin1String("comment")) || lower.contains(QLatin1String("kommentar"))) {
            continue;
        }
        linked << url;
    }
    return preferred + normal + linked + last;
}

namespace {

// &amp; &quot; &#39; &#x27; ... in a title
QString decodeEntities(QString text)
{
    QRegularExpression numeric(QStringLiteral("&#(x[0-9a-fA-F]+|[0-9]+);"));
    QRegularExpressionMatch m;
    int from = 0;
    while ((m = numeric.match(text, from)).hasMatch()) {
        const QString code = m.captured(1);
        bool ok = false;
        const uint value = code.startsWith(QLatin1Char('x')) ? code.mid(1).toUInt(&ok, 16) : code.toUInt(&ok, 10);
        const QString replacement = ok && value > 0 && value < 0x110000 ? QString::fromUcs4(&value, 1) : QString();
        text.replace(m.capturedStart(0), m.capturedLength(0), replacement);
        from = m.capturedStart(0) + replacement.length();
    }
    text.replace(QLatin1String("&quot;"), QLatin1String("\""));
    text.replace(QLatin1String("&apos;"), QLatin1String("'"));
    text.replace(QLatin1String("&lt;"), QLatin1String("<"));
    text.replace(QLatin1String("&gt;"), QLatin1String(">"));
    text.replace(QLatin1String("&nbsp;"), QLatin1String(" "));
    text.replace(QLatin1String("&amp;"), QLatin1String("&"));
    return text.simplified();
}

} // namespace

QString FeedProbe::pageTitle(const QByteArray &html)
{
    const QString text = QString::fromUtf8(html.left(512 * 1024));
    // <meta property="og:title" content="..."> (attributes in any order)
    QRegularExpression meta(QStringLiteral("<meta\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression ogTitle(QStringLiteral("(property|name)\\s*=\\s*[\"'](og:title|twitter:title)[\"']"),
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpression content(QStringLiteral("content\\s*=\\s*(\"([^\"]*)\"|'([^']*)')"),
                               QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatchIterator it = meta.globalMatch(text);
    while (it.hasNext()) {
        const QString tag = it.next().captured(0);
        if (!ogTitle.match(tag).hasMatch()) {
            continue;
        }
        const QRegularExpressionMatch c = content.match(tag);
        if (c.hasMatch()) {
            const QString value = decodeEntities(c.captured(2).isEmpty() ? c.captured(3) : c.captured(2));
            if (!value.isEmpty()) {
                return value;
            }
        }
    }
    QRegularExpression title(QStringLiteral("<title[^>]*>([^<]*)</title>"), QRegularExpression::CaseInsensitiveOption);
    const QRegularExpressionMatch t = title.match(text);
    return t.hasMatch() ? decodeEntities(t.captured(1)) : QString();
}

QString FeedProbe::discoverFeed(const QByteArray &html, const QUrl &base)
{
    const QStringList feeds = discoverFeeds(html, base);
    return feeds.isEmpty() ? QString() : feeds.first();
}

void FeedProbe::check(const QString &url)
{
    if (m_reply) {
        QNetworkReply *old = m_reply;
        m_reply = nullptr;
        old->disconnect(this);
        old->abort();
        old->deleteLater();
    }
    m_result = FeedProbeResult();
    m_result.requestedUrl = url;
    m_discovered = false;
    m_candidates.clear();
    m_best = FeedProbeResult();
    m_hasBest = false;
    const QUrl target(normalize(url));
    if (!target.isValid() || target.host().isEmpty()
            || (target.scheme() != QLatin1String("http") && target.scheme() != QLatin1String("https"))) {
        m_result.status = FeedProbeResult::InvalidUrl;
        // Reported from the event loop, like every other result
        QTimer::singleShot(0, this, [this]() { emit finished(m_result); });
        return;
    }
    m_result.url = target.toString();
    start(target);
}

void FeedProbe::start(const QUrl &url)
{
    QNetworkRequest request(url);
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    // No credentials are sent here, so following redirects to other hosts
    // is harmless (unlike in FeedUpdater)
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_network->get(request);
    m_reply = reply;
    QTimer *timer = new QTimer(reply);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, reply, [this, reply]() {
        if (m_reply == reply) {
            m_result.status = FeedProbeResult::Timeout;
            m_result.error = QStringLiteral("timeout");
            reply->setProperty("timedOut", true);
        }
        reply->abort();
    });
    timer->start(TimeoutMs);
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { onFinished(reply); });
}

void FeedProbe::onFinished(QNetworkReply *reply)
{
    reply->deleteLater();
    if (reply != m_reply) {
        return;     // an older check
    }
    m_reply = nullptr;
    if (reply->property("timedOut").toBool()) {
        // One of a website's feeds is too slow: the next one, or the best so far
        if (!nextOrBest()) {
            finish();
        }
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_result.httpStatus = status;
    // A feed named by the website that does not load: the next one (102),
    // or the best one found so far
    if ((status >= 400 || reply->error() != QNetworkReply::NoError) && status != 401 && status != 403
            && nextOrBest()) {
        return;
    }
    // A website's feed with login while others remain: those first
    if ((status == 401 || status == 403) && m_discovered && (m_hasBest || !m_candidates.isEmpty())
            && nextOrBest()) {
        return;
    }
    if (status == 401 || status == 403) {
        m_result.status = FeedProbeResult::LoginRequired;
        finish();
        return;
    }
    if (status >= 400) {
        m_result.status = FeedProbeResult::HttpError;
        finish();
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        m_result.status = FeedProbeResult::NetworkError;
        m_result.error = reply->errorString();
        finish();
        return;
    }
    const QByteArray data = reply->readAll();
    QString parseError;
    const ParsedFeed feed = FeedParser::parse(data, &parseError);
    if (!feed.title.isEmpty() || !feed.episodes.isEmpty()) {
        m_result.status = FeedProbeResult::Ok;
        m_result.title = feed.title;
        m_result.author = feed.author;
        m_result.coverUrl = feed.coverUrl;
        m_result.episodes = feed.episodes.size();
        m_result.description = feed.description;
        // The newest few, so the user sees what the podcast is about (50)
        QList<ParsedEpisode> sorted = feed.episodes;
        std::sort(sorted.begin(), sorted.end(), [](const ParsedEpisode &a, const ParsedEpisode &b) {
            return a.published > b.published;
        });
        m_result.latest.clear();
        for (int i = 0; i < sorted.size() && i < 5; ++i) {
            FeedProbeResult::Latest l;
            l.title = sorted.at(i).title;
            l.published = sorted.at(i).published;
            l.duration = sorted.at(i).duration;
            m_result.latest.append(l);
        }
        if (!feed.newUrl.isEmpty()) {
            m_result.url = feed.newUrl;     // itunes:new-feed-url
        }
        if (m_discovered) {
            // A website's feed (102): keep the one with the most episodes,
            // then check the next
            if (!m_hasBest || m_result.episodes > m_best.episodes) {
                m_best = m_result;
                m_hasBest = true;
            }
            nextOrBest();
            return;
        }
        finish();
        return;
    }
    // A web page: its title for the search on the add page (103)
    if (!m_discovered && m_result.pageTitle.isEmpty()) {
        m_result.pageTitle = pageTitle(data);
    }
    // A website? Follow the feed it names, once
    if (!m_discovered) {
        m_candidates = discoverFeeds(data, reply->url()).mid(0, 8);
        if (!m_candidates.isEmpty()) {
            m_discovered = true;
            m_result.discovered = true;
            m_result.url = m_candidates.takeFirst();
            start(QUrl(m_result.url));
            return;
        }
    }
    // A candidate that is no feed at all: the next one, or the best so far
    if (nextOrBest()) {
        return;
    }
    m_result.status = FeedProbeResult::NotAFeed;
    finish();
}

void FeedProbe::finishDiscovered()
{
    const QString requested = m_result.requestedUrl;
    const QString title = m_result.pageTitle;
    m_result = m_best;
    m_result.requestedUrl = requested;
    m_result.pageTitle = title;
    m_result.discovered = true;
    finish();
}

bool FeedProbe::nextOrBest()
{
    if (!m_discovered) {
        return false;
    }
    if (tryNextCandidate()) {
        return true;
    }
    if (m_hasBest) {
        finishDiscovered();
    } else {
        // None of the website's feeds loads: "no podcast on this page"
        m_result.url = m_result.requestedUrl;
        m_result.status = FeedProbeResult::NotAFeed;
        m_result.error.clear();
        finish();
    }
    return true;
}

bool FeedProbe::tryNextCandidate()
{
    if (!m_discovered || m_candidates.isEmpty()) {
        return false;
    }
    m_result.url = m_candidates.takeFirst();
    m_result.httpStatus = 0;
    m_result.status = FeedProbeResult::NotAFeed;
    m_result.error.clear();
    start(QUrl(m_result.url));
    return true;
}

void FeedProbe::finish()
{
    emit finished(m_result);
}

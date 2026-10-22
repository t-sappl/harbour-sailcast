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

QString FeedProbe::discoverFeed(const QByteArray &html, const QUrl &base)
{
    const QString text = QString::fromUtf8(html.left(512 * 1024));
    QRegularExpression linkTag(QStringLiteral("<link\\b[^>]*>"), QRegularExpression::CaseInsensitiveOption);
    QRegularExpression href(QStringLiteral("href\\s*=\\s*[\"']([^\"']+)[\"']"),
                            QRegularExpression::CaseInsensitiveOption);
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
        if (m.hasMatch()) {
            QString value = m.captured(1);
            value.replace(QLatin1String("&amp;"), QLatin1String("&"));
            return base.resolved(QUrl(value)).toString();
        }
    }
    return QString();
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
        finish();
        return;
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    m_result.httpStatus = status;
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
        finish();
        return;
    }
    // A website? Follow the feed it names, once
    if (!m_discovered) {
        const QString found = discoverFeed(data, reply->url());
        if (!found.isEmpty()) {
            m_discovered = true;
            m_result.discovered = true;
            m_result.url = found;
            start(QUrl(found));
            return;
        }
    }
    m_result.status = FeedProbeResult::NotAFeed;
    finish();
}

void FeedProbe::finish()
{
    emit finished(m_result);
}

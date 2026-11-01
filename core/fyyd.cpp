// SPDX-License-Identifier: GPL-3.0-or-later
#include "fyyd.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QRegularExpression>
#include <QTimer>

#include "netutil.h"

namespace {

const char ApiBase[] = "https://api.fyyd.de/0.2/";
const int TimeoutMs = 30000;

// fyyd sends some numbers as strings ("episode_count": "921")
int intValue(const QJsonValue &value)
{
    if (value.isDouble()) {
        return int(value.toDouble());
    }
    if (value.isString()) {
        return value.toString().trimmed().toInt();
    }
    if (value.isBool()) {
        return value.toBool() ? 1 : 0;
    }
    return 0;
}

// fyyd sends titles with HTML entities ("Leben &amp; Liebe", 132)
QString decodeEntities(const QString &text)
{
    if (!text.contains(QLatin1Char('&'))) {
        return text;
    }
    QString result;
    result.reserve(text.size());
    int i = 0;
    while (i < text.size()) {
        const QChar c = text.at(i);
        const int end = c == QLatin1Char('&') ? text.indexOf(QLatin1Char(';'), i + 1) : -1;
        if (end > i && end - i <= 10) {
            const QString name = text.mid(i + 1, end - i - 1);
            QString replacement;
            if (name == QLatin1String("amp")) {
                replacement = QStringLiteral("&");
            } else if (name == QLatin1String("lt")) {
                replacement = QStringLiteral("<");
            } else if (name == QLatin1String("gt")) {
                replacement = QStringLiteral(">");
            } else if (name == QLatin1String("quot")) {
                replacement = QStringLiteral("\"");
            } else if (name == QLatin1String("apos")) {
                replacement = QStringLiteral("'");
            } else if (name == QLatin1String("nbsp")) {
                replacement = QStringLiteral(" ");
            } else if (name.startsWith(QLatin1Char('#'))) {
                bool ok = false;
                const uint code = name.startsWith(QLatin1String("#x"), Qt::CaseInsensitive)
                        ? name.mid(2).toUInt(&ok, 16) : name.mid(1).toUInt(&ok, 10);
                if (ok && code > 0 && code < 0x110000) {
                    replacement = QString::fromUcs4(&code, 1);
                }
            }
            if (!replacement.isEmpty()) {
                result += replacement;
                i = end + 1;
                continue;
            }
        }
        result += c;
        ++i;
    }
    return result;
}

QString text(const QJsonObject &o, const char *key)
{
    const QJsonValue v = o.value(QLatin1String(key));
    return v.isString() ? decodeEntities(v.toString().trimmed()) : QString();
}

// "2018-09-28T15:24:13+02:00" (also without zone = UTC)
qint64 unixTime(const QString &iso)
{
    if (iso.isEmpty()) {
        return 0;
    }
    QDateTime t = QDateTime::fromString(iso, Qt::ISODate);
    if (!t.isValid()) {
        // "2018-09-28 15:24:13"
        t = QDateTime::fromString(iso, QStringLiteral("yyyy-MM-dd HH:mm:ss"));
        t.setTimeSpec(Qt::UTC);
    }
    return t.isValid() ? t.toMSecsSinceEpoch() / 1000 : 0;
}

// An array that is either the value itself or one member of an object
// ("data": [...] or "data": {"podcasts": [...]}) - the examples of the
// documentation differ between endpoints
QJsonArray arrayIn(const QJsonValue &value, const char *key)
{
    if (value.isArray()) {
        return value.toArray();
    }
    return value.toObject().value(QLatin1String(key)).toArray();
}

bool hasNextPage(const QJsonObject &root)
{
    const QJsonValue next = root.value(QStringLiteral("meta")).toObject()
            .value(QStringLiteral("paging")).toObject().value(QStringLiteral("next_page"));
    return !next.isNull() && !next.isUndefined();
}

FyydPodcast podcastFrom(const QJsonObject &o)
{
    FyydPodcast p;
    p.id = intValue(o.value(QStringLiteral("id")));
    p.title = text(o, "title");
    p.subtitle = text(o, "subtitle");
    p.author = text(o, "author");
    p.url = text(o, "xmlURL");
    p.link = text(o, "htmlURL");
    p.imageUrl = text(o, "imgURL");
    p.thumbUrl = text(o, "smallImageURL");
    if (p.thumbUrl.isEmpty()) {
        p.thumbUrl = text(o, "thumbImageURL");
    }
    if (p.imageUrl.isEmpty()) {
        p.imageUrl = p.thumbUrl;
    }
    p.description = text(o, "description");
    p.language = text(o, "language").toLower();
    const QJsonArray categories = o.value(QStringLiteral("categories")).toArray();
    for (int i = 0; i < categories.size(); ++i) {
        const int id = intValue(categories.at(i));
        if (id > 0 && !p.categories.contains(id)) {
            p.categories.append(id);
        }
    }
    p.lastPub = unixTime(text(o, "lastpub"));
    p.episodeCount = intValue(o.value(QStringLiteral("episode_count")));
    if (p.episodeCount == 0) {
        p.episodeCount = intValue(o.value(QStringLiteral("count_episodes")));
    }
    if (p.episodeCount == 0) {
        p.episodeCount = intValue(o.value(QStringLiteral("stats")).toObject().value(QStringLiteral("episodecount")));
    }
    p.rank = intValue(o.value(QStringLiteral("rank")));
    p.fyydUrl = text(o, "url_fyyd");
    return p;
}

QList<FyydPodcast> podcastsFrom(const QJsonArray &array)
{
    QList<FyydPodcast> list;
    for (int i = 0; i < array.size(); ++i) {
        const FyydPodcast p = podcastFrom(array.at(i).toObject());
        // Without a feed address it cannot be subscribed
        if (p.id > 0 && !p.url.isEmpty()) {
            list.append(p);
        }
    }
    return list;
}

FyydCollection collectionFrom(const QJsonObject &o)
{
    FyydCollection c;
    c.id = intValue(o.value(QStringLiteral("id")));
    c.userId = intValue(o.value(QStringLiteral("user_id")));
    c.title = text(o, "title");
    c.description = text(o, "description");
    c.slug = text(o, "slug");
    c.imageUrl = text(o, "smallImageURL");
    if (c.imageUrl.isEmpty()) {
        c.imageUrl = text(o, "thumbImageURL");
    }
    if (c.imageUrl.isEmpty()) {
        c.imageUrl = text(o, "layoutImageURL");
    }
    c.url = text(o, "url");
    return c;
}

FyydCuration curationFrom(const QJsonObject &o)
{
    FyydCuration c;
    c.id = intValue(o.value(QStringLiteral("id")));
    c.userId = intValue(o.value(QStringLiteral("user_id")));
    c.title = text(o, "title");
    c.description = text(o, "description");
    c.slug = text(o, "slug");
    c.imageUrl = text(o, "smallImageURL");
    if (c.imageUrl.isEmpty()) {
        c.imageUrl = text(o, "thumbImageURL");
    }
    if (c.imageUrl.isEmpty()) {
        c.imageUrl = text(o, "layoutImageURL");
    }
    c.url = text(o, "url");
    c.feedUrl = text(o, "xmlURL");
    const QJsonValue isPublic = o.value(QStringLiteral("public"));
    c.isPublic = isPublic.isUndefined() || isPublic.isNull() || intValue(isPublic) != 0;
    return c;
}

FyydEpisode episodeFrom(const QJsonObject &o)
{
    FyydEpisode e;
    e.id = intValue(o.value(QStringLiteral("id")));
    e.podcastId = intValue(o.value(QStringLiteral("podcast_id")));
    e.title = text(o, "title");
    e.guid = text(o, "guid");
    e.link = text(o, "url");
    e.enclosure = text(o, "enclosure");
    e.contentType = text(o, "content_type");
    e.imageUrl = text(o, "imgURL");
    e.description = text(o, "description");
    e.published = unixTime(text(o, "pubdate"));
    e.duration = intValue(o.value(QStringLiteral("duration")));
    e.season = intValue(o.value(QStringLiteral("num_season")));
    e.episodeNumber = intValue(o.value(QStringLiteral("num_episode")));
    e.added = unixTime(text(o, "favedDate"));
    if (e.added <= 0) {
        e.added = unixTime(text(o, "inserted"));
    }
    return e;
}

} // namespace

Fyyd::Fyyd(QObject *parent)
    : QObject(parent)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

bool Fyyd::sameFeed(const QString &a, const QString &b)
{
    QString x = a.trimmed().toLower();
    QString y = b.trimmed().toLower();
    const QString https = QStringLiteral("https://");
    const QString http = QStringLiteral("http://");
    if (x.startsWith(https)) {
        x = x.mid(https.size());
    } else if (x.startsWith(http)) {
        x = x.mid(http.size());
    }
    if (y.startsWith(https)) {
        y = y.mid(https.size());
    } else if (y.startsWith(http)) {
        y = y.mid(http.size());
    }
    while (x.endsWith(QLatin1Char('/'))) {
        x.chop(1);
    }
    while (y.endsWith(QLatin1Char('/'))) {
        y.chop(1);
    }
    return !x.isEmpty() && x == y;
}

void Fyyd::cancelAll()
{
    QList<QNetworkReply *> replies;
    for (QSet<QNetworkReply *>::const_iterator it = m_replies.constBegin(); it != m_replies.constEnd(); ++it) {
        replies.append(*it);
    }
    m_replies.clear();
    for (int i = 0; i < replies.size(); ++i) {
        replies.at(i)->disconnect(this);
        replies.at(i)->abort();
        replies.at(i)->deleteLater();
    }
}

void Fyyd::get(const QString &path, const QUrlQuery &query, const QString &request, Handler handler)
{
    QUrl url(QString::fromLatin1(ApiBase) + path);
    url.setQuery(query);
    QByteArray body;
    if (m_cache.fresh(url, &body)) {
        // Always asynchronous, as an answer from the network
        QTimer::singleShot(0, this, [this, url, body, request, handler]() {
            deliver(url, body, request, handler, true);
        });
        return;
    }
    send(url, request, handler, false);
}

void Fyyd::send(const QUrl &url, const QString &request, Handler handler, bool retried)
{
    QNetworkRequest networkRequest(url);
    networkRequest.setRawHeader("User-Agent", NetworkAccess::userAgent());
    networkRequest.setRawHeader("Accept", "application/json");
    QNetworkReply *reply = m_network->get(networkRequest);
    m_replies.insert(reply);
    QTimer::singleShot(TimeoutMs, reply, SLOT(abort()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, url, request, handler, retried]() {
        onFinished(reply, url, request, handler, retried);
    });
}

void Fyyd::onFinished(QNetworkReply *reply, const QUrl &url, const QString &request, Handler handler, bool retried)
{
    reply->deleteLater();
    if (!m_replies.remove(reply)) {
        return;     // cancelled
    }
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = reply->readAll();
    // 404 = the podcast, category or list does not exist (fyyd sends no
    // body then) - an empty result, not an error; not cached
    if (status == 404) {
        emit received(url, body, false);
        handler(QJsonObject(), QJsonValue());
        return;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    const bool ok = reply->error() == QNetworkReply::NoError && status < 400
            && parseError.error == QJsonParseError::NoError && document.isObject();
    if (ok) {
        m_cache.store(url, body);
        deliver(url, body, request, handler, false);
        return;
    }
    const bool timedOut = reply->error() == QNetworkReply::OperationCanceledError;
    const bool retryable = !timedOut && (status == 0 || status == 429 || status >= 500);
    if (retryable && !retried) {
        // Once more after two seconds, as for Podcast Index
        QTimer::singleShot(2000, this, [this, url, request, handler]() { send(url, request, handler, true); });
        return;
    }
    // Offline or fyyd down: an older answer is better than nothing
    QByteArray old;
    if (m_cache.stale(url, &old)) {
        deliver(url, old, request, handler, true);
        return;
    }
    QString message;
    if (timedOut) {
        message = QStringLiteral("no answer from fyyd (timeout)");
    } else if (status > 0) {
        message = QStringLiteral("fyyd answered HTTP %1").arg(status);
        const QJsonObject errors = document.object().value(QStringLiteral("errors")).toObject();
        const QString detail = errors.value(QStringLiteral("message")).toString().trimmed();
        if (!detail.isEmpty()) {
            message += QStringLiteral(" (%1)").arg(detail.left(120));
        }
    } else {
        message = QStringLiteral("no connection to fyyd (network error %1)").arg(int(reply->error()));
    }
    qWarning("fyyd: %s - %s", qPrintable(request), qPrintable(message));
    emit failed(request, message);
}

void Fyyd::deliver(const QUrl &url, const QByteArray &body, const QString &request, Handler handler, bool fromCache)
{
    Q_UNUSED(request)
    emit received(url, body, fromCache);
    const QJsonObject root = QJsonDocument::fromJson(body).object();
    handler(root, root.value(QStringLiteral("data")));
}

namespace {
QList<FyydCategory> categoriesFrom(const QJsonValue &data)
{
    QList<FyydCategory> list;
    const QJsonArray top = data.toArray();
    for (int i = 0; i < top.size(); ++i) {
        const QJsonObject o = top.at(i).toObject();
        FyydCategory c;
        c.id = intValue(o.value(QStringLiteral("id")));
        c.slug = text(o, "slug");
        c.name = text(o, "name");
        c.nameDe = text(o, "name_de");
        if (c.id <= 0) {
            continue;
        }
        list.append(c);
        const QJsonArray subs = o.value(QStringLiteral("subcategories")).toArray();
        for (int j = 0; j < subs.size(); ++j) {
            const QJsonObject s = subs.at(j).toObject();
            FyydCategory sub;
            sub.id = intValue(s.value(QStringLiteral("id")));
            sub.parentId = c.id;
            sub.slug = text(s, "slug");
            sub.name = text(s, "name");
            sub.nameDe = text(s, "name_de");
            if (sub.id > 0) {
                list.append(sub);
            }
        }
    }
    return list;
}
} // namespace

void Fyyd::categories()
{
    get(QStringLiteral("categories"), QUrlQuery(), QStringLiteral("categories"),
        [this](const QJsonObject &, const QJsonValue &data) {
        emit categoriesReady(categoriesFrom(data));
    });
}

QList<FyydCategory> Fyyd::cachedCategories() const
{
    QByteArray body;
    if (!m_cache.stale(QUrl(QString::fromLatin1(ApiBase) + QStringLiteral("categories")), &body)) {
        return QList<FyydCategory>();
    }
    return categoriesFrom(QJsonDocument::fromJson(body).object().value(QStringLiteral("data")));
}

void Fyyd::search(const QString &term, int count)
{
    const QString t = term.trimmed();
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("term"), t);
    query.addQueryItem(QStringLiteral("count"), QString::number(count));
    get(QStringLiteral("search/podcast"), query, QStringLiteral("search %1").arg(t),
        [this, t](const QJsonObject &, const QJsonValue &data) {
        emit searchReady(t, podcastsFrom(arrayIn(data, "podcasts")));
    });
}

void Fyyd::hot(const QString &language, int count)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("count"), QString::number(count));
    const QString lang = language.trimmed().toLower();
    if (!lang.isEmpty()) {
        query.addQueryItem(QStringLiteral("language"), lang);
    }
    get(QStringLiteral("feature/podcast/hot"), query, QStringLiteral("hot %1").arg(lang),
        [this, lang](const QJsonObject &, const QJsonValue &data) {
        emit hotReady(lang, podcastsFrom(arrayIn(data, "podcasts")));
    });
}

void Fyyd::category(int categoryId, int page, int count)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("category_id"), QString::number(categoryId));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("count"), QString::number(count));
    get(QStringLiteral("category"), query, QStringLiteral("category %1").arg(categoryId),
        [this, categoryId](const QJsonObject &root, const QJsonValue &data) {
        emit categoryReady(categoryId, podcastsFrom(arrayIn(data, "podcasts")), hasNextPage(root));
    });
}

// Letters and digits only, lower case: "CRE: Technik, Kultur" = "cre technik kultur"
QString Fyyd::normalized(const QString &text)
{
    QString result;
    const QString lower = text.toLower();
    for (int i = 0; i < lower.size(); ++i) {
        if (lower.at(i).isLetterOrNumber()) {
            result.append(lower.at(i));
        }
    }
    return result;
}

QString Fyyd::siteKey(const QString &url)
{
    QString key = url.trimmed().toLower();
    key.remove(QRegularExpression(QStringLiteral("^[a-z]+://")));
    if (key.startsWith(QLatin1String("www."))) {
        key = key.mid(4);
    }
    key = key.section(QLatin1Char('#'), 0, 0);
    while (key.endsWith(QLatin1Char('/'))) {
        key.chop(1);
    }
    return key;
}

void Fyyd::lookupThen(const QString &feedUrl, const QString &title, const QString &author,
                      std::function<void(int, bool)> next, const QString &website)
{
    const QString address = feedUrl.trimmed();
    if (address.isEmpty() && title.trimmed().isEmpty()) {
        QTimer::singleShot(0, this, [next]() { next(0, false); });
        return;
    }
    // 1. The podcast search by address; its hits are compared with the
    // address. (feed/info, used by other fyyd clients, never answered in
    // 4.86 and was dropped in 128.)
    // 2. If fyyd lists the podcast under another address (cre.fm/feed/m4a
    // vs. feeds.metaebene.me/cre/m4a, 4.86.7): the search by title, a hit
    // counts with the same title and the same author - or as the only hit
    // with that title when an author is missing on one side.
    const QString wantedTitle = normalized(title);
    const QString wantedAuthor = normalized(author);
    const QString wantedSite = siteKey(website);
    // 3. (157) A full-text search for the title, a hit counts with the same
    // web page (a podcast fyyd lists under another name or address)
    std::function<void()> byTerm = [this, title, wantedSite, next]() {
        if (wantedSite.isEmpty() || title.trimmed().isEmpty()) {
            next(0, false);
            return;
        }
        QUrlQuery search;
        search.addQueryItem(QStringLiteral("term"), title.trimmed());
        search.addQueryItem(QStringLiteral("count"), QStringLiteral("20"));
        get(QStringLiteral("search/podcast"), search, QStringLiteral("lookup term \"%1\"").arg(title.trimmed()),
            [wantedSite, next](const QJsonObject &, const QJsonValue &hits) {
            const QJsonArray array = arrayIn(hits, "podcasts");
            for (int i = 0; i < array.size(); ++i) {
                const QJsonObject p = array.at(i).toObject();
                if (siteKey(text(p, "htmlURL")) == wantedSite && intValue(p.value(QStringLiteral("id"))) > 0) {
                    next(intValue(p.value(QStringLiteral("id"))), true);
                    return;
                }
            }
            next(0, false);
        });
    };
    std::function<void()> byTitle = [this, address, title, wantedTitle, wantedAuthor, wantedSite, next, byTerm]() {
        if (wantedTitle.isEmpty()) {
            next(0, false);
            return;
        }
        QUrlQuery search;
        search.addQueryItem(QStringLiteral("title"), title.trimmed());
        search.addQueryItem(QStringLiteral("count"), QStringLiteral("20"));
        get(QStringLiteral("search/podcast"), search, QStringLiteral("lookup \"%1\"").arg(title.trimmed()),
            [wantedTitle, wantedAuthor, wantedSite, next, byTerm](const QJsonObject &, const QJsonValue &hits) {
            const QJsonArray array = arrayIn(hits, "podcasts");
            QList<QJsonObject> sameTitle;
            for (int i = 0; i < array.size(); ++i) {
                const QJsonObject p = array.at(i).toObject();
                if (normalized(text(p, "title")) == wantedTitle && intValue(p.value(QStringLiteral("id"))) > 0) {
                    sameTitle.append(p);
                }
            }
            for (int i = 0; i < sameTitle.size(); ++i) {
                const QString hitAuthor = normalized(text(sameTitle.at(i), "author"));
                if (!wantedAuthor.isEmpty() && hitAuthor == wantedAuthor) {
                    next(intValue(sameTitle.at(i).value(QStringLiteral("id"))), true);
                    return;
                }
            }
            if (sameTitle.size() == 1
                    && (wantedAuthor.isEmpty() || normalized(text(sameTitle.first(), "author")).isEmpty())) {
                next(intValue(sameTitle.first().value(QStringLiteral("id"))), true);
                return;
            }
            // (157) The same web page
            for (int i = 0; i < array.size() && !wantedSite.isEmpty(); ++i) {
                const QJsonObject p = array.at(i).toObject();
                if (siteKey(text(p, "htmlURL")) == wantedSite && intValue(p.value(QStringLiteral("id"))) > 0) {
                    next(intValue(p.value(QStringLiteral("id"))), true);
                    return;
                }
            }
            byTerm();
        });
    };
    if (address.isEmpty()) {
        QTimer::singleShot(0, this, byTitle);
        return;
    }
    QUrlQuery search;
    search.addQueryItem(QStringLiteral("url"), address);
    search.addQueryItem(QStringLiteral("count"), QStringLiteral("10"));
    get(QStringLiteral("search/podcast"), search, QStringLiteral("lookup %1").arg(address),
        [address, next, byTitle](const QJsonObject &, const QJsonValue &hits) {
        const QJsonArray array = arrayIn(hits, "podcasts");
        for (int i = 0; i < array.size(); ++i) {
            const QJsonObject p = array.at(i).toObject();
            if (sameFeed(text(p, "xmlURL"), address)) {
                next(intValue(p.value(QStringLiteral("id"))), false);
                return;
            }
        }
        byTitle();
    });
}

void Fyyd::lookup(const QString &feedUrl, const QString &title, const QString &author)
{
    const QString address = feedUrl.trimmed();
    lookupThen(address, title, author, [this, address](int id, bool byTitle) {
        emit lookupReady(address, id, byTitle);
    });
}

void Fyyd::similar(int podcastId, int count)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("podcast_id"), QString::number(podcastId));
    query.addQueryItem(QStringLiteral("count"), QString::number(count));
    get(QStringLiteral("podcast/recommend"), query, QStringLiteral("similar %1").arg(podcastId),
        [this, podcastId](const QJsonObject &, const QJsonValue &data) {
        QList<FyydPodcast> list = podcastsFrom(arrayIn(data, "podcasts"));
        // Never the podcast itself
        for (int i = list.size() - 1; i >= 0; --i) {
            if (list.at(i).id == podcastId) {
                list.removeAt(i);
            }
        }
        emit similarReady(QString(), podcastId, list);
    });
}

void Fyyd::similarForFeed(const QString &feedUrl, int count, const QString &title, const QString &author,
                          const QString &website)
{
    const QString address = feedUrl.trimmed();
    lookupThen(address, title, author, [this, address, count](int id, bool) {
        if (id <= 0) {
            emit similarReady(address, 0, QList<FyydPodcast>());
            return;
        }
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("podcast_id"), QString::number(id));
        query.addQueryItem(QStringLiteral("count"), QString::number(count));
        get(QStringLiteral("podcast/recommend"), query, QStringLiteral("similar %1").arg(id),
            [this, address, id](const QJsonObject &, const QJsonValue &data) {
            QList<FyydPodcast> list = podcastsFrom(arrayIn(data, "podcasts"));
            for (int i = list.size() - 1; i >= 0; --i) {
                if (list.at(i).id == id || sameFeed(list.at(i).url, address)) {
                    list.removeAt(i);
                }
            }
            emit similarReady(address, id, list);
        });
    }, website);
}

void Fyyd::podcastCollections(int podcastId, int count)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("podcast_id"), QString::number(podcastId));
    query.addQueryItem(QStringLiteral("count"), QString::number(count));
    get(QStringLiteral("podcast/collections"), query, QStringLiteral("collections %1").arg(podcastId),
        [this, podcastId](const QJsonObject &, const QJsonValue &data) {
        QList<FyydCollection> list;
        const QJsonArray array = arrayIn(data, "collections");
        for (int i = 0; i < array.size(); ++i) {
            const FyydCollection c = collectionFrom(array.at(i).toObject());
            if (c.id > 0) {
                list.append(c);
            }
        }
        emit podcastCollectionsReady(QString(), podcastId, list);
    });
}

void Fyyd::podcastCollectionsForFeed(const QString &feedUrl, int count, const QString &title,
                                     const QString &author)
{
    const QString address = feedUrl.trimmed();
    lookupThen(address, title, author, [this, address, count](int id, bool) {
        if (id <= 0) {
            emit podcastCollectionsReady(address, 0, QList<FyydCollection>());
            return;
        }
        QUrlQuery query;
        query.addQueryItem(QStringLiteral("podcast_id"), QString::number(id));
        query.addQueryItem(QStringLiteral("count"), QString::number(count));
        get(QStringLiteral("podcast/collections"), query, QStringLiteral("collections %1").arg(id),
            [this, address, id](const QJsonObject &, const QJsonValue &data) {
            QList<FyydCollection> list;
            const QJsonArray array = arrayIn(data, "collections");
            for (int i = 0; i < array.size(); ++i) {
                const FyydCollection c = collectionFrom(array.at(i).toObject());
                if (c.id > 0) {
                    list.append(c);
                }
            }
            emit podcastCollectionsReady(address, id, list);
        });
    });
}

void Fyyd::collection(int collectionId)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("collection_id"), QString::number(collectionId));
    get(QStringLiteral("collection/podcasts"), query, QStringLiteral("collection %1").arg(collectionId),
        [this, collectionId](const QJsonObject &, const QJsonValue &data) {
        FyydCollection c = collectionFrom(data.toObject());
        if (c.id <= 0) {
            c.id = collectionId;
        }
        emit collectionReady(c, podcastsFrom(arrayIn(data, "podcasts")));
    });
}

void Fyyd::categoryCurations(int categoryId, int page, int count)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("category_id"), QString::number(categoryId));
    query.addQueryItem(QStringLiteral("page"), QString::number(page));
    query.addQueryItem(QStringLiteral("count"), QString::number(count));
    get(QStringLiteral("category/curation"), query, QStringLiteral("curations %1").arg(categoryId),
        [this, categoryId](const QJsonObject &root, const QJsonValue &data) {
        QList<FyydCuration> list;
        const QJsonArray array = arrayIn(data, "curations");
        for (int i = 0; i < array.size(); ++i) {
            const FyydCuration c = curationFrom(array.at(i).toObject());
            // Only public ones (the endpoint should send nothing else)
            if (c.id > 0 && c.isPublic) {
                list.append(c);
            }
        }
        emit categoryCurationsReady(categoryId, list, hasNextPage(root));
    });
}

void Fyyd::podcast(int podcastId)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("podcast_id"), QString::number(podcastId));
    get(QStringLiteral("podcast"), query, QStringLiteral("podcast %1").arg(podcastId),
        [this, podcastId](const QJsonObject &, const QJsonValue &data) {
        const FyydPodcast p = podcastFrom(data.isArray() && !data.toArray().isEmpty()
                                          ? data.toArray().at(0).toObject() : data.toObject());
        emit podcastReady(podcastId, p);
    });
}

void Fyyd::curation(int curationId)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("curation_id"), QString::number(curationId));
    get(QStringLiteral("curation/episodes"), query, QStringLiteral("curation %1").arg(curationId),
        [this, curationId](const QJsonObject &, const QJsonValue &data) {
        FyydCuration c = curationFrom(data.toObject());
        if (c.id <= 0) {
            c.id = curationId;
        }
        QList<FyydEpisode> episodes;
        const QJsonArray array = arrayIn(data, "episodes");
        for (int i = 0; i < array.size(); ++i) {
            const FyydEpisode e = episodeFrom(array.at(i).toObject());
            if (e.id > 0) {
                episodes.append(e);
            }
        }
        emit curationReady(c, episodes);
    });
}

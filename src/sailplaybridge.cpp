// SPDX-License-Identifier: GPL-3.0-or-later
#include "sailplaybridge.h"

#include "database.h"
#include "imagecache.h"
#include "sailplaycontent.h"
#include "sailplayprovider.h"

#include <QDBusConnection>
#include <QDateTime>
#include <QLocale>
#include <QVector>

#include <algorithm>

namespace {

// The car shows at most 50 rows per reply (Content1); longer lists are
// paged. Lists that have no natural end are cut here.
const int MaxRows = 500;
const int MaxSearchRows = 100;

const QString RootId = QStringLiteral("root");
const QString ContinueId = QStringLiteral("continue");
const QString NewId = QStringLiteral("new");
const QString PlaylistId = QStringLiteral("playlist");
const QString PodcastsId = QStringLiteral("podcasts");
const QString PodcastPrefix = QStringLiteral("podcast/");
const QString EpisodePrefix = QStringLiteral("episode/");
const QString PlayAction = QStringLiteral("sailcast.play");

// Content1 text fields hold at most 1024 characters
QString bounded(const QString &text, int limit = 1024)
{
    QString t = text;
    t.replace(QChar::Null, QChar::Space);
    return t.toUcs4().size() <= limit ? t : QString::fromUcs4(t.toUcs4().constData(), limit);
}

QString minutes(int seconds)
{
    return QString::number(qMax(1, (seconds + 30) / 60));
}

} // namespace

SailplayBridge::SailplayBridge(Database *database, QObject *parent)
    : QObject(parent), m_database(database)
{
    // An update of many feeds changes the lists many times in a row: one
    // new catalog version per second at most
    m_changeTimer.setSingleShot(true);
    m_changeTimer.setInterval(1000);
    connect(&m_changeTimer, &QTimer::timeout, this, [this]() {
        ++m_version;
        publish();
    });
}

SailplayBridge::~SailplayBridge()
{
    if (m_provider) {
        m_provider->close();
        delete m_provider;
    }
}

bool SailplayBridge::start()
{
    if (m_provider) {
        return true;
    }
    QDBusConnection bus = QDBusConnection::sessionBus();
    m_provider = new SailplayProvider(bus, QStringLiteral("harbour-sailcast"), state(),
                                      QStringList() << QStringLiteral("media") << QStringLiteral("content"),
                                      this);
    const auto browse = [this](qulonglong invocation, const QVariantMap &parameters, bool search) {
        QString error;
        QVariantMap presentation;
        const QVariantMap page = query(parameters, search, &error, &presentation);
        if (page.isEmpty()) {
            m_provider->fail(invocation, QStringLiteral("NotFound"), error);
            return;
        }
        QVariantMap result{{QStringLiteral("content"), page}};
        if (!presentation.isEmpty()) {
            result.insert(QStringLiteral("presentation"), presentation);
        }
        m_provider->complete(invocation, result);
    };
    m_provider->setActionHandler(QStringLiteral("content.browse"),
        [browse](qulonglong invocation, const QVariantMap &parameters) { browse(invocation, parameters, false); });
    m_provider->setActionHandler(QStringLiteral("content.search"),
        [browse](qulonglong invocation, const QVariantMap &parameters) { browse(invocation, parameters, true); });
    m_provider->setActionHandler(PlayAction, [this](qulonglong invocation, const QVariantMap &parameters) {
        const int episodeId = parameters.value(QStringLiteral("episodeId")).toInt();
        if (episodeId <= 0 || !m_database || !m_database->episode(episodeId).id) {
            m_provider->fail(invocation, QStringLiteral("NotFound"), QStringLiteral("Episode is no longer available"));
            return;
        }
        emit playRequested(episodeId, parameters.value(QStringLiteral("playlist")).toBool());
        m_provider->complete(invocation, QVariantMap{{QStringLiteral("started"), true}});
    });
    if (!m_provider->start()) {
        qWarning("Sailcast: Sailplay SDK objects not exported");
        delete m_provider;
        m_provider = nullptr;
        return false;
    }
    return true;
}

void SailplayBridge::catalogChanged()
{
    if (m_provider) {
        m_changeTimer.start();
    }
}

void SailplayBridge::setPlayback(int episodeId, bool playing)
{
    if (episodeId == m_episodeId && playing == m_playing) {
        return;
    }
    m_episodeId = episodeId;
    m_playing = playing;
    publish();
}

QVariantMap SailplayBridge::state() const
{
    // D-Bus needs a concrete type for every value, also without an episode
    QVariantMap media;
    QString title;
    QString podcast;
    if (m_episodeId > 0 && m_database) {
        const EpisodeRow e = m_database->episode(m_episodeId);
        title = e.title;
        podcast = e.podcastTitle;
    }
    media.insert(QStringLiteral("episodeId"), qMax(0, m_episodeId));
    media.insert(QStringLiteral("title"), bounded(title));
    media.insert(QStringLiteral("podcast"), bounded(podcast));
    media.insert(QStringLiteral("playing"), m_playing);
    return QVariantMap{{QStringLiteral("media"), media}, {QStringLiteral("content"), root()},
                       {QStringLiteral("contentPresentation"), rootPresentation()}};
}

void SailplayBridge::publish()
{
    if (m_provider && !m_provider->publish(state())) {
        qWarning("Sailcast: Sailplay state not published");
    }
}

QVariantMap SailplayBridge::root() const
{
    QVariantMap page = SailplayContent::page(RootId, QStringLiteral("Sailcast"));
    page.insert(QStringLiteral("version"), m_version);
    page.insert(QStringLiteral("searchEnabled"), true);
    QVariantList items;
    items << SailplayContent::item(ContinueId, tr("Continue listening"))
          << SailplayContent::item(NewId, tr("New episodes"))
          << SailplayContent::item(PlaylistId, tr("Playlist"))
          << SailplayContent::item(PodcastsId, tr("Podcasts"));
    page.insert(QStringLiteral("items"), items);
    return page;
}

QVariantMap SailplayBridge::rootPresentation() const
{
    // Continue listening, New episodes, Playlist and Podcasts as tabs
    return SailplayContent::presentation(QStringLiteral("list"), true);
}

QVariantMap SailplayBridge::query(const QVariantMap &parameters, bool search, QString *error,
                                  QVariantMap *presentation) const
{
    if (!SailplayContent::validQuery(parameters, search)) {
        if (error) *error = QStringLiteral("Invalid query");
        return QVariantMap();
    }
    const QString id = parameters.value(QStringLiteral("id")).toString();
    const uint offset = parameters.value(QStringLiteral("offset")).toUInt();
    const uint limit = parameters.value(QStringLiteral("limit")).toUInt();

    QString title;
    QString message;
    QString layout = QStringLiteral("list");
    Hints hints;
    QVariantList all;
    if (search) {
        title = tr("Search");
        all = searchItems(parameters.value(QStringLiteral("query")).toString(), &hints);
        if (all.isEmpty()) {
            message = tr("Nothing found");
        }
    } else if (id == RootId) {
        all = root().value(QStringLiteral("items")).toList();
        title = QStringLiteral("Sailcast");
    } else {
        bool found = false;
        all = folderItems(id, &title, &message, &found, &layout, &hints);
        if (!found) {
            if (error) *error = QStringLiteral("Content folder is unavailable");
            return QVariantMap();
        }
    }

    QVariantMap page = SailplayContent::page(id, bounded(title));
    page.insert(QStringLiteral("version"), m_version);
    page.insert(QStringLiteral("offset"), offset);
    page.insert(QStringLiteral("searchEnabled"), id == RootId);
    QVariantList items;
    if (offset < uint(all.size())) {
        items = all.mid(int(offset), int(limit));
    }
    page.insert(QStringLiteral("items"), items);
    page.insert(QStringLiteral("hasMore"), !items.isEmpty() && offset + uint(items.size()) < uint(all.size()));
    if (!message.isEmpty() && offset == 0) {
        page.insert(QStringLiteral("message"), bounded(message));
        page.insert(QStringLiteral("messageKind"), QStringLiteral("info"));
    }
    if (!SailplayContent::validPage(page)) {
        if (error) *error = QStringLiteral("Content page could not be built");
        return QVariantMap();
    }
    if (presentation) {
        // Hints for the rows of this reply only
        *presentation = (!search && id == RootId) ? rootPresentation() : SailplayContent::presentation(layout);
        for (const QVariant &v : items) {
            const QString rowId = v.toMap().value(QStringLiteral("id")).toString();
            const QVariantMap hint = hints.value(rowId);
            const QString image = hint.value(QStringLiteral("image")).toString();
            const QString playback = hint.value(QStringLiteral("playback")).toString();
            // An address the car cannot use costs the image, not the status
            if (!SailplayContent::hint(*presentation, rowId, image, playback)) {
                SailplayContent::hint(*presentation, rowId, QString(), playback);
            }
        }
    }
    return page;
}

QVariantList SailplayBridge::folderItems(const QString &id, QString *title, QString *message, bool *found,
                                         QString *layout, Hints *hints) const
{
    QVariantList items;
    *found = true;
    if (!m_database) {
        *message = tr("Sailcast is starting");
        return items;
    }
    if (id == ContinueId) {
        *title = tr("Continue listening");
        for (const EpisodeRow &e : m_database->episodes(-1, MaxRows, Database::InProgress)) {
            items << episodeItem(e, false, true, hints);
        }
        if (items.isEmpty()) *message = tr("No episode started");
    } else if (id == NewId) {
        *title = tr("New episodes");
        for (const EpisodeRow &e : m_database->episodes(-1, MaxRows, Database::HomeNew)) {
            items << episodeItem(e, false, true, hints);
        }
        if (items.isEmpty()) *message = tr("No new episodes");
    } else if (id == PlaylistId) {
        *title = tr("Playlist");
        for (qint64 episodeId : m_database->queueIds()) {
            const EpisodeRow e = m_database->episode(episodeId);
            if (e.id) {
                items << episodeItem(e, true, true, hints);
            }
            if (items.size() >= MaxRows) break;
        }
        if (items.isEmpty()) *message = tr("The playlist is empty");
    } else if (id == PodcastsId) {
        *title = tr("Podcasts");
        // Podcasts on the left, the chosen one's episodes on the right
        *layout = QStringLiteral("split");
        QList<PodcastRow> podcasts = m_database->podcasts();
        std::sort(podcasts.begin(), podcasts.end(), [](const PodcastRow &a, const PodcastRow &b) {
            return Database::comparePodcastTitles(a.title, b.title) < 0;
        });
        for (const PodcastRow &p : podcasts) {
            if (p.provisional) continue;
            items << podcastItem(p, p.unplayedCount > 0 ? tr("%n unplayed", nullptr, p.unplayedCount) : QString(),
                                 hints);
        }
        if (items.isEmpty()) *message = tr("No podcasts subscribed");
    } else if (id.startsWith(PodcastPrefix)) {
        bool ok = false;
        const qint64 podcastId = id.mid(PodcastPrefix.size()).toLongLong(&ok);
        const PodcastRow p = ok ? m_database->podcast(podcastId) : PodcastRow();
        if (!p.id) {
            *found = false;
            return items;
        }
        *title = p.title;
        for (const EpisodeRow &e : m_database->episodes(p.id, MaxRows, Database::AllEpisodes)) {
            items << episodeItem(e, false, false, hints);
        }
        if (items.isEmpty()) *message = tr("No episodes");
    } else {
        *found = false;
    }
    return items;
}

QVariantList SailplayBridge::searchItems(const QString &query, Hints *hints) const
{
    QVariantList items;
    if (!m_database) {
        return items;
    }
    // Podcasts first (as in Search my podcasts), then episodes
    const Database::SearchTerm term = Database::prepareSearch(query);
    QList<PodcastRow> podcasts = m_database->podcasts();
    std::sort(podcasts.begin(), podcasts.end(), [](const PodcastRow &a, const PodcastRow &b) {
        return Database::comparePodcastTitles(a.title, b.title) < 0;
    });
    for (const PodcastRow &p : podcasts) {
        if (p.provisional || !Database::matchesSearch(QStringList() << p.title << p.author, term, 0)) continue;
        items << podcastItem(p, tr("Podcast"), hints);
        if (items.size() >= 20) break;
    }
    for (const EpisodeRow &e : m_database->searchEpisodes(query, MaxSearchRows)) {
        items << episodeItem(e, false, true, hints);
    }
    return items;
}

QString SailplayBridge::imageFor(const QString &remote)
{
    // The scaled local copy of the image cache when there is one (the car
    // screen reads it as the same user, outside Sailcast's sandbox), else
    // the address itself: the cache fetches it meanwhile for the next time
    if (remote.isEmpty()) {
        return QString();
    }
    ImageCache *cache = ImageCache::instance();
    const QString local = cache ? cache->url(remote) : QString();
    return local.isEmpty() ? remote : local;
}

QString SailplayBridge::playbackOf(const EpisodeRow &e) const
{
    if (e.id == m_episodeId && m_playing) {
        return QStringLiteral("playing");
    }
    if (e.played) {
        return QStringLiteral("finished");
    }
    return e.inProgress() ? QStringLiteral("started") : QStringLiteral("unplayed");
}

QVariantMap SailplayBridge::podcastItem(const PodcastRow &p, const QString &subtitle, Hints *hints) const
{
    const QString id = PodcastPrefix + QString::number(p.id);
    QVariantMap item = SailplayContent::item(id, bounded(p.title.isEmpty() ? p.url : p.title));
    if (!subtitle.isEmpty()) {
        item.insert(QStringLiteral("subtitle"), bounded(subtitle));
    }
    if (hints) {
        hints->insert(id, QVariantMap{{QStringLiteral("image"), imageFor(p.coverUrl)}});
    }
    return item;
}

QVariantMap SailplayBridge::episodeItem(const EpisodeRow &e, bool fromPlaylist, bool showPodcast, Hints *hints) const
{
    QVariantMap parameters;
    parameters.insert(QStringLiteral("episodeId"), int(e.id));
    parameters.insert(QStringLiteral("playlist"), fromPlaylist);
    QVariantMap item = SailplayContent::item(EpisodePrefix + QString::number(e.id),
                                             bounded(e.title.isEmpty() ? tr("Episode") : e.title),
                                             QStringLiteral("action"), PlayAction, parameters);
    // "Podcast · 12 min left" / "21 Nov · 45 min" / "Played"
    QStringList parts;
    if (showPodcast && !e.podcastTitle.isEmpty()) {
        parts << e.podcastTitle;
    } else if (e.published > 0) {
        parts << QLocale().toString(QDateTime::fromMSecsSinceEpoch(e.published * 1000).date(), QLocale::ShortFormat);
    }
    if (e.played) {
        parts << tr("Played");
    } else if (e.inProgress() && e.remaining() > 0) {
        parts << tr("%1 min left").arg(minutes(e.remaining()));
    } else if (e.duration > 0) {
        parts << tr("%1 min").arg(minutes(e.duration));
    }
    item.insert(QStringLiteral("subtitle"), bounded(parts.join(QStringLiteral(" · "))));
    if (hints) {
        hints->insert(item.value(QStringLiteral("id")).toString(),
                      QVariantMap{{QStringLiteral("image"), imageFor(e.displayImageUrl)},
                                  {QStringLiteral("playback"), playbackOf(e)}});
    }
    return item;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "episodelistmodel.h"

#include <algorithm>

#include <QDateTime>
#include <QFileInfo>
#include <QLocale>
#include <QSet>
#include <QTimer>

#include "appcore.h"

namespace {
const int HomeLimit = 100;
// (306) Grouped "All unplayed": rows per podcast, folded and unfolded
const int GroupRows = 3;
const int GroupRowsOpen = 50;
}

EpisodeListModel::EpisodeListModel(QObject *parent)
    : RowListModel<EpisodeRow>(parent)
{
    if (AppCore *core = AppCore::instance()) {
        connect(core, &AppCore::dataChanged, this, &EpisodeListModel::scheduleReload);
        connect(core, &AppCore::queueChanged, this, [this]() {
            if (m_filter == Queue) {
                scheduleReload();
            } else if (!m_rows.isEmpty()) {
                emit dataChanged(index(0), index(m_rows.size() - 1), QVector<int>() << QueuedRole);
            }
        });
        connect(core, &AppCore::episodeChanged, this, &EpisodeListModel::refreshEpisode);
        connect(core, &AppCore::downloadProgressChanged, this,
                [this](int episodeId, qreal) { refreshDownload(episodeId); });
        connect(core, &AppCore::downloadFinished, this,
                [this](int episodeId, bool) { refreshDownload(episodeId); });
    }
    // The properties are usually set right after creation (QML): load once
    // they are all known
    scheduleReload();
}

void EpisodeListModel::setPodcastId(int id)
{
    if (id != m_podcastId) {
        m_podcastId = id;
        emit podcastIdChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setKeepVanished(bool on)
{
    if (on != m_keepVanished) {
        m_keepVanished = on;
        emit keepVanishedChanged();
    }
}

void EpisodeListModel::unfoldGroup(int podcastId, bool open)
{
    if (open) {
        m_groupLimit.insert(podcastId, GroupRowsOpen);
    } else {
        m_groupLimit.remove(podcastId);
    }
    scheduleReload();
}

QSet<qint64> EpisodeListModel::collapsedPodcasts() const
{
    QSet<qint64> ids;
    AppCore *core = AppCore::instance();
    if (!core) {
        return ids;
    }
    const QStringList parts = core->database()->setting(QStringLiteral("home.collapsedPodcasts"))
            .split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString &part : parts) {
        const qint64 id = part.toLongLong();
        if (id > 0) {
            ids.insert(id);
        }
    }
    return ids;
}

void EpisodeListModel::storeCollapsed(const QSet<qint64> &ids)
{
    AppCore *core = AppCore::instance();
    if (!core) {
        return;
    }
    QStringList parts;
    for (qint64 id : ids) {
        parts << QString::number(id);
    }
    core->database()->setSetting(QStringLiteral("home.collapsedPodcasts"), parts.join(QLatin1Char(',')));
}

void EpisodeListModel::setGroupCollapsed(int podcastId, bool collapsed)
{
    QSet<qint64> ids = collapsedPodcasts();
    if (collapsed) {
        ids.insert(podcastId);
        // Its kept rows go with it
        for (const EpisodeRow &r : m_rows) {
            if (r.podcastId == podcastId) {
                m_kept.remove(r.id);
            }
        }
    } else {
        ids.remove(podcastId);
    }
    // Opened again: the three newest, not an earlier unfolded state
    m_groupLimit.remove(podcastId);
    storeCollapsed(ids);
    scheduleReload();
}

void EpisodeListModel::setAllGroupsCollapsed(bool collapsed)
{
    QSet<qint64> ids;
    if (collapsed) {
        for (auto it = m_groupTotal.constBegin(); it != m_groupTotal.constEnd(); ++it) {
            ids.insert(it.key());
        }
        // Folded podcasts not in the view now stay folded
        ids.unite(collapsedPodcasts());
        m_kept.clear();
    } else {
        // (333, code review) "Unfold all" opens only the podcasts in the view;
        // folded ones without unplayed episodes now stay folded
        ids = collapsedPodcasts();
        for (auto it = m_groupTotal.constBegin(); it != m_groupTotal.constEnd(); ++it) {
            ids.remove(it.key());
        }
    }
    m_groupLimit.clear();
    storeCollapsed(ids);
    scheduleReload();
}

bool EpisodeListModel::anyGroupOpen() const
{
    const QSet<qint64> folded = collapsedPodcasts();
    for (auto it = m_groupTotal.constBegin(); it != m_groupTotal.constEnd(); ++it) {
        if (!folded.contains(it.key())) {
            return true;
        }
    }
    return false;
}

// (335) The download filter of New episodes (setting home.downloadFilter,
// read by the database); a change reloads without keeping rows
void EpisodeListModel::setDownloadFilter(const QString &filter)
{
    if (filter != m_downloadFilter) {
        m_downloadFilter = filter;
        m_kept.clear();
        m_groupLimit.clear();
        m_skipKeepOnce = true;
        emit downloadFilterChanged();
        scheduleReload();
    }
}

void EpisodeListModel::clearKept()
{
    m_kept.clear();
    // (306) Unfolded groups fold again with the kept rows (view changed,
    // page left)
    m_groupLimit.clear();
    m_skipKeepOnce = true;
    scheduleReload();
}

void EpisodeListModel::setFilter(Filter filter)
{
    if (filter != m_filter) {
        // (302) Another view: nothing is kept from the old one
        m_kept.clear();
        m_groupLimit.clear();
        m_skipKeepOnce = true;
        m_filter = filter;
        emit filterChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setSections(bool on)
{
    if (on != m_sections) {
        m_sections = on;
        emit sectionsChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setGroupByPodcast(bool on)
{
    if (on != m_groupByPodcast) {
        // (302) Another arrangement: nothing is kept
        m_kept.clear();
        m_groupLimit.clear();
        m_skipKeepOnce = true;
        m_groupByPodcast = on;
        emit groupByPodcastChanged();
        scheduleReload();
    }
}

QString EpisodeListModel::sectionOf(const EpisodeRow &e) const
{
    if (m_groupByPodcast) {
        return e.podcastTitle;
    }
    return sectionLabel(e, m_bySeason);
}

QString EpisodeListModel::sectionLabel(const EpisodeRow &e, bool bySeason)
{
    if (bySeason) {
        return e.season > 0 ? tr("Season %1").arg(e.season) : tr("Without season");
    }
    if (e.published <= 0) {
        return tr("Without date");
    }
    const QDate date = QDateTime::fromMSecsSinceEpoch(e.published * 1000).date();
    if (date.year() == QDate::currentDate().year()) {
        // "Oktober 2026" (stand-alone month name of the phone's language)
        return QLocale().standaloneMonthName(date.month()) + QLatin1Char(' ') + QString::number(date.year());
    }
    return QString::number(date.year());
}

void EpisodeListModel::setLimit(int limit)
{
    if (limit != m_limit) {
        m_limit = limit;
        emit limitChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setOldestFirst(bool oldestFirst)
{
    if (oldestFirst != m_oldestFirst) {
        m_oldestFirst = oldestFirst;
        emit oldestFirstChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setSearch(const QString &search)
{
    if (search != m_search) {
        m_search = search;
        emit searchChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setSortMode(int mode)
{
    if (mode != m_sortMode) {
        m_sortMode = mode;
        emit sortModeChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setEpisodeId(int id)
{
    if (id != m_episodeId) {
        m_episodeId = id;
        emit episodeIdChanged();
        scheduleReload();
    }
}

void EpisodeListModel::setExcludeEpisodeId(int id)
{
    if (id != m_excludeEpisodeId) {
        m_excludeEpisodeId = id;
        emit excludeEpisodeIdChanged();
        scheduleReload();
    }
}

void EpisodeListModel::scheduleReload()
{
    if (!m_reloadPending) {
        m_reloadPending = true;
        QTimer::singleShot(0, this, &EpisodeListModel::reload);
    }
}

void EpisodeListModel::reload()
{
    m_reloadPending = false;
    AppCore *core = AppCore::instance();
    if (!core || m_dragging) {
        return;
    }
    Database *db = core->database();
    QList<EpisodeRow> rows;
    if (m_filter == Single) {
        if (m_episodeId > 0) {
            const EpisodeRow row = db->episode(m_episodeId);
            if (row.id) {
                rows.append(row);
            }
        }
    } else if (m_filter == Recent) {
        const QStringList ids = db->setting(QStringLiteral("player.recent")).split(QLatin1Char(','), QString::SkipEmptyParts);
        for (const QString &value : ids) {
            if (m_limit > 0 && rows.size() >= m_limit) {
                break;
            }
            const EpisodeRow row = db->episode(value.toLongLong());
            // Gone (unsubscribed) or the one playing: left out
            if (row.id && row.id != m_excludeEpisodeId && !row.provisional) {
                rows.append(row);
            }
        }
    } else {
        Database::EpisodeFilter filter = Database::AllEpisodes;
        int limit = m_limit;
        int sortMode = m_sortMode;
        switch (m_filter) {
        case Unplayed: filter = Database::Unplayed; break;
        case InProgress: filter = Database::InProgress; break;
        case Downloaded: filter = Database::Downloaded; break;
        case DownloadedActive: filter = Database::DownloadedActive; break;
        case DownloadedArchived: filter = Database::DownloadedArchived; break;
        case NewEpisodes: filter = Database::NewEpisodes; break;
        case Queue: filter = Database::Queue; break;
        case HomeNew:
            filter = Database::HomeNew;
            limit = m_limit > 0 ? m_limit : HomeLimit;
            if (m_groupByPodcast) {
                sortMode = 4;
            }
            break;
        case Home:
            filter = Database::HomeUnplayed;
            limit = m_limit > 0 ? m_limit : HomeLimit;
            // (293) Grouped by podcast: sorted by podcast (Database sort mode 4)
            if (m_groupByPodcast) {
                sortMode = 4;
            }
            break;
        default: break;
        }
        // (303) Largest file first: read in download order, sorted by the
        // size on disk below (the database does not know it)
        const bool bySize = sortMode == 5;
        if (bySize) {
            sortMode = 0;
        }
        m_groupTotal.clear();
        m_groupShown.clear();
        m_folded.clear();
        if (shortensGroups() && m_search.isEmpty()) {
            // (306) A few per podcast instead of the first 100 in the
            // alphabet (which left podcasts late in the alphabet out)
            rows = groupedHomeRows(db);
            limit = -1;
        } else {
        // One more, in case the excluded episode is among them; one more to
        // know whether there are more (158)
        rows = db->episodes(m_podcastId, limit > 0 ? limit + 1 + (m_excludeEpisodeId > 0 ? 1 : 0) : limit, filter,
                            m_oldestFirst, m_search, sortMode);
        }
        // (306) Podcast order without leading quotes (as the Podcasts page)
        if (sortMode == 4 || (sortMode == 3 && !bySize)) {
            std::stable_sort(rows.begin(), rows.end(), [](const EpisodeRow &a, const EpisodeRow &b) {
                return Database::comparePodcastTitles(a.podcastTitle, b.podcastTitle) < 0;
            });
        }
        // Seasons (158): only when the podcast's feed has two or more
        m_bySeason = m_sections && m_podcastId > 0 && db->seasonCount(m_podcastId) >= 2;
        if (m_excludeEpisodeId > 0) {
            for (int i = rows.size() - 1; i >= 0; --i) {
                if (rows.at(i).id == m_excludeEpisodeId) {
                    rows.removeAt(i);
                }
            }
        }
        const bool more = limit > 0 && rows.size() > limit;
        if (limit > 0) {
            while (rows.size() > limit) {
                rows.removeLast();
            }
        }
        if (more != m_hasMore) {
            m_hasMore = more;
            emit countChanged();
        }
        m_bytes.clear();
        if (bySize) {
            std::stable_sort(rows.begin(), rows.end(), [this](const EpisodeRow &a, const EpisodeRow &b) {
                return fileBytes(a) > fileBytes(b);
            });
        }
        // (302) Rows the user dealt with stay (see keepVanished)
        if (m_keepVanished && (m_filter == Home || m_filter == HomeNew)) {
            keepVanishedRows(db, rows);
        }
    }
    m_videoIds = db->videoAlternateEpisodes();
    if (applyRows(rows)) {
        emit countChanged();
    }
    // Sections switched on or changed kind: every row's section is new;
    // with sections every reload may move a section's first row (161)
    const int mode = !m_sections ? 0 : (m_bySeason ? 2 : 1);
    if (mode != m_appliedSectionMode || m_sections) {
        m_appliedSectionMode = mode;
        if (!m_rows.isEmpty()) {
            emit dataChanged(index(0), index(m_rows.size() - 1));
        }
    }
}

// (306) Grouped "All unplayed": the newest GroupRows unplayed episodes of
// each podcast (unfolded ones more), podcasts in the order of the Podcasts
// page; the rest only counted for "+ N more"
QList<EpisodeRow> EpisodeListModel::groupedHomeRows(Database *db)
{
    const bool onlyNew = m_filter == HomeNew;
    QList<EpisodeRef> refs = db->homeUnplayedRefs(onlyNew);
    // Newest first inside a podcast (the order of the query); same title,
    // different podcasts: kept apart by id
    std::stable_sort(refs.begin(), refs.end(), [](const EpisodeRef &a, const EpisodeRef &b) {
        const int c = Database::comparePodcastTitles(a.podcastTitle, b.podcastTitle);
        return c != 0 ? c < 0 : a.podcastId < b.podcastId;
    });
    QList<EpisodeRow> rows;
    // (322) "New" has no folding (a tap opens the podcast): nothing folded there
    const QSet<qint64> folded = onlyNew ? QSet<qint64>() : collapsedPodcasts();
    m_folded = folded;
    for (const EpisodeRef &r : refs) {
        if (r.id == m_excludeEpisodeId) {
            continue;
        }
        const int total = m_groupTotal.value(r.podcastId) + 1;
        m_groupTotal.insert(r.podcastId, total);
        if (folded.contains(r.podcastId)) {
            // (307) Folded: its newest row carries the header only
            if (total == 1) {
                const EpisodeRow row = db->episode(r.id);
                if (row.id) {
                    rows.append(row);
                }
            }
            continue;
        }
        if (total <= m_groupLimit.value(r.podcastId, GroupRows)) {
            const EpisodeRow row = db->episode(r.id);
            if (row.id) {
                rows.append(row);
                m_groupShown.insert(r.podcastId, m_groupShown.value(r.podcastId) + 1);
            }
        }
    }
    return rows;
}

// (302) Rows that were shown and now drop out of the list because the
// user dealt with the episode (flag gone, or played) are put back; rows
// that drop out for other reasons (podcast hidden or gone, more new
// episodes pushing the oldest past the limit) are not.
void EpisodeListModel::keepVanishedRows(Database *db, QList<EpisodeRow> &rows)
{
    if (m_skipKeepOnce) {
        m_skipKeepOnce = false;
        m_kept.clear();
        return;
    }
    QSet<qint64> present;
    for (const EpisodeRow &e : rows) {
        present.insert(e.id);
    }
    const QList<qint64> hidden = db->hiddenFromNew();
    // (335) Download filter: a row whose download state changed (➕ loads it,
    // "Delete download", Auto-Download) left the filtered list without the
    // user leaving it - it stays like a row dealt with
    const QString loadFilter = db->homeDownloadFilter();
    auto dealtWith = [this, &hidden, &loadFilter](const EpisodeRow &fresh) {
        if (!fresh.id || fresh.provisional || hidden.contains(fresh.podcastId)) {
            return false;
        }
        if ((loadFilter == QLatin1String("downloaded") && !fresh.downloaded)
                || (loadFilter == QLatin1String("notDownloaded") && fresh.downloaded)) {
            return true;
        }
        return m_filter == HomeNew ? !(fresh.isNew && !fresh.played) : fresh.played;
    };
    for (const EpisodeRow &old : m_rows) {
        if (!present.contains(old.id) && !m_kept.contains(old.id) && dealtWith(db->episode(old.id))) {
            m_kept.insert(old.id);
        }
    }
    const QList<qint64> keptNow = m_kept.toList();
    for (qint64 id : keptNow) {
        if (present.contains(id)) {
            // Back in the list by itself (flag set again): no longer "kept"
            m_kept.remove(id);
            continue;
        }
        const EpisodeRow fresh = db->episode(id);
        if (!dealtWith(fresh)) {
            m_kept.remove(id);
            continue;
        }
        rows.append(fresh);
    }
    if (rows.size() > present.size()) {
        const bool grouped = m_groupByPodcast;
        std::stable_sort(rows.begin(), rows.end(), [grouped](const EpisodeRow &a, const EpisodeRow &b) {
            if (grouped) {
                const int c = Database::comparePodcastTitles(a.podcastTitle, b.podcastTitle);
                if (c != 0) {
                    return c < 0;
                }
            }
            if (a.published != b.published) {
                return a.published > b.published;
            }
            return a.id > b.id;
        });
    }
}

void EpisodeListModel::move(int from, int to)
{
    if (moveRow(from, to)) {
        m_dragging = true;
    }
}

void EpisodeListModel::commitQueueOrder(int movedId)
{
    m_dragging = false;
    AppCore *core = AppCore::instance();
    if (!core || m_filter != Queue) {
        return;
    }
    QVariantList ids;
    for (int i = 0; i < m_rows.size(); ++i) {
        ids.append(int(m_rows.at(i).id));
    }
    core->queueSetOrder(ids, movedId);
    // (276) The pin shows at once, not only after the list is loaded again
    if (!m_rows.isEmpty()) {
        QVector<int> roles;
        roles << PinnedRole << AutoAddedRole;
        emit dataChanged(index(0), index(m_rows.size() - 1), roles);
    }
}

void EpisodeListModel::refreshEpisode(int episodeId)
{
    m_bytes.remove(episodeId);
    const int row = rowOf(episodeId);
    AppCore *core = AppCore::instance();
    if (!core) {
        return;
    }
    if (row < 0) {
        // An episode just started (or paused for the first time) belongs
        // into the list of episodes in progress
        if (m_filter == InProgress && episodeId != m_excludeEpisodeId) {
            scheduleReload();
        }
        return;
    }
    const EpisodeRow fresh = core->database()->episode(episodeId);
    if (!fresh.id) {
        scheduleReload();
        return;
    }
    m_rows[row] = fresh;
    emit dataChanged(index(row), index(row));
}

void EpisodeListModel::refreshDownload(int episodeId)
{
    const int row = rowOf(episodeId);
    if (row >= 0) {
        emit dataChanged(index(row), index(row),
                         QVector<int>() << DownloadingRole << DownloadProgressRole << DownloadedRole);
    }
}

// (303) The real size of the downloaded file (not the feed's figure)
qint64 EpisodeListModel::fileBytes(const EpisodeRow &e) const
{
    if (!e.downloaded || e.downloadPath.isEmpty()) {
        return 0;
    }
    auto it = m_bytes.constFind(e.id);
    if (it != m_bytes.constEnd()) {
        return it.value();
    }
    const QFileInfo info(e.downloadPath);
    const qint64 bytes = info.exists() ? info.size() : 0;
    m_bytes.insert(e.id, bytes);
    return bytes;
}

int EpisodeListModel::episodeIdAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? int(m_rows.at(row).id) : -1;
}

int EpisodeListModel::rowOfEpisode(int episodeId) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (int(m_rows.at(i).id) == episodeId) {
            return i;
        }
    }
    return -1;
}

QVariantList EpisodeListModel::allEpisodeIds() const
{
    QVariantList ids;
    for (const EpisodeRow &e : m_rows) {
        ids.append(int(e.id));
    }
    return ids;
}

// Both ends included, in the order of the list as it is now
QVariantList EpisodeListModel::episodeIdsBetween(int idA, int idB) const
{
    QVariantList ids;
    int a = rowOfEpisode(idA);
    int b = rowOfEpisode(idB);
    if (a < 0 || b < 0) {
        return ids;
    }
    if (a > b) {
        qSwap(a, b);
    }
    for (int i = a; i <= b; ++i) {
        ids.append(int(m_rows.at(i).id));
    }
    return ids;
}

QVariantList EpisodeListModel::episodeIdsNotQueued() const
{
    QVariantList ids;
    AppCore *core = AppCore::instance();
    for (const EpisodeRow &e : m_rows) {
        if (!core || !core->isQueued(int(e.id))) {
            ids.append(int(e.id));
        }
    }
    return ids;
}

QVariantList EpisodeListModel::episodeIdsOfPodcast(int podcastId) const
{
    QVariantList ids;
    for (const EpisodeRow &e : m_rows) {
        if (int(e.podcastId) == podcastId) {
            ids.append(int(e.id));
        }
    }
    return ids;
}

double EpisodeListModel::bytesOf(const QVariantList &ids) const
{
    QSet<int> wanted;
    for (const QVariant &v : ids) {
        wanted.insert(v.toInt());
    }
    double sum = 0;
    for (const EpisodeRow &e : m_rows) {
        if (wanted.contains(int(e.id))) {
            sum += double(fileBytes(e));
        }
    }
    return sum;
}

QVariant EpisodeListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const EpisodeRow &e = m_rows.at(index.row());
    AppCore *core = AppCore::instance();
    switch (role) {
    case EpisodeIdRole: return int(e.id);
    case PodcastIdRole: return int(e.podcastId);
    case TitleRole: return e.title;
    case PodcastTitleRole: return e.podcastTitle;
    case ImageUrlRole: return e.displayImageUrl;
    case PublishedRole: return double(e.published) * 1000.0;    // ms for JavaScript's Date
    case DurationRole: return e.duration;
    case PositionRole: return e.position;
    case RemainingRole: return e.remaining();
    case ProgressRole:
        if (e.played) {
            return 1.0;
        }
        // double, not qreal: qreal is float on armv7hl/i486, and qBound(double,
        // float, double) does not compile there (79)
        return e.duration > 0 ? qBound(0.0, double(e.position) / double(e.duration), 1.0) : 0.0;
    case PlayedRole: return e.played;
    case IsNewRole: return e.isNew;
    case InProgressRole: return e.inProgress();
    case ChapterCountRole: return e.chapterCount;
    case BookmarkCountRole: return e.bookmarkCount;
    case ProvisionalRole: return e.provisional;
    case VideoRole: return e.mimeType.startsWith(QLatin1String("video/")) || m_videoIds.contains(e.id);
    case VideoFileRole: return e.mimeType.startsWith(QLatin1String("video/"));
    case FileBytesRole: return double(fileBytes(e));
    case PodcastCoverRole: return e.podcastCoverUrl;
    case GroupCountRole: {
        if (!m_groupByPodcast) {
            return 0;
        }
        if (m_groupTotal.contains(e.podcastId)) {
            return m_groupTotal.value(e.podcastId);
        }
        int n = 0;
        for (const EpisodeRow &r : m_rows) {
            if (r.podcastId == e.podcastId && !r.played) {
                ++n;
            }
        }
        return n;
    }
    case GroupCollapsedRole:
        return shortensGroups() && m_groupTotal.contains(e.podcastId) && !m_groupShown.contains(e.podcastId)
                && m_folded.contains(e.podcastId);
    case GroupMoreRole:
    case GroupExpandedRole: {
        if (!shortensGroups() || !m_groupTotal.contains(e.podcastId) || !m_groupShown.contains(e.podcastId)) {
            return role == GroupMoreRole ? QVariant(0) : QVariant(false);
        }
        const bool last = index.row() == m_rows.size() - 1 || m_rows.at(index.row() + 1).podcastId != e.podcastId;
        if (!last) {
            return role == GroupMoreRole ? QVariant(0) : QVariant(false);
        }
        if (role == GroupMoreRole) {
            return qMax(0, m_groupTotal.value(e.podcastId) - m_groupShown.value(e.podcastId));
        }
        return m_groupLimit.contains(e.podcastId) && m_groupTotal.value(e.podcastId) > GroupRows;
    }
    case SectionRole: return m_sections || m_groupByPodcast ? sectionOf(e) : QString();
    case SectionStartRole:
        return (m_sections || m_groupByPodcast) && (index.row() == 0 || sectionOf(m_rows.at(index.row() - 1)) != sectionOf(e));
    case SeasonRole: return e.season;
    case EpisodeNumberRole: return e.episodeNumber;
    // The file lies under its archive name (archive folder, 120)
    case KeptRole: return e.downloaded && !e.archiveFile.isEmpty()
                && e.downloadPath.endsWith(QLatin1Char('/') + e.archiveFile);
    case DownloadedRole: return e.downloaded;
    // (345, Thomas + Gemini 17 Nov) A row on New episodes whose download state
    // no longer matches the download filter (➕ loaded it, Auto-Download,
    // "Delete download"): it stays until the page is left (335) and says why
    case LeftFilterRole:
        if ((m_filter != Home && m_filter != HomeNew) || m_downloadFilter.isEmpty()) {
            return false;
        }
        return m_downloadFilter == QLatin1String("notDownloaded") ? e.downloaded
             : m_downloadFilter == QLatin1String("downloaded") ? !e.downloaded : false;
    case DownloadingRole: return core ? core->isDownloading(int(e.id)) : false;
    case DownloadProgressRole: return core ? core->downloadProgress(int(e.id)) : 0.0;
    case QueuedRole: return core ? core->isQueued(int(e.id)) : false;
    case PinnedRole: return core ? core->isPinned(int(e.id)) : false;
    case AutoAddedRole: return core ? core->isAutoAdded(int(e.id)) : false;
    default: return QVariant();
    }
}

QHash<int, QByteArray> EpisodeListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names.insert(EpisodeIdRole, "episodeId");
    names.insert(PodcastIdRole, "podcastId");
    names.insert(TitleRole, "title");
    names.insert(PodcastTitleRole, "podcastTitle");
    names.insert(ImageUrlRole, "imageUrl");
    names.insert(PublishedRole, "published");
    names.insert(DurationRole, "duration");
    names.insert(PositionRole, "position");
    names.insert(RemainingRole, "remaining");
    names.insert(ProgressRole, "progress");
    names.insert(PlayedRole, "played");
    names.insert(IsNewRole, "isNew");
    names.insert(InProgressRole, "inProgress");
    names.insert(ChapterCountRole, "chapterCount");
    names.insert(BookmarkCountRole, "bookmarkCount");
    names.insert(SeasonRole, "season");
    names.insert(EpisodeNumberRole, "episodeNumber");
    names.insert(KeptRole, "kept");
    names.insert(LeftFilterRole, "leftFilter");
    names.insert(ProvisionalRole, "provisional");
    names.insert(VideoRole, "video");
    names.insert(VideoFileRole, "videoFile");
    names.insert(FileBytesRole, "fileBytes");
    names.insert(PodcastCoverRole, "podcastCover");
    names.insert(GroupCountRole, "groupCount");
    names.insert(GroupMoreRole, "groupMore");
    names.insert(GroupExpandedRole, "groupExpanded");
    names.insert(GroupCollapsedRole, "groupCollapsed");
    names.insert(SectionRole, "section");
    names.insert(SectionStartRole, "sectionStart");
    names.insert(DownloadedRole, "downloaded");
    names.insert(DownloadingRole, "downloading");
    names.insert(DownloadProgressRole, "downloadProgress");
    names.insert(QueuedRole, "queued");
    names.insert(PinnedRole, "pinned");
    names.insert(AutoAddedRole, "autoAdded");
    return names;
}

// SPDX-License-Identifier: GPL-3.0-or-later
#include "episodelistmodel.h"

#include <QTimer>

#include "appcore.h"

namespace {
const int HomeLimit = 100;
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

void EpisodeListModel::setFilter(Filter filter)
{
    if (filter != m_filter) {
        m_filter = filter;
        emit filterChanged();
        scheduleReload();
    }
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
    } else {
        Database::EpisodeFilter filter = Database::AllEpisodes;
        int limit = m_limit;
        switch (m_filter) {
        case Unplayed: filter = Database::Unplayed; break;
        case InProgress: filter = Database::InProgress; break;
        case Downloaded: filter = Database::Downloaded; break;
        case NewEpisodes: filter = Database::NewEpisodes; break;
        case Queue: filter = Database::Queue; break;
        case Home:
            filter = Database::Unplayed;
            limit = m_limit > 0 ? m_limit : HomeLimit;
            break;
        default: break;
        }
        // One more, in case the excluded episode is among them
        rows = db->episodes(m_podcastId, limit > 0 && m_excludeEpisodeId > 0 ? limit + 1 : limit, filter,
                            m_oldestFirst, m_search);
        if (m_excludeEpisodeId > 0) {
            for (int i = rows.size() - 1; i >= 0; --i) {
                if (rows.at(i).id == m_excludeEpisodeId) {
                    rows.removeAt(i);
                }
            }
            if (limit > 0) {
                while (rows.size() > limit) {
                    rows.removeLast();
                }
            }
        }
    }
    if (applyRows(rows)) {
        emit countChanged();
    }
}

void EpisodeListModel::move(int from, int to)
{
    if (moveRow(from, to)) {
        m_dragging = true;
    }
}

void EpisodeListModel::commitQueueOrder()
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
    core->queueSetOrder(ids);
}

void EpisodeListModel::refreshEpisode(int episodeId)
{
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

int EpisodeListModel::episodeIdAt(int row) const
{
    return row >= 0 && row < m_rows.size() ? int(m_rows.at(row).id) : -1;
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
    case SeasonRole: return e.season;
    case EpisodeNumberRole: return e.episodeNumber;
    case DownloadedRole: return e.downloaded;
    case DownloadingRole: return core ? core->isDownloading(int(e.id)) : false;
    case DownloadProgressRole: return core ? core->downloadProgress(int(e.id)) : 0.0;
    case QueuedRole: return core ? core->isQueued(int(e.id)) : false;
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
    names.insert(DownloadedRole, "downloaded");
    names.insert(DownloadingRole, "downloading");
    names.insert(DownloadProgressRole, "downloadProgress");
    names.insert(QueuedRole, "queued");
    return names;
}

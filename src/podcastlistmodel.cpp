// SPDX-License-Identifier: GPL-3.0-or-later
#include "podcastlistmodel.h"

#include <algorithm>

#include <QCoreApplication>
#include <QTimer>

#include "appcore.h"

PodcastListModel::PodcastListModel(QObject *parent)
    : RowListModel<PodcastRow>(parent)
{
    if (AppCore *core = AppCore::instance()) {
        connect(core, &AppCore::dataChanged, this, &PodcastListModel::scheduleReload);
    }
    reload();
}

// Several changes in a row (e.g. an update of all feeds) are applied once,
// and never in the middle of the QML code that triggered them
void PodcastListModel::scheduleReload()
{
    if (!m_reloadPending) {
        m_reloadPending = true;
        QTimer::singleShot(0, this, &PodcastListModel::reload);
    }
}

void PodcastListModel::reload()
{
    m_reloadPending = false;
    AppCore *core = AppCore::instance();
    if (!core) {
        return;
    }
    QList<PodcastRow> rows = core->database()->podcasts();
    if (m_folderId >= 0) {
        QList<PodcastRow> filtered;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).folderId == m_folderId) {
                filtered.append(rows.at(i));
            }
        }
        rows = filtered;
    }
    if (m_firstFolderId >= 0) {
        // Stable: both groups stay alphabetical
        std::stable_partition(rows.begin(), rows.end(), [this](const PodcastRow &row) {
            return row.folderId == m_firstFolderId;
        });
    }
    int fresh = 0;
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).freshEpisodes) {
            ++fresh;
        }
    }
    if (applyRows(rows)) {
        emit countChanged();
    }
    if (fresh != m_freshCount) {
        m_freshCount = fresh;
        emit freshCountChanged();
    }
}

void PodcastListModel::setFolderId(int folderId)
{
    if (m_folderId != folderId) {
        m_folderId = folderId;
        emit folderIdChanged();
        reload();
    }
}

void PodcastListModel::setFirstFolderId(int folderId)
{
    if (m_firstFolderId != folderId) {
        m_firstFolderId = folderId;
        emit firstFolderIdChanged();
        reload();
    }
}

QVariant PodcastListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const PodcastRow &p = m_rows.at(index.row());
    switch (role) {
    case PodcastIdRole: return int(p.id);
    case TitleRole: return p.title;
    case AuthorRole: return p.author;
    case CoverUrlRole: return p.coverUrl;
    case EpisodeCountRole: return p.episodeCount;
    case UnplayedRole: return p.unplayedCount;
    case NewCountRole: return p.newCount;
    case DownloadedRole: return p.downloadedCount;
    case ArchivedRole: return p.archived;
    case LastErrorRole: return p.lastError;
    case ListenedSecondsRole: return double(p.listenedSeconds);
    case ManualUpdateRole: return p.manualUpdate;
    case FolderIdRole: return int(p.folderId);
    case FreshRole: return p.freshEpisodes;
    default: return QVariant();
    }
}

QHash<int, QByteArray> PodcastListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names.insert(PodcastIdRole, "podcastId");
    names.insert(TitleRole, "title");
    names.insert(AuthorRole, "author");
    names.insert(CoverUrlRole, "coverUrl");
    names.insert(EpisodeCountRole, "episodeCount");
    names.insert(UnplayedRole, "unplayed");
    names.insert(NewCountRole, "newCount");
    names.insert(DownloadedRole, "downloadedCount");
    names.insert(ArchivedRole, "archived");
    names.insert(LastErrorRole, "lastError");
    names.insert(ListenedSecondsRole, "listenedSeconds");
    names.insert(ManualUpdateRole, "manualUpdate");
    names.insert(FolderIdRole, "folderId");
    names.insert(FreshRole, "fresh");
    return names;
}

// --- FolderListModel (45) ---------------------------------------------------

FolderListModel::FolderListModel(QObject *parent)
    : RowListModel<FolderRow>(parent)
{
    if (AppCore *core = AppCore::instance()) {
        connect(core, &AppCore::dataChanged, this, &FolderListModel::scheduleReload);
    }
    reload();
}

void FolderListModel::scheduleReload()
{
    if (!m_reloadPending) {
        m_reloadPending = true;
        QTimer::singleShot(0, this, &FolderListModel::reload);
    }
}

void FolderListModel::reload()
{
    m_reloadPending = false;
    AppCore *core = AppCore::instance();
    if (!core) {
        return;
    }
    QList<FolderRow> rows = core->database()->folders();
    const int folders = rows.size();
    FolderRow other = core->database()->unfiledFolder();
    other.id = 0;
    other.name = core->otherFolderName();
    if (folders == 0 || other.podcastCount > 0) {
        rows.append(other);
    }
    const bool changed = applyRows(rows) || folders != m_folderCount;
    m_folderCount = folders;
    if (changed) {
        emit countChanged();
    }
}

QVariant FolderListModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() < 0 || index.row() >= m_rows.size()) {
        return QVariant();
    }
    const FolderRow &f = m_rows.at(index.row());
    switch (role) {
    case FolderIdRole: return int(f.id);
    case NameRole: return f.name;
    case ManualUpdateRole: return f.manualUpdate;
    case PodcastCountRole: return f.podcastCount;
    case UnplayedRole: return f.unplayedCount;
    default: return QVariant();
    }
}

QHash<int, QByteArray> FolderListModel::roleNames() const
{
    QHash<int, QByteArray> names;
    names.insert(FolderIdRole, "folderId");
    names.insert(NameRole, "name");
    names.insert(ManualUpdateRole, "manualUpdate");
    names.insert(PodcastCountRole, "podcastCount");
    names.insert(UnplayedRole, "unplayed");
    return names;
}

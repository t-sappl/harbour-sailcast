// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PODCASTLISTMODEL_H
#define PODCASTLISTMODEL_H

#include <QList>

#include "database.h"
#include "rowlistmodel.h"

// All subscriptions, alphabetically, for the podcast grid/list. Reloads
// itself when AppCore reports changed data (only the changed rows, see
// RowListModel).
class PodcastListModel : public RowListModel<PodcastRow>
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Podcasts with new episodes since last opened (70, marker on the
    // folder header)
    Q_PROPERTY(int freshCount READ freshCount NOTIFY freshCountChanged)
    // Only the podcasts of one folder (45): -1 all (default), 0 those
    // without a folder, N folder N
    Q_PROPERTY(int folderId READ folderId WRITE setFolderId NOTIFY folderIdChanged)
    // The podcasts of this folder first, then the rest - both alphabetical
    // (57, choosing the podcasts of a folder): -1 off (default), 0 those
    // without a folder, N folder N
    Q_PROPERTY(int firstFolderId READ firstFolderId WRITE setFirstFolderId NOTIFY firstFolderIdChanged)

public:
    enum Roles {
        PodcastIdRole = Qt::UserRole + 1,
        TitleRole,
        AuthorRole,
        CoverUrlRole,
        EpisodeCountRole,
        UnplayedRole,
        NewCountRole,
        DownloadedRole,
        ArchivedRole,
        LastErrorRole,
        ListenedSecondsRole,
        ManualUpdateRole,
        FolderIdRole,
        FreshRole
    };

    explicit PodcastListModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_rows.size(); }
    int freshCount() const { return m_freshCount; }
    int folderId() const { return m_folderId; }
    void setFolderId(int folderId);
    int firstFolderId() const { return m_firstFolderId; }
    void setFirstFolderId(int folderId);

    Q_INVOKABLE void reload();
    Q_INVOKABLE int indexOf(int podcastId) const { return rowOf(podcastId); }

signals:
    void countChanged();
    void folderIdChanged();
    void firstFolderIdChanged();
    void freshCountChanged();

private:
    void scheduleReload();
    bool m_reloadPending = false;
    int m_folderId = -1;
    int m_firstFolderId = -1;
    int m_freshCount = 0;
};

// The folders for the Podcasts page (45), by their order; the last row is
// "Other podcasts" (id 0, podcasts without a folder) when there are folders
// and such podcasts. Without any folder: just that one row (the page then
// shows no folder headers).
class FolderListModel : public RowListModel<FolderRow>
{
    Q_OBJECT
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Real folders (without "Other podcasts")
    Q_PROPERTY(int folderCount READ folderCount NOTIFY countChanged)

public:
    enum Roles {
        FolderIdRole = Qt::UserRole + 1,
        NameRole,
        ManualUpdateRole,
        PodcastCountRole,
        UnplayedRole
    };

    explicit FolderListModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;
    int count() const { return m_rows.size(); }
    int folderCount() const { return m_folderCount; }

    Q_INVOKABLE void reload();

signals:
    void countChanged();

private:
    void scheduleReload();
    bool m_reloadPending = false;
    int m_folderCount = 0;
};

#endif // PODCASTLISTMODEL_H

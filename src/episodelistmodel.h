// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EPISODELISTMODEL_H
#define EPISODELISTMODEL_H

#include <QHash>
#include <QList>
#include <QString>
#include <QVariantList>

#include "database.h"
#include "rowlistmodel.h"

// Episodes for the lists, loaded from SQL with a filter instead of keeping
// everything in memory. Roles cover the layout wishes: remaining time and
// progress, number of chapters, episode image (else the podcast cover),
// and - in PodcastListModel - the number of unplayed episodes.
//
// Reloads only apply the differences (see RowListModel), so delegates and
// their images stay while the data changes.
//
// filter Home: the newest unplayed episodes of all podcasts (started ones
// included, they show their progress).
// filter Single: just the episode "episodeId" (the playlist's "Now
// playing").
// excludeEpisodeId: left out of the list (the playlist shows the current
// episode once, at the top).
class EpisodeListModel : public RowListModel<EpisodeRow>
{
    Q_OBJECT
    Q_PROPERTY(int podcastId READ podcastId WRITE setPodcastId NOTIFY podcastIdChanged)
    Q_PROPERTY(Filter filter READ filter WRITE setFilter NOTIFY filterChanged)
    Q_PROPERTY(int limit READ limit WRITE setLimit NOTIFY limitChanged)
    Q_PROPERTY(int episodeId READ episodeId WRITE setEpisodeId NOTIFY episodeIdChanged)
    Q_PROPERTY(int excludeEpisodeId READ excludeEpisodeId WRITE setExcludeEpisodeId NOTIFY excludeEpisodeIdChanged)
    Q_PROPERTY(int count READ count NOTIFY countChanged)
    // Oldest first (53, per-podcast setting for serials) and a title search
    Q_PROPERTY(bool oldestFirst READ oldestFirst WRITE setOldestFirst NOTIFY oldestFirstChanged)
    Q_PROPERTY(QString search READ search WRITE setSearch NOTIFY searchChanged)
    // (274) Downloads page: 0 last downloaded, 1 newest, 2 oldest, 3 by podcast
    Q_PROPERTY(int sortMode READ sortMode WRITE setSortMode NOTIFY sortModeChanged)
    // (293) New episodes list: group by podcast (sections = podcast title)
    Q_PROPERTY(bool groupByPodcast READ groupByPodcast WRITE setGroupByPodcast NOTIFY groupByPodcastChanged)
    Q_PROPERTY(QString downloadFilter READ downloadFilter WRITE setDownloadFilter NOTIFY downloadFilterChanged)
    // (302) New episodes page: an episode that was in the list and drops out
    // because the user dealt with it (flag gone, marked played) stays until
    // clearKept(): no jumping rows, the change is visible, a wrong tap can be
    // undone
    Q_PROPERTY(bool keepVanished READ keepVanished WRITE setKeepVanished NOTIFY keepVanishedChanged)
    // (158) Long lists of a podcast: a section per season (when the feed has
    // two or more) or per month (this year) / year (before) - role "section"
    Q_PROPERTY(bool sections READ sections WRITE setSections NOTIFY sectionsChanged)
    // (158) With a limit: there are more episodes than shown
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)

public:
    // (188) Recent: the episodes heard to the end or skipped in the player,
    // newest first (setting "player.recent")
    // (302) HomeNew: the "New" view of the page New episodes (flag set, not
    // played); Home stays the view "All unplayed"
    enum Filter { All, Unplayed, InProgress, Downloaded, NewEpisodes, Queue, Home, Single, Recent,
                  DownloadedActive, DownloadedArchived, HomeNew };
    Q_ENUM(Filter)

    enum Roles {
        EpisodeIdRole = Qt::UserRole + 1,
        PodcastIdRole,
        TitleRole,
        PodcastTitleRole,
        ImageUrlRole,
        PublishedRole,
        DurationRole,
        PositionRole,
        RemainingRole,
        ProgressRole,
        PlayedRole,
        IsNewRole,
        InProgressRole,
        ChapterCountRole,
        DownloadedRole,
        DownloadingRole,
        DownloadProgressRole,
        QueuedRole,
        PinnedRole,
        AutoAddedRole,       // (286)
        BookmarkCountRole,
        SeasonRole,          // (83) 0 = none
        EpisodeNumberRole,   // (83) 0 = none
        KeptRole,            // (120) the downloaded file is in the archive folder
        ProvisionalRole,     // (156) its podcast is not subscribed
        SectionRole,         // (158) see sections
        SectionStartRole,    // (161) the first row of its section
        VideoRole,           // (245) a video episode or one with a video version
        VideoFileRole,       // (245) the episode's own file is a video
        FileBytesRole,       // (303) size of the downloaded file on disk, 0 = none
        PodcastCoverRole,    // (306) the podcast's cover (group header)
        GroupCountRole,      // (306) episodes of this podcast in the view (header)
        GroupMoreRole,       // (306) last row of a shortened group: episodes not shown
        GroupExpandedRole,   // (306) last row of an unfolded group
        GroupCollapsedRole   // (307) the group is folded: the row only carries its header
    };

    explicit EpisodeListModel(QObject *parent = nullptr);

    QVariant data(const QModelIndex &index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    int podcastId() const { return m_podcastId; }
    void setPodcastId(int id);
    Filter filter() const { return m_filter; }
    void setFilter(Filter filter);
    int limit() const { return m_limit; }
    void setLimit(int limit);
    bool oldestFirst() const { return m_oldestFirst; }
    void setOldestFirst(bool oldestFirst);
    QString search() const { return m_search; }
    int sortMode() const { return m_sortMode; }
    bool groupByPodcast() const { return m_groupByPodcast; }
    QString downloadFilter() const { return m_downloadFilter; }
    void setDownloadFilter(const QString &filter);
    void setGroupByPodcast(bool on);
    void setSortMode(int mode);
    void setSearch(const QString &search);
    bool sections() const { return m_sections; }
    void setSections(bool on);
    bool hasMore() const { return m_hasMore; }
    bool keepVanished() const { return m_keepVanished; }
    void setKeepVanished(bool on);
    int episodeId() const { return m_episodeId; }
    void setEpisodeId(int id);
    int excludeEpisodeId() const { return m_excludeEpisodeId; }
    void setExcludeEpisodeId(int id);
    int count() const { return m_rows.size(); }

    Q_INVOKABLE void reload();
    // (302) Lets go of the kept rows (chip changed, page left, list emptied)
    Q_INVOKABLE void clearKept();
    // (306) Grouped "All unplayed": a group shows its newest GroupRows
    // episodes; unfolded up to GroupRowsOpen (beyond that the podcast page)
    Q_INVOKABLE void unfoldGroup(int podcastId, bool open);
    // (307) Folded groups (header only), remembered per podcast in the
    // setting "home.collapsedPodcasts"; folding drops an unfolded state
    Q_INVOKABLE void setGroupCollapsed(int podcastId, bool collapsed);
    Q_INVOKABLE void setAllGroupsCollapsed(bool collapsed);
    Q_INVOKABLE bool anyGroupOpen() const;
    Q_INVOKABLE int episodeIdAt(int row) const;
    // (313) Selection mode (Downloads): helpers on episode ids
    Q_INVOKABLE int rowOfEpisode(int episodeId) const;
    Q_INVOKABLE QVariantList allEpisodeIds() const;
    Q_INVOKABLE QVariantList episodeIdsBetween(int idA, int idB) const;
    Q_INVOKABLE QVariantList episodeIdsNotQueued() const;
    Q_INVOKABLE QVariantList episodeIdsOfPodcast(int podcastId) const;
    Q_INVOKABLE double bytesOf(const QVariantList &ids) const;
    // Drag and drop: moves a row in the list only ...
    Q_INVOKABLE void move(int from, int to);
    // ... and stores the shown order as the playlist order (filter Queue)
    // (275) movedId: the dragged episode (pinned at its new place)
    Q_INVOKABLE void commitQueueOrder(int movedId = -1);

signals:
    void podcastIdChanged();
    void filterChanged();
    void limitChanged();
    void oldestFirstChanged();
    void searchChanged();
    void sortModeChanged();
    void groupByPodcastChanged();
    void downloadFilterChanged();
    void sectionsChanged();
    void keepVanishedChanged();
    void episodeIdChanged();
    void excludeEpisodeIdChanged();
    void countChanged();

private:
    // (245) Episodes with a video version, read with every reload
    QSet<qint64> m_videoIds;
    void keepVanishedRows(Database *db, QList<EpisodeRow> &rows);
    QList<EpisodeRow> groupedHomeRows(Database *db);
    // (322) "New" by podcast shortens its groups as "All unplayed" does
    bool shortensGroups() const { return (m_filter == Home || m_filter == HomeNew) && m_groupByPodcast; }
    QHash<qint64, int> m_groupLimit;   // (306) unfolded podcasts: rows to show
    QHash<qint64, int> m_groupTotal;   // (306) episodes per podcast in the view
    QHash<qint64, int> m_groupShown;   // (306) of them in the list
    QSet<qint64> collapsedPodcasts() const;
    QSet<qint64> m_folded;             // (307) folded groups of the last reload
    void storeCollapsed(const QSet<qint64> &ids);
    void refreshEpisode(int episodeId);
    void refreshDownload(int episodeId);
    void scheduleReload();

    int m_podcastId = -1;
    Filter m_filter = All;
    int m_limit = -1;
    bool m_oldestFirst = false;
    QString m_search;
    int m_sortMode = 0;
    bool m_groupByPodcast = false;
    QString m_downloadFilter;   // (335) only to reload; the database reads the setting
    int m_episodeId = -1;
    int m_excludeEpisodeId = -1;
    bool m_reloadPending = false;
    bool m_keepVanished = false;
    // (303) Sizes of the downloaded files (read from disk once per reload)
    mutable QHash<qint64, qint64> m_bytes;
    qint64 fileBytes(const EpisodeRow &e) const;
    QSet<qint64> m_kept;        // (302) ids of the rows kept in the list
    bool m_skipKeepOnce = false; // the next reload does not keep what dropped out
    bool m_sections = false;
    bool m_bySeason = false;    // sections by season (else by time)
    bool m_hasMore = false;
    int m_appliedSectionMode = -1;  // sections shown last time (0 none, 1 time, 2 season)
    QString sectionOf(const EpisodeRow &e) const;
    // While a drag is not committed, reloads would undo the shown order
    bool m_dragging = false;

public:
    // (159) The section of an episode: its season, or month (this year) / year
    static QString sectionLabel(const EpisodeRow &e, bool bySeason);
};

#endif // EPISODELISTMODEL_H

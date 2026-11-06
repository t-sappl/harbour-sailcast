// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef EPISODELISTMODEL_H
#define EPISODELISTMODEL_H

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
    // (158) Long lists of a podcast: a section per season (when the feed has
    // two or more) or per month (this year) / year (before) - role "section"
    Q_PROPERTY(bool sections READ sections WRITE setSections NOTIFY sectionsChanged)
    // (158) With a limit: there are more episodes than shown
    Q_PROPERTY(bool hasMore READ hasMore NOTIFY countChanged)

public:
    // (188) Recent: the episodes heard to the end or skipped in the player,
    // newest first (setting "player.recent")
    enum Filter { All, Unplayed, InProgress, Downloaded, NewEpisodes, Queue, Home, Single, Recent };
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
        BookmarkCountRole,
        SeasonRole,          // (83) 0 = none
        EpisodeNumberRole,   // (83) 0 = none
        KeptRole,            // (120) the downloaded file is in the archive folder
        ProvisionalRole,     // (156) its podcast is not subscribed
        SectionRole,         // (158) see sections
        SectionStartRole,    // (161) the first row of its section
        VideoRole,           // (245) a video episode or one with a video version
        VideoFileRole        // (245) the episode's own file is a video
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
    void setSearch(const QString &search);
    bool sections() const { return m_sections; }
    void setSections(bool on);
    bool hasMore() const { return m_hasMore; }
    int episodeId() const { return m_episodeId; }
    void setEpisodeId(int id);
    int excludeEpisodeId() const { return m_excludeEpisodeId; }
    void setExcludeEpisodeId(int id);
    int count() const { return m_rows.size(); }

    Q_INVOKABLE void reload();
    Q_INVOKABLE int episodeIdAt(int row) const;
    // Drag and drop: moves a row in the list only ...
    Q_INVOKABLE void move(int from, int to);
    // ... and stores the shown order as the playlist order (filter Queue)
    Q_INVOKABLE void commitQueueOrder();

signals:
    void podcastIdChanged();
    void filterChanged();
    void limitChanged();
    void oldestFirstChanged();
    void searchChanged();
    void sectionsChanged();
    void episodeIdChanged();
    void excludeEpisodeIdChanged();
    void countChanged();

private:
    // (245) Episodes with a video version, read with every reload
    QSet<qint64> m_videoIds;
    void refreshEpisode(int episodeId);
    void refreshDownload(int episodeId);
    void scheduleReload();

    int m_podcastId = -1;
    Filter m_filter = All;
    int m_limit = -1;
    bool m_oldestFirst = false;
    QString m_search;
    int m_episodeId = -1;
    int m_excludeEpisodeId = -1;
    bool m_reloadPending = false;
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

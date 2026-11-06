// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef ARCHIVER_H
#define ARCHIVER_H

#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>
#include <QUrl>

class Database;
class DownloadManager;
class FeedUpdater;
class QNetworkAccessManager;
struct PodcastRow;
struct EpisodeRow;

struct ArchiveResult
{
    QString folder;             // absolute path of the podcast's archive folder
    bool feedSaved = false;     // feed.xml written (false if the feed is gone)
    bool coverSaved = false;
    int episodes = 0;           // episodes known for the podcast
    int downloaded = 0;         // loaded in this run
    int moved = 0;              // taken over from the normal download folder
    int present = 0;            // already in the archive
    QStringList failures;       // "<title>: <error>"
};

struct ArchiveImportResult
{
    bool ok = false;
    QString error;
    qint64 podcastId = 0;
    bool podcastAdded = false;
    int episodesAdded = 0;
    int filesFound = 0;
    int statesTaken = 0;        // listening states newer than the local ones
};

// One playback source of an episode, in the order to try
struct PlaybackSource
{
    enum Kind { LocalFile, Original, CloudArchive };
    Kind kind;
    QUrl url;
};

// Archives podcasts: all episodes plus the feed and metadata in one folder
// per podcast, complete and readable without Sailcast:
//
//   <archive root>/<Podcast>/feed.xml          original feed (latest copy)
//   <archive root>/<Podcast>/cover.jpg
//   <archive root>/<Podcast>/episodes.json     metadata and listening state
//   <archive root>/<Podcast>/2021-06-19 S01E01 - Title.mp3
//
// Steps of archive(): update the feed without conditional request (fresh,
// possibly signed media links; the XML is saved) -> cover -> episodes
// (files already downloaded elsewhere are moved, existing ones kept,
// interrupted ones resumed) -> episodes.json. A dead feed does not stop
// the archive: the known episodes are archived with their last URLs.
class Archiver : public QObject
{
    Q_OBJECT
public:
    Archiver(Database *database, FeedUpdater *updater, DownloadManager *downloads,
             QObject *parent = nullptr);

    void setArchiveRoot(const QString &root) { m_root = root; }
    // (245) Video files of "Downloads in Music" and "Save in Music" go to
    // this root instead (~/Videos/Podcasts), in a folder of the same name
    void setVideoRoot(const QString &root) { m_videoRoot = root; }
    QString archiveRoot() const { return m_root; }
    bool isRunning() const { return m_podcastId != 0; }

    void archive(qint64 podcastId);

    // Moves the downloaded file of one episode into the podcast's archive
    // folder (120: "Save in Music"): the folder is created (name chosen as
    // for a full archive), the file gets its archive name, the episode
    // keeps playing from there and is never cleaned up; episodes.json is
    // written. Returns the new path, empty on failure (`error`).
    QString keepEpisode(qint64 episodeId, QString *error = nullptr);

    // Writes episodes.json of an already archived podcast again (e.g.
    // after listening) - no network
    bool writeMetadata(qint64 podcastId, QString *error = nullptr);

    // Imports an archive folder (episodes.json): adds the podcast if
    // needed, the episodes that are missing, links the files found and
    // takes over newer listening states. No network.
    ArchiveImportResult importFolder(const QString &folder);

    // Sources to try when playing: local file, original URL, cloud archive
    // (setting "archive.url" + "<podcast folder>/<file>")
    static QList<PlaybackSource> playbackSources(Database *database, qint64 episodeId);

    // (209) Where a download goes with "Downloads in Music": the podcast's
    // archive folder, the name archiving would give it; empty if no root or
    // the file exists already (then the normal download folder)
    QString musicTarget(qint64 episodeId);
    // (217) Moves a downloaded file to `target` (folder created, rename or
    // copy + delete) and stores the new path; false if that fails
    bool relocate(qint64 episodeId, const QString &target);

signals:
    void progress(const QString &step);
    void finished(const ArchiveResult &result);
    void failed(const QString &error);

private:
    QString m_videoRoot;
    // (245) The root for an episode's file: Videos for video files
    QString rootFor(const EpisodeRow &e) const;
    void saveCover();
    QString ensureFolderName(const PodcastRow &podcast);
    void archiveEpisodes();
    void finish();
    QString folderFor(qint64 podcastId) const;

    Database *m_database;
    FeedUpdater *m_updater;
    DownloadManager *m_downloads;
    QNetworkAccessManager *m_network;
    QString m_root;
    qint64 m_podcastId = 0;
    ArchiveResult m_result;
    QSet<qint64> m_pending;     // episodes still downloading
    QList<QMetaObject::Connection> m_connections;
};

#endif // ARCHIVER_H

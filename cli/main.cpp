// SPDX-License-Identifier: GPL-3.0-or-later
//
// sailcast-cli - tests the Sailcast core (core/) on the desktop.
// Works on its own database (default: ~/.local/share/sailcast-cli/sailcast.db),
// never on the phone's.

#include <QCommandLineOption>
#include <QCommandLineParser>
#include <QCoreApplication>
#include <QHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QSharedPointer>
#include <QStandardPaths>
#include <QStringList>
#include <QTextStream>
#include <QTimer>

#include <termios.h>
#include <unistd.h>

#include "archiver.h"
#include "chaptersloader.h"
#include "database.h"
#include "downloadmanager.h"
#include "feedparser.h"
#include "feedprobe.h"
#include "feedupdater.h"
#include "gpodderimport.h"
#include "opml.h"
#include "podcastindex.h"
#include "syncengine.h"

namespace {

// Line end that also flushes (Qt 5.6 has no Qt::endl, and ::endl is
// deprecated in newer Qt 5 versions)
QTextStream &nl(QTextStream &stream)
{
    stream << QLatin1Char('\n');
    stream.flush();
    return stream;
}

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

QTextStream &err()
{
    static QTextStream stream(stderr);
    return stream;
}

QString formatDuration(int seconds)
{
    if (seconds < 0) {
        return QStringLiteral("?");
    }
    const int h = seconds / 3600;
    const int m = (seconds / 60) % 60;
    const int s = seconds % 60;
    if (h > 0) {
        return QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(s, 2, 10, QLatin1Char('0'));
    }
    return QStringLiteral("%1:%2").arg(m).arg(s, 2, 10, QLatin1Char('0'));
}

QString formatDate(qint64 seconds)
{
    if (seconds <= 0) {
        return QStringLiteral("----------");
    }
    return QDateTime::fromMSecsSinceEpoch(seconds * 1000).toString(QStringLiteral("yyyy-MM-dd"));
}

QString elide(const QString &text, int width)
{
    QString simple = text.simplified();
    return simple.size() > width ? simple.left(width - 1) + QChar(0x2026) : simple;
}

// --- Feed passwords (CLI only) ---
// The app keeps feed passwords in Sailfish Secrets. The desktop test tool
// has no such store, so it keeps them in a JSON file next to its database
// ({"<feed URL>": "<password>"}), readable by the owner only. For testing
// on the desktop, not meant as a secure password store.

QString credentialsPath(const QString &dbPath)
{
    return QFileInfo(dbPath).absolutePath() + QStringLiteral("/credentials.json");
}

QJsonObject loadCredentials(const QString &dbPath)
{
    QFile file(credentialsPath(dbPath));
    if (!file.open(QIODevice::ReadOnly)) {
        return QJsonObject();
    }
    return QJsonDocument::fromJson(file.readAll()).object();
}

bool saveCredentials(const QString &dbPath, const QJsonObject &credentials)
{
    QSaveFile file(credentialsPath(dbPath));
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(credentials).toJson());
    if (!file.commit()) {
        return false;
    }
    return QFile::setPermissions(credentialsPath(dbPath), QFile::ReadOwner | QFile::WriteOwner);
}

// Reads a line from the terminal without echo
QString readPassword(const QString &prompt)
{
    err() << prompt;
    err().flush();
    termios oldSettings;
    const bool tty = tcgetattr(STDIN_FILENO, &oldSettings) == 0;
    if (tty) {
        termios noEcho = oldSettings;
        noEcho.c_lflag &= ~static_cast<tcflag_t>(ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &noEcho);
    }
    QTextStream in(stdin);
    const QString line = in.readLine();
    if (tty) {
        tcsetattr(STDIN_FILENO, TCSANOW, &oldSettings);
    }
    err() << nl;
    return line;
}

QString podcastLabel(const Database &db, qint64 id)
{
    const PodcastRow row = db.podcast(id);
    return row.id ? row.title : QStringLiteral("#%1").arg(id);
}

int cmdImportOpml(Database &db, const QString &path)
{
    QString error;
    const QList<OpmlEntry> entries = Opml::read(path, &error);
    if (entries.isEmpty()) {
        err() << "No feeds found in " << path << (error.isEmpty() ? QString() : ": " + error) << nl;
        return 1;
    }
    int added = 0, existing = 0;
    for (int i = 0; i < entries.size(); ++i) {
        qint64 id = db.findPodcast(entries.at(i).url);
        if (id >= 0) {
            ++existing;
            if (entries.at(i).folder.isEmpty() || db.podcast(id).folderId != 0) {
                continue;
            }
        } else {
            id = db.addPodcast(entries.at(i).url, entries.at(i).title);
            if (id >= 0) {
                ++added;
            }
        }
        // Folders from the file (45)
        if (id >= 0 && !entries.at(i).folder.isEmpty()) {
            db.setPodcastFolder(id, db.createFolder(entries.at(i).folder));
        }
    }
    out() << entries.size() << " feeds in the file: " << added << " added, "
          << existing << " already subscribed (http/https count as the same feed)." << nl
          << "Run \"update\" to load the episodes." << nl;
    return 0;
}

int cmdExportOpml(Database &db, const QString &path)
{
    QList<OpmlEntry> entries;
    const QList<PodcastRow> podcasts = db.podcasts();
    // Folders (45): written as enclosing outlines, in the folder order
    const QList<FolderRow> folders = db.folders();
    QHash<qint64, QString> folderNames;
    for (int f = 0; f < folders.size(); ++f) {
        folderNames.insert(folders.at(f).id, folders.at(f).name);
    }
    for (int f = -1; f < folders.size(); ++f) {
        const qint64 folderId = f < 0 ? 0 : folders.at(f).id;
        for (int i = 0; i < podcasts.size(); ++i) {
            if (podcasts.at(i).folderId != folderId
                    && !(f < 0 && !folderNames.contains(podcasts.at(i).folderId))) {
                continue;
            }
            OpmlEntry entry;
            entry.title = podcasts.at(i).title;
            entry.url = podcasts.at(i).url;
            entry.htmlUrl = podcasts.at(i).link;
            entry.folder = folderNames.value(podcasts.at(i).folderId);
            entries.append(entry);
        }
    }
    QString error;
    if (!Opml::write(path, entries, &error)) {
        err() << "Export failed: " << error << nl;
        return 1;
    }
    out() << entries.size() << " feeds written to " << path << nl;
    return 0;
}

int cmdImportGpodder(Database &db, const QString &path)
{
    QElapsedTimer timer;
    timer.start();
    const GpodderImportResult r = GpodderImport::run(&db, path);
    if (!r.ok) {
        err() << "Import failed: " << r.error << nl;
        return 1;
    }
    out() << "Podcasts: " << r.podcastsAdded << " added, " << r.podcastsExisting << " already there" << nl
          << "Episodes: " << r.episodesAdded << " added, " << r.episodesMerged << " merged" << nl
          << "Played: " << r.played << ", in progress: " << r.inProgress << nl
          << "Took " << timer.elapsed() << " ms" << nl;
    return 0;
}

int cmdList(Database &db)
{
    const QList<PodcastRow> podcasts = db.podcasts();
    out() << QStringLiteral("%1  %2  %3  %4").arg(QStringLiteral("ID"), 4)
                 .arg(QStringLiteral("unplayed/all"), 13).arg(QStringLiteral("new"), 3)
                 .arg(QStringLiteral("title")) << nl;
    for (int i = 0; i < podcasts.size(); ++i) {
        const PodcastRow &p = podcasts.at(i);
        out() << QStringLiteral("%1  %2  %3  %4%5")
                 .arg(p.id, 4)
                 .arg(QStringLiteral("%1/%2").arg(p.unplayedCount).arg(p.episodeCount), 13)
                 .arg(p.newCount, 3)
                 .arg(elide(p.title, 60) + (p.authUser.isEmpty() ? QString() : QStringLiteral(" [login]"))
                      + (p.archived ? QStringLiteral(" [archived]") : QString())
                      + (p.downloadedCount > 0 ? QStringLiteral(" [%1 dl]").arg(p.downloadedCount) : QString()))
                 .arg(p.lastError.isEmpty() ? QString() : QStringLiteral("   [!] ") + elide(p.lastError, 50))
              << nl;
    }
    out() << podcasts.size() << " podcasts" << nl;
    return 0;
}

int cmdEpisodes(Database &db, qint64 podcastId, int limit, Database::EpisodeFilter filter)
{
    const QList<EpisodeRow> episodes = db.episodes(podcastId, limit, filter);
    // Flags: P = played, N = new, > = in progress, I = own episode image,
    // D = downloaded
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        QString flags;
        flags += e.played ? QLatin1Char('P') : QLatin1Char('.');
        flags += e.isNew ? QLatin1Char('N') : QLatin1Char('.');
        flags += e.inProgress() ? QLatin1Char('>') : QLatin1Char('.');
        flags += e.imageUrl.isEmpty() ? QLatin1Char('.') : QLatin1Char('I');
        flags += e.downloaded ? QLatin1Char('D') : QLatin1Char('.');
        const QString time = e.played ? formatDuration(e.duration > 0 ? e.duration : -1)
                                      : formatDuration(e.remaining()) + QStringLiteral(" left");
        out() << QStringLiteral("%1  %2  %3  %4  %5  %6")
                 .arg(e.id, 6)
                 .arg(formatDate(e.published))
                 .arg(flags)
                 .arg(time, 13)
                 .arg(e.chapterCount > 0 ? QStringLiteral("ch:%1").arg(e.chapterCount, -3) : QStringLiteral("      "))
                 .arg(podcastId < 0 ? QStringLiteral("%1  %2").arg(elide(e.podcastTitle, 20), -20).arg(elide(e.title, 60))
                                    : elide(e.title, 80))
              << nl;
    }
    out() << episodes.size() << " episodes (flags: P played, N new, > in progress, I own image, D downloaded)" << nl;
    return 0;
}

int cmdChapters(Database &db, qint64 episodeId)
{
    const QList<ParsedChapter> chapters = db.chapters(episodeId);
    for (int i = 0; i < chapters.size(); ++i) {
        out() << QStringLiteral("%1  %2").arg(formatDuration(static_cast<int>(chapters.at(i).startMs / 1000)), 8)
                 .arg(chapters.at(i).title) << nl;
    }
    out() << chapters.size() << " chapters" << nl;
    return 0;
}

// Parses a local feed file without touching the database - for testing
// the parser and the repair of broken XML
// Podcast Index search (key built in, or PODCASTINDEX_KEY / _SECRET in the
// environment for testing)
int cmdSearch(QCoreApplication &app, const QString &term)
{
    PodcastIndex index;
    const QByteArray envKey = qgetenv("PODCASTINDEX_KEY");
    const QByteArray envSecret = qgetenv("PODCASTINDEX_SECRET");
    if (!envKey.isEmpty() && !envSecret.isEmpty()) {
        index.setCredentials(QString::fromUtf8(envKey), QString::fromUtf8(envSecret));
    }
    int exitCode = 0;
    QObject::connect(&index, &PodcastIndex::searchFinished,
                     [&](const QString &, const QList<PodcastSearchResult> &results) {
        for (int i = 0; i < results.size(); ++i) {
            const PodcastSearchResult &r = results.at(i);
            out() << r.title << "  (" << r.author << ", " << r.episodeCount << " episodes, "
                  << r.language << ")" << nl << "    " << r.url << nl;
        }
        out() << results.size() << " results" << nl;
        app.quit();
    });
    QObject::connect(&index, &PodcastIndex::searchFailed, [&](const QString &, const QString &error) {
        err() << "Search failed: " << error << nl;
        exitCode = 2;
        app.quit();
    });
    QTimer::singleShot(0, &index, [&index, term]() { index.search(term); });
    app.exec();
    return exitCode;
}

// Checks an address like the app's "Add podcast" dialog (no database)
int cmdCheck(QCoreApplication &app, const QString &url)
{
    FeedProbe probe;
    int exitCode = 0;
    QObject::connect(&probe, &FeedProbe::finished, [&](const FeedProbeResult &r) {
        static const char *const names[] = { "ok", "login required", "not a podcast feed", "HTTP error",
                                             "network error", "timeout", "invalid URL" };
        out() << "Status:   " << names[r.status];
        if (r.httpStatus > 0) {
            out() << " (HTTP " << r.httpStatus << ")";
        }
        if (!r.error.isEmpty()) {
            out() << " - " << r.error;
        }
        out() << nl << "Feed URL: " << r.url << (r.discovered ? "  (named by the website)" : "") << nl;
        if (r.ok()) {
            out() << "Title:    " << r.title << nl << "Author:   " << r.author << nl
                  << "Cover:    " << r.coverUrl << nl << "Episodes: " << r.episodes << nl;
        }
        exitCode = r.ok() ? 0 : 2;
        app.quit();
    });
    QTimer::singleShot(0, &probe, [&probe, url]() { probe.check(url); });
    app.exec();
    return exitCode;
}

int cmdParseFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        err() << "Cannot read " << path << ": " << file.errorString() << nl;
        return 1;
    }
    QString error;
    const ParsedFeed feed = FeedParser::parse(file.readAll(), &error);
    out() << "Title:     " << feed.title << nl
          << "Cover:     " << feed.coverUrl << nl
          << "Episodes:  " << feed.episodes.size() << nl
          << "Repaired:  " << (feed.repaired ? "yes" : "no") << nl
          << "Error:     " << (error.isEmpty() ? QStringLiteral("none") : error) << nl;
    // podcast:funding (83)
    for (int i = 0; i < feed.funding.size(); ++i) {
        out() << "Funding:   " << feed.funding.at(i).url << "  " << feed.funding.at(i).title << nl;
    }
    for (int i = 0; i < feed.episodes.size(); ++i) {
        const ParsedEpisode &e = feed.episodes.at(i);
        out() << "  " << formatDate(e.published) << "  "
              << formatDuration(e.duration > 0 ? e.duration : -1) << "  "
              << (e.chapters.isEmpty() ? QString() : QStringLiteral("ch:%1  ").arg(e.chapters.size()))
              << (e.episodeNumber > 0 ? QStringLiteral("S%1E%2  ").arg(e.season).arg(e.episodeNumber) : QString())
              << e.title << nl;
        if (!e.description.isEmpty()) {
            out() << "      " << elide(e.description, 90) << nl;
        }
    }
    return feed.isEmpty() ? 1 : 0;
}

int cmdAuth(Database &db, const QString &dbPath, qint64 id, const QString &user)
{
    const PodcastRow podcast = db.podcast(id);
    if (!podcast.id) {
        err() << "No podcast with id " << id << nl;
        return 1;
    }
    QJsonObject credentials = loadCredentials(dbPath);
    if (user.isEmpty()) {
        db.setAuthUser(id, QString());
        credentials.remove(podcast.url);
        saveCredentials(dbPath, credentials);
        out() << "Login removed for " << podcast.title << nl;
        return 0;
    }
    const QString password = readPassword(QStringLiteral("Password for %1 at %2: ").arg(user, podcast.title));
    if (password.isEmpty()) {
        err() << "No password entered, nothing changed." << nl;
        return 1;
    }
    db.setAuthUser(id, user);
    credentials.insert(podcast.url, password);
    if (!saveCredentials(dbPath, credentials)) {
        err() << "Cannot write " << credentialsPath(dbPath) << nl;
        return 1;
    }
    out() << "Login set for " << podcast.title << " - run \"update " << id << "\" to test it." << nl;
    return 0;
}

int cmdStats(Database &db)
{
    const DatabaseStats s = db.stats();
    out() << "Podcasts:    " << s.podcasts << nl
          << "Episodes:    " << s.episodes << nl
          << "Played:      " << s.played << nl
          << "Unplayed:    " << s.unplayed << nl
          << "In progress: " << s.inProgress << nl;
    return 0;
}

// Passwords of feeds with login (see cmdAuth)
void setFeedPasswords(FeedUpdater &updater, Database &db, const QString &dbPath)
{
    const QJsonObject credentials = loadCredentials(dbPath);
    const QList<PodcastRow> podcasts = db.podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        if (!podcasts.at(i).authUser.isEmpty()) {
            updater.setPassword(podcasts.at(i).id, credentials.value(podcasts.at(i).url).toString());
        }
    }
}

int cmdUpdate(QCoreApplication &app, Database &db, const QString &dbPath, const QStringList &ids,
              const QString &subscribeUrl, int parallel)
{
    FeedUpdater updater(&db);
    updater.setMaxParallel(parallel);
    setFeedPasswords(updater, db, dbPath);
    int failed = 0, updated = 0, newTotal = 0, notModified = 0;
    qint64 lastNotModified = -1;
    QObject::connect(&updater, &FeedUpdater::podcastNotModified, [&](qint64 id) {
        ++notModified;
        lastNotModified = id;
    });
    QObject::connect(&updater, &FeedUpdater::podcastUpdated, [&](qint64 id, int newEpisodes) {
        ++updated;
        newTotal += newEpisodes;
        QString note;
        if (newEpisodes > 0) {
            note = QStringLiteral(" (+%1)").arg(newEpisodes);
        } else if (id == lastNotModified) {
            note = QStringLiteral(" (unchanged, HTTP 304)");
        }
        out() << "  ok    " << podcastLabel(db, id) << note << nl;
    });
    QObject::connect(&updater, &FeedUpdater::podcastFailed, [&](qint64 id, const QString &error) {
        ++failed;
        out() << "  FAIL  " << podcastLabel(db, id) << ": " << error << nl;
    });
    QObject::connect(&updater, &FeedUpdater::podcastWarning, [&](qint64 id, const QString &warning) {
        out() << "  WARN  " << podcastLabel(db, id) << ": " << warning << nl;
    });
    QObject::connect(&updater, &FeedUpdater::finished, &app, &QCoreApplication::quit);

    if (!subscribeUrl.isEmpty()) {
        if (updater.subscribe(subscribeUrl) < 0) {
            err() << "Subscribe failed: " << db.lastError() << nl;
            return 1;
        }
    } else if (ids.isEmpty()) {
        updater.updateAll();
    } else {
        for (int i = 0; i < ids.size(); ++i) {
            updater.updatePodcast(ids.at(i).toLongLong());
        }
    }
    if (!updater.isBusy()) {
        out() << "Nothing to update." << nl;
        return 0;
    }

    QElapsedTimer timer;
    timer.start();
    app.exec();
    out() << updated << " updated (" << notModified << " unchanged, HTTP 304), " << failed << " failed, "
          << newTotal << " new episodes in " << timer.elapsed() << " ms" << nl;

    // Listening states from the sync that were waiting for these episodes
    const ApplyResult applied = db.applyPendingActions();
    if (applied.applied > 0) {
        out() << applied.applied << " listening states from the sync applied" << nl;
    }
    return failed > 0 ? 2 : 0;
}

// --- Sync ---

int cmdSyncSetup(Database &db, const QString &dbPath, const QStringList &args)
{
    SyncEngine::Backend backend;
    if (args.size() < 3 || !SyncEngine::backendFromName(args.at(0), &backend)) {
        err() << "Usage: sync-setup nextcloud|gpodder SERVER USER [DEVICE]" << nl
              << "  e.g. sync-setup nextcloud https://cloud.example.com alice" << nl;
        return 1;
    }
    QString server = args.at(1).trimmed();
    while (server.endsWith(QLatin1Char('/'))) {
        server.chop(1);
    }
    if (!server.startsWith(QLatin1String("https://")) && !server.startsWith(QLatin1String("http://"))) {
        server.prepend(QStringLiteral("https://"));
    }
    const QString user = args.at(2);
    const QString device = args.size() > 3 ? args.at(3) : QStringLiteral("sailcast-cli");

    const QString password = readPassword(backend == SyncEngine::Nextcloud
                                          ? QStringLiteral("App password for %1: ").arg(user)
                                          : QStringLiteral("Password for %1: ").arg(user));
    if (password.isEmpty()) {
        err() << "No password entered, nothing changed." << nl;
        return 1;
    }

    // Another account or server: start over (the "since" timestamps and
    // the outbox belong to the old one)
    const bool sameAccount = db.setting(QStringLiteral("sync.backend")) == SyncEngine::backendName(backend)
            && db.setting(QStringLiteral("sync.server")) == server
            && db.setting(QStringLiteral("sync.user")) == user
            && db.setting(QStringLiteral("sync.device")) == device;
    if (!sameAccount) {
        db.resetSyncState();
    }
    db.setSetting(QStringLiteral("sync.backend"), SyncEngine::backendName(backend));
    db.setSetting(QStringLiteral("sync.server"), server);
    db.setSetting(QStringLiteral("sync.user"), user);
    db.setSetting(QStringLiteral("sync.device"), device);

    QJsonObject credentials = loadCredentials(dbPath);
    credentials.insert(QStringLiteral("sync"), password);
    if (!saveCredentials(dbPath, credentials)) {
        err() << "Cannot write " << credentialsPath(dbPath) << nl;
        return 1;
    }
    out() << "Sync account set: " << SyncEngine::backendName(backend) << ", " << server << ", " << user
          << (backend == SyncEngine::GpodderApi ? QStringLiteral(", device ") + device : QString()) << nl
          << (sameAccount ? "Same account as before - sync state kept."
                          : "New account - the next sync is a first sync.") << nl
          << "Run \"sync\" to synchronize." << nl;
    return 0;
}

int cmdSyncStatus(Database &db)
{
    if (!db.isSyncConfigured()) {
        out() << "No sync account (see sync-setup)." << nl;
        return 0;
    }
    const auto formatTime = [](const QString &seconds) -> QString {
        if (seconds.isEmpty()) {
            return QStringLiteral("never");
        }
        return QDateTime::fromMSecsSinceEpoch(seconds.toLongLong() * 1000)
                .toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    };
    const SyncCounts counts = db.syncCounts();
    out() << "Backend:              " << db.setting(QStringLiteral("sync.backend")) << nl
          << "Server:               " << db.setting(QStringLiteral("sync.server")) << nl
          << "User:                 " << db.setting(QStringLiteral("sync.user")) << nl
          << "Device:               " << db.setting(QStringLiteral("sync.device")) << nl
          << "Last sync:            " << formatTime(db.setting(QStringLiteral("sync.last_sync"))) << nl
          << "Subscriptions since:  " << db.setting(QStringLiteral("sync.subs_since"), QStringLiteral("-")) << nl
          << "Actions since:        " << db.setting(QStringLiteral("sync.actions_since"), QStringLiteral("-")) << nl
          << "To upload:            " << counts.outboxSubscriptions << " subscription changes, "
          << counts.outboxActions << " episode actions" << nl
          << "Waiting for episodes: " << counts.pendingActions << " received actions" << nl;
    return 0;
}

int cmdSync(QCoreApplication &app, Database &db, const QString &dbPath)
{
    if (!db.isSyncConfigured()) {
        err() << "No sync account - run sync-setup first." << nl;
        return 1;
    }
    FeedUpdater updater(&db);
    setFeedPasswords(updater, db, dbPath);
    SyncEngine engine(&db, &updater);
    engine.setPassword(loadCredentials(dbPath).value(QStringLiteral("sync")).toString());

    int exitCode = 0;
    QObject::connect(&engine, &SyncEngine::progress, [](const QString &step) {
        out() << "  ... " << step << nl;
    });
    QObject::connect(&updater, &FeedUpdater::podcastFailed, [&](qint64 id, const QString &error) {
        out() << "  FAIL  " << podcastLabel(db, id) << ": " << error << nl;
    });
    QObject::connect(&engine, &SyncEngine::finished, [&](const SyncResult &r) {
        out() << (r.firstSync ? "First sync done" : "Sync done") << " in " << r.elapsedMs << " ms" << nl
              << "  Subscriptions: " << r.podcastsAdded << " added here, " << r.podcastsRemoved
              << " removed here, " << r.subscriptionsUploaded << " changes uploaded" << nl;
        if (r.feedsLoaded > 0) {
            out() << "  Feeds loaded:  " << r.feedsLoaded << nl;
        }
        out() << "  Episodes:      " << r.actionsReceived << " actions received, " << r.applied.applied
              << " applied, " << r.applied.older << " older than the local state, " << r.applied.pending
              << " without matching episode (kept)" << nl
              << "                 " << r.actionsUploaded << " local actions uploaded" << nl;
        app.quit();
    });
    QObject::connect(&engine, &SyncEngine::failed, [&](const QString &error) {
        err() << "Sync failed: " << error << nl;
        exitCode = 2;
        app.quit();
    });

    // Started from the event loop (signals may be emitted right away)
    QTimer::singleShot(0, &engine, [&engine]() { engine.start(); });
    app.exec();
    return exitCode;
}

// --- Local listening state (to test the upload) ---

int cmdPlay(Database &db, qint64 id, const QString &position)
{
    const EpisodeRow e = db.episode(id);
    if (!e.id) {
        err() << "No episode with id " << id << nl;
        return 1;
    }
    bool ok = false;
    int seconds = position.toInt(&ok);
    if (!ok) {
        // Also accepts mm:ss and h:mm:ss
        const QStringList parts = position.split(QLatin1Char(':'));
        seconds = 0;
        ok = !parts.isEmpty() && parts.size() <= 3;
        for (int i = 0; ok && i < parts.size(); ++i) {
            seconds = seconds * 60 + parts.at(i).toInt(&ok);
        }
    }
    if (!ok || seconds < 0) {
        err() << "Invalid position: " << position << " (seconds, mm:ss or h:mm:ss)" << nl;
        return 1;
    }
    if (!db.recordPlayback(id, e.played ? 0 : e.position, seconds, e.duration)) {
        err() << db.lastError() << nl;
        return 1;
    }
    const EpisodeRow after = db.episode(id);
    out() << e.title << ": " << (after.played ? QStringLiteral("played")
                                              : QStringLiteral("at %1").arg(formatDuration(after.position)))
          << (db.isSyncConfigured() ? " (queued for the sync)" : "") << nl;
    return 0;
}

int cmdMark(Database &db, qint64 id, bool played)
{
    const EpisodeRow e = db.episode(id);
    if (!e.id) {
        err() << "No episode with id " << id << nl;
        return 1;
    }
    if (!(played ? db.markPlayed(id) : db.markUnplayed(id))) {
        err() << db.lastError() << nl;
        return 1;
    }
    out() << e.title << ": " << (played ? "played" : "unplayed")
          << (db.isSyncConfigured() ? " (queued for the sync)" : "") << nl;
    return 0;
}


// --- Downloads and archive (stage 3) ---

QString formatSize(qint64 bytes)
{
    if (bytes >= 1024LL * 1024 * 1024) {
        return QString::number(bytes / (1024.0 * 1024 * 1024), 'f', 2) + QStringLiteral(" GB");
    }
    return QString::number(bytes / (1024.0 * 1024), 'f', 1) + QStringLiteral(" MB");
}

QString defaultDownloadDir(const QString &dbPath)
{
    return QFileInfo(dbPath).absolutePath() + QStringLiteral("/downloads");
}

QString defaultArchiveDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Podcasts");
}

QString downloadDir(Database &db, const QString &dbPath)
{
    return db.setting(QStringLiteral("downloads.dir"), defaultDownloadDir(dbPath));
}

QString archiveDir(Database &db)
{
    return db.setting(QStringLiteral("archive.dir"), defaultArchiveDir());
}

// Clears the progress line on stderr before a normal line is printed
void clearProgressLine()
{
    err() << "\r" << QString(72, QLatin1Char(' ')) << "\r";
    err().flush();
}

// Shows the progress of running downloads on stderr (one updating line)
// and the finished ones on stdout
void reportDownloads(DownloadManager &downloads, Database &db, int *ok, int *failed)
{
    QSharedPointer<QHash<qint64, qint64> > received(new QHash<qint64, qint64>);
    QSharedPointer<QElapsedTimer> timer(new QElapsedTimer);
    timer->start();
    QObject::connect(&downloads, &DownloadManager::progress,
                     [received, timer](qint64 id, qint64 bytes, qint64) {
        received->insert(id, bytes);
        if (timer->elapsed() > 1000) {
            timer->restart();
            qint64 total = 0;
            for (QHash<qint64, qint64>::const_iterator it = received->constBegin(); it != received->constEnd(); ++it) {
                total += it.value();
            }
            err() << "\r  ... " << received->size() << " running, " << formatSize(total) << " loaded   ";
            err().flush();
        }
    });
    QObject::connect(&downloads, &DownloadManager::resumed, [&db](qint64 id, qint64 offset) {
        clearProgressLine();
        out() << "  resuming " << elide(db.episode(id).title, 50) << " at " << formatSize(offset) << nl;
    });
    QObject::connect(&downloads, &DownloadManager::downloaded, [&db, ok, received](qint64 id, const QString &path) {
        received->remove(id);
        ++*ok;
        clearProgressLine();
        out() << "  ok    " << elide(db.episode(id).title, 60) << "  (" << formatSize(QFileInfo(path).size())
              << ")" << nl;
    });
    QObject::connect(&downloads, &DownloadManager::failed, [&db, failed, received](qint64 id, const QString &error) {
        received->remove(id);
        ++*failed;
        clearProgressLine();
        out() << "  FAIL  " << elide(db.episode(id).title, 60) << ": " << error << nl;
    });
}

int cmdDownload(QCoreApplication &app, Database &db, const QString &dbPath, const QStringList &ids, int parallel)
{
    FeedUpdater updater(&db);
    setFeedPasswords(updater, db, dbPath);
    DownloadManager downloads(&db, &updater);
    downloads.setDownloadDirectory(downloadDir(db, dbPath));
    downloads.setMaxParallel(parallel);
    const QJsonObject credentials = loadCredentials(dbPath);
    const QList<PodcastRow> podcasts = db.podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        if (!podcasts.at(i).authUser.isEmpty()) {
            downloads.setPassword(podcasts.at(i).id, credentials.value(podcasts.at(i).url).toString());
        }
    }
    int ok = 0, failed = 0;
    reportDownloads(downloads, db, &ok, &failed);
    QObject::connect(&downloads, &DownloadManager::finished, &app, &QCoreApplication::quit);
    for (int i = 0; i < ids.size(); ++i) {
        downloads.download(ids.at(i).toLongLong());
    }
    if (ids.isEmpty()) {
        err() << "Usage: download EPISODE_ID [...]" << nl;
        return 1;
    }
    QElapsedTimer timer;
    timer.start();
    app.exec();
    out() << ok << " downloaded, " << failed << " failed in " << timer.elapsed() / 1000 << " s ("
          << downloadDir(db, dbPath) << ")" << nl;
    return failed > 0 ? 2 : 0;
}

int cmdDownloads(Database &db)
{
    const QList<EpisodeRow> episodes = db.episodes(-1, -1, Database::Downloaded);
    qint64 total = 0;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        const qint64 size = QFileInfo(e.downloadPath).size();
        total += size;
        out() << QStringLiteral("%1  %2  %3  %4  %5")
                 .arg(e.id, 6)
                 .arg(formatSize(size), 10)
                 .arg(e.played ? QLatin1Char('P') : QLatin1Char('.'))
                 .arg(elide(e.podcastTitle, 20), -20)
                 .arg(elide(e.title, 50)) << nl
              << "          " << e.downloadPath << nl;
    }
    out() << episodes.size() << " downloaded episodes, " << formatSize(total) << nl;
    return 0;
}

int cmdCleanup(Database &db, const QString &dbPath, const QStringList &args)
{
    const int days = args.isEmpty() ? db.setting(QStringLiteral("cleanup.days"), QStringLiteral("7")).toInt()
                                    : args.first().toInt();
    DownloadManager downloads(&db, nullptr);
    downloads.setDownloadDirectory(downloadDir(db, dbPath));
    const CleanupResult r = downloads.cleanup(days, archiveDir(db));
    out() << r.files << " files deleted (" << formatSize(r.bytes) << "): played more than " << days
          << " days ago, archived podcasts and the archive folder excluded" << nl;
    return 0;
}

int cmdArchive(QCoreApplication &app, Database &db, const QString &dbPath, qint64 podcastId, int parallel)
{
    const PodcastRow podcast = db.podcast(podcastId);
    if (!podcast.id) {
        err() << "No podcast with id " << podcastId << nl;
        return 1;
    }
    FeedUpdater updater(&db);
    setFeedPasswords(updater, db, dbPath);
    DownloadManager downloads(&db, &updater);
    downloads.setDownloadDirectory(downloadDir(db, dbPath));
    downloads.setMaxParallel(parallel);
    const QJsonObject credentials = loadCredentials(dbPath);
    if (!podcast.authUser.isEmpty()) {
        downloads.setPassword(podcast.id, credentials.value(podcast.url).toString());
    }
    Archiver archiver(&db, &updater, &downloads);
    archiver.setArchiveRoot(archiveDir(db));

    int ok = 0, failed = 0, exitCode = 0;
    reportDownloads(downloads, db, &ok, &failed);
    QObject::connect(&updater, &FeedUpdater::podcastFailed, [](qint64, const QString &error) {
        out() << "  Feed not available (" << error << ") - archiving the known episodes" << nl;
    });
    QObject::connect(&archiver, &Archiver::progress, [](const QString &step) {
        clearProgressLine();
        out() << "  ... " << step << nl;
    });
    QObject::connect(&archiver, &Archiver::finished, [&](const ArchiveResult &r) {
        clearProgressLine();
        out() << "Archive of " << podcast.title << ": " << r.folder << nl
              << "  Feed saved:   " << (r.feedSaved ? "yes" : "no") << ", cover: " << (r.coverSaved ? "yes" : "no") << nl
              << "  Episodes:     " << r.episodes << " known, " << r.present << " already there, " << r.moved
              << " moved from the download folder, " << r.downloaded << " downloaded, " << r.failures.size()
              << " failed" << nl;
        for (int i = 0; i < r.failures.size(); ++i) {
            out() << "    " << r.failures.at(i) << nl;
        }
        exitCode = r.failures.isEmpty() ? 0 : 2;
        app.quit();
    });
    QObject::connect(&archiver, &Archiver::failed, [&](const QString &error) {
        err() << "Archive failed: " << error << nl;
        exitCode = 1;
        app.quit();
    });
    QTimer::singleShot(0, &archiver, [&archiver, podcastId]() { archiver.archive(podcastId); });
    app.exec();
    return exitCode;
}

int cmdArchived(Database &db, qint64 podcastId, const QString &value)
{
    const PodcastRow podcast = db.podcast(podcastId);
    if (!podcast.id || (value != QLatin1String("on") && value != QLatin1String("off"))) {
        err() << "Usage: archived PODCAST_ID on|off" << nl;
        return 1;
    }
    const bool on = value == QLatin1String("on");
    db.setArchived(podcastId, on);
    out() << podcast.title << ": " << (on ? "archived - no more updates, downloads are kept"
                                         : "active again - updated with the others") << nl;
    return 0;
}

int cmdImportArchive(Database &db, const QString &folder)
{
    Archiver archiver(&db, nullptr, nullptr);
    archiver.setArchiveRoot(archiveDir(db));
    const ArchiveImportResult r = archiver.importFolder(folder);
    if (!r.ok) {
        err() << "Import failed: " << r.error << nl;
        return 1;
    }
    out() << podcastLabel(db, r.podcastId) << (r.podcastAdded ? " (added)" : " (already subscribed)") << nl
          << "  " << r.episodesAdded << " episodes added, " << r.filesFound << " files linked, "
          << r.statesTaken << " newer listening states taken over" << nl;
    return 0;
}

int cmdSources(Database &db, qint64 episodeId)
{
    const QList<PlaybackSource> sources = Archiver::playbackSources(&db, episodeId);
    if (sources.isEmpty()) {
        err() << "No episode with id " << episodeId << " or no source" << nl;
        return 1;
    }
    for (int i = 0; i < sources.size(); ++i) {
        const PlaybackSource &s = sources.at(i);
        const char *kind = s.kind == PlaybackSource::LocalFile ? "local file "
                         : s.kind == PlaybackSource::Original ? "original   " : "cloud      ";
        out() << "  " << (i + 1) << ". " << kind << "  " << s.url.toString() << nl;
    }
    return 0;
}

int cmdLoadChapters(QCoreApplication &app, Database &db, const QStringList &args)
{
    const QList<qint64> ids = db.episodesMissingChapters(args.isEmpty() ? -1 : args.first().toLongLong());
    if (ids.isEmpty()) {
        out() << "No episodes with a podcast:chapters file missing its chapters." << nl;
        return 0;
    }
    ChaptersLoader loader(&db);
    int ok = 0, failed = 0;
    QObject::connect(&loader, &ChaptersLoader::loaded, [&](qint64 id, int count) {
        ++ok;
        out() << "  ok    " << elide(db.episode(id).title, 60) << ": " << count << " chapters" << nl;
    });
    QObject::connect(&loader, &ChaptersLoader::failed, [&](qint64 id, const QString &error) {
        ++failed;
        out() << "  FAIL  " << elide(db.episode(id).title, 60) << ": " << error << nl;
    });
    QObject::connect(&loader, &ChaptersLoader::finished, &app, &QCoreApplication::quit);
    loader.load(ids);
    app.exec();
    out() << ok << " loaded, " << failed << " failed" << nl;
    return 0;
}

int cmdQueue(Database &db, const QStringList &args)
{
    const QString sub = args.isEmpty() ? QStringLiteral("list") : args.first();
    if (sub == QLatin1String("add") || sub == QLatin1String("next")) {
        for (int i = 1; i < args.size(); ++i) {
            const EpisodeRow e = db.episode(args.at(i).toLongLong());
            if (!e.id) {
                err() << "No episode with id " << args.at(i) << nl;
                return 1;
            }
            db.queueAdd(e.id, sub == QLatin1String("next"));
            out() << (sub == QLatin1String("next") ? "next: " : "added: ") << e.title << nl;
        }
        return 0;
    }
    if (sub == QLatin1String("remove") && args.size() > 1) {
        db.queueRemove(args.at(1).toLongLong());
        return 0;
    }
    if (sub == QLatin1String("move") && args.size() > 2) {
        db.queueMove(args.at(1).toLongLong(), args.at(2).toInt() - 1);
        return 0;
    }
    if (sub == QLatin1String("clear")) {
        db.queueClear();
        out() << "Playlist cleared" << nl;
        return 0;
    }
    if (sub != QLatin1String("list")) {
        err() << "Usage: queue [list] | add ID... | next ID... | remove ID | move ID POSITION | clear" << nl;
        return 1;
    }
    const QList<EpisodeRow> episodes = db.episodes(-1, -1, Database::Queue);
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        out() << QStringLiteral("%1. %2  %3  %4").arg(i + 1, 3).arg(e.id, 6)
                 .arg(elide(e.podcastTitle, 20), -20).arg(elide(e.title, 60)) << nl;
    }
    out() << episodes.size() << " episodes in the playlist" << nl;
    return 0;
}

int cmdSettings(Database &db, const QString &dbPath, const QStringList &args)
{
    static const char *const keys[][2] = {
        { "downloads.dir", "folder for normal downloads" },
        { "archive.dir", "archive root folder" },
        { "archive.url", "cloud archive base URL, e.g. http://127.0.0.1:8080/ (rclone serve http)" },
        { "cleanup.days", "cleanup: delete downloads played more than N days ago (default 7)" },
    };
    if (args.size() >= 2) {
        bool known = false;
        for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
            known = known || args.at(0) == QLatin1String(keys[i][0]);
        }
        if (!known) {
            err() << "Unknown setting " << args.at(0) << " (see \"settings\")" << nl;
            return 1;
        }
        db.setSetting(args.at(0), args.mid(1).join(QLatin1Char(' ')));
        out() << args.at(0) << " = " << db.setting(args.at(0)) << nl;
        return 0;
    }
    const QString defaults[] = { defaultDownloadDir(dbPath), defaultArchiveDir(), QString(), QStringLiteral("7") };
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); ++i) {
        const QString key = QLatin1String(keys[i][0]);
        const QString value = db.setting(key);
        out() << QStringLiteral("%1  %2").arg(key, -14)
                 .arg(value.isEmpty() ? (defaults[i].isEmpty() ? QStringLiteral("(not set)")
                                                               : defaults[i] + QStringLiteral("  (default)"))
                                      : value) << nl
              << "                " << keys[i][1] << nl;
    }
    return 0;
}
} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("sailcast-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral("1.1 (build 105)"));

    const QString defaultDb = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation)
            + QStringLiteral("/sailcast-cli/sailcast.db");

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
            "Tests the Sailcast core on the desktop.\n\n"
            "Commands:\n"
            "  import-opml FILE       add the feeds of an OPML file\n"
            "  import-gpodder FILE    import a gpodder-sailfish database (file \"Database\")\n"
            "  export-opml FILE       write all subscriptions to an OPML file\n"
            "  subscribe URL          add a feed and load it\n"
            "  remove ID              remove a podcast and its episodes\n"
            "  update [ID ...]        update all (or the given) podcasts\n"
            "  list                   podcasts with unplayed/all and new counts\n"
            "  episodes [ID]          episodes of one podcast or of all (newest first)\n"
            "  chapters EPISODE_ID    chapters of an episode\n"
            "  parse-file FILE        parse a local feed file (no database)\n"
            "  check URL              check an address before subscribing (no database)\n"
            "  search TERM            search podcasts in the Podcast Index (no database)\n"
            "  auth ID USER           set the login of a feed (asks for the password)\n"
            "  auth ID                remove the login of a feed\n"
            "  stats                  totals\n"
            "\nSync:\n"
            "  sync-setup nextcloud|gpodder SERVER USER [DEVICE]\n"
            "                         set the sync account (asks for the password)\n"
            "  sync                   synchronize subscriptions and listening state\n"
            "  sync-status            account, last sync, waiting changes\n"
            "  play EPISODE_ID POS    set the playback position (seconds, mm:ss, h:mm:ss)\n"
            "  mark-played EPISODE_ID\n"
            "  mark-unplayed EPISODE_ID\n"
            "\nDownloads and archive:\n"
            "  download EPISODE_ID [...]  download episodes (resumes interrupted ones)\n"
            "  downloads              downloaded episodes with path and size\n"
            "  cleanup [DAYS]         delete downloads played more than DAYS days ago\n"
            "  archive PODCAST_ID     all episodes + feed.xml + cover + episodes.json\n"
            "  archived PODCAST_ID on|off  archived state (no more feed updates)\n"
            "  import-archive FOLDER  import an archive folder (episodes.json)\n"
            "  sources EPISODE_ID     playback sources in the order they are tried\n"
            "  load-chapters [PODCAST_ID]  load podcast:chapters (JSON) files\n"
            "  queue [list|add ID...|next ID...|remove ID|move ID POS|clear]  playlist\n"
            "  settings               show the settings\n"
            "  set KEY VALUE          change a setting"));
    parser.addHelpOption();
    parser.addVersionOption();
    QCommandLineOption dbOption(QStringList() << QStringLiteral("d") << QStringLiteral("db"),
                                QStringLiteral("Database file (default: %1).").arg(defaultDb),
                                QStringLiteral("file"), defaultDb);
    QCommandLineOption limitOption(QStringList() << QStringLiteral("n") << QStringLiteral("limit"),
                                   QStringLiteral("Number of episodes to show (default 30, -1 = all)."),
                                   QStringLiteral("count"), QStringLiteral("30"));
    QCommandLineOption unplayedOption(QStringList() << QStringLiteral("u") << QStringLiteral("unplayed"),
                                      QStringLiteral("Only unplayed episodes."));
    QCommandLineOption inProgressOption(QStringList() << QStringLiteral("i") << QStringLiteral("in-progress"),
                                        QStringLiteral("Only episodes in progress (most recently played first)."));
    QCommandLineOption downloadedOption(QStringList() << QStringLiteral("D") << QStringLiteral("downloaded"),
                                        QStringLiteral("Only downloaded episodes."));
    QCommandLineOption parallelOption(QStringList() << QStringLiteral("p") << QStringLiteral("parallel"),
                                      QStringLiteral("Feeds fetched at the same time (default 4)."),
                                      QStringLiteral("count"), QStringLiteral("4"));
    parser.addOption(dbOption);
    parser.addOption(limitOption);
    parser.addOption(unplayedOption);
    parser.addOption(inProgressOption);
    parser.addOption(downloadedOption);
    parser.addOption(parallelOption);
    parser.addPositionalArgument(QStringLiteral("command"), QStringLiteral("See the list above."));
    parser.process(app);

    QStringList args = parser.positionalArguments();
    if (args.isEmpty()) {
        parser.showHelp(1);
    }
    const QString command = args.takeFirst();

    // Works without a database
    if (command == QLatin1String("search")) {
        if (args.isEmpty()) {
            err() << command << ": TERM missing" << nl;
            return 1;
        }
        return cmdSearch(app, args.join(QLatin1Char(' ')));
    }
    if (command == QLatin1String("check")) {
        if (args.isEmpty()) {
            err() << command << ": URL missing" << nl;
            return 1;
        }
        return cmdCheck(app, args.first());
    }
    if (command == QLatin1String("parse-file")) {
        if (args.isEmpty()) {
            err() << command << ": FILE missing" << nl;
            return 1;
        }
        return cmdParseFile(args.first());
    }

    const QString dbPath = parser.value(dbOption);
    QDir().mkpath(QFileInfo(dbPath).absolutePath());
    Database db;
    if (!db.open(dbPath)) {
        err() << "Cannot open " << dbPath << ": " << db.lastError() << nl;
        return 1;
    }

    const auto needArg = [&](const QString &what) -> bool {
        if (args.isEmpty()) {
            err() << command << ": " << what << " missing" << nl;
            return false;
        }
        return true;
    };

    if (command == QLatin1String("import-opml")) {
        return needArg(QStringLiteral("FILE")) ? cmdImportOpml(db, args.first()) : 1;
    }
    if (command == QLatin1String("export-opml")) {
        return needArg(QStringLiteral("FILE")) ? cmdExportOpml(db, args.first()) : 1;
    }
    if (command == QLatin1String("import-gpodder")) {
        return needArg(QStringLiteral("FILE")) ? cmdImportGpodder(db, args.first()) : 1;
    }
    if (command == QLatin1String("subscribe")) {
        return needArg(QStringLiteral("URL"))
                ? cmdUpdate(app, db, dbPath, QStringList(), args.first(), parser.value(parallelOption).toInt()) : 1;
    }
    if (command == QLatin1String("remove")) {
        if (!needArg(QStringLiteral("ID"))) {
            return 1;
        }
        const QString title = podcastLabel(db, args.first().toLongLong());
        if (!db.removePodcast(args.first().toLongLong())) {
            err() << "No podcast with id " << args.first() << nl;
            return 1;
        }
        out() << "Removed " << title << nl;
        return 0;
    }
    if (command == QLatin1String("update")) {
        return cmdUpdate(app, db, dbPath, args, QString(), parser.value(parallelOption).toInt());
    }
    if (command == QLatin1String("list")) {
        return cmdList(db);
    }
    if (command == QLatin1String("episodes")) {
        const qint64 id = args.isEmpty() ? -1 : args.first().toLongLong();
        Database::EpisodeFilter filter = Database::AllEpisodes;
        if (parser.isSet(inProgressOption)) {
            filter = Database::InProgress;
        } else if (parser.isSet(downloadedOption)) {
            filter = Database::Downloaded;
        } else if (parser.isSet(unplayedOption)) {
            filter = Database::Unplayed;
        }
        return cmdEpisodes(db, id, parser.value(limitOption).toInt(), filter);
    }
    if (command == QLatin1String("chapters")) {
        return needArg(QStringLiteral("EPISODE_ID")) ? cmdChapters(db, args.first().toLongLong()) : 1;
    }
    if (command == QLatin1String("auth")) {
        if (!needArg(QStringLiteral("ID"))) {
            return 1;
        }
        return cmdAuth(db, dbPath, args.first().toLongLong(), args.size() > 1 ? args.at(1) : QString());
    }
    if (command == QLatin1String("sync-setup")) {
        return cmdSyncSetup(db, dbPath, args);
    }
    if (command == QLatin1String("sync")) {
        return cmdSync(app, db, dbPath);
    }
    if (command == QLatin1String("sync-status")) {
        return cmdSyncStatus(db);
    }
    if (command == QLatin1String("play")) {
        if (args.size() < 2) {
            err() << "Usage: play EPISODE_ID POSITION" << nl;
            return 1;
        }
        return cmdPlay(db, args.at(0).toLongLong(), args.at(1));
    }
    if (command == QLatin1String("mark-played") || command == QLatin1String("mark-unplayed")) {
        return needArg(QStringLiteral("EPISODE_ID"))
                ? cmdMark(db, args.first().toLongLong(), command == QLatin1String("mark-played")) : 1;
    }
    if (command == QLatin1String("download")) {
        // Downloads default to 2 at a time, feeds to 4
        const int parallel = parser.isSet(parallelOption) ? parser.value(parallelOption).toInt() : 2;
        return cmdDownload(app, db, dbPath, args, parallel);
    }
    if (command == QLatin1String("downloads")) {
        return cmdDownloads(db);
    }
    if (command == QLatin1String("cleanup")) {
        return cmdCleanup(db, dbPath, args);
    }
    if (command == QLatin1String("archive")) {
        const int parallel = parser.isSet(parallelOption) ? parser.value(parallelOption).toInt() : 2;
        return needArg(QStringLiteral("PODCAST_ID")) ? cmdArchive(app, db, dbPath, args.first().toLongLong(), parallel) : 1;
    }
    if (command == QLatin1String("archived")) {
        if (args.size() < 2) {
            err() << "Usage: archived PODCAST_ID on|off" << nl;
            return 1;
        }
        return cmdArchived(db, args.at(0).toLongLong(), args.at(1));
    }
    if (command == QLatin1String("import-archive")) {
        return needArg(QStringLiteral("FOLDER")) ? cmdImportArchive(db, args.first()) : 1;
    }
    if (command == QLatin1String("sources")) {
        return needArg(QStringLiteral("EPISODE_ID")) ? cmdSources(db, args.first().toLongLong()) : 1;
    }
    if (command == QLatin1String("load-chapters")) {
        return cmdLoadChapters(app, db, args);
    }
    if (command == QLatin1String("queue")) {
        return cmdQueue(db, args);
    }
    if (command == QLatin1String("settings") || command == QLatin1String("set")) {
        return cmdSettings(db, dbPath, command == QLatin1String("set") ? args : QStringList());
    }
    if (command == QLatin1String("stats")) {
        return cmdStats(db);
    }

    err() << "Unknown command: " << command << " (see --help)" << nl;
    return 1;
}

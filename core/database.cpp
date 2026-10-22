// SPDX-License-Identifier: GPL-3.0-or-later
#include "database.h"
#include "sqltext.h"
#include "urlmask.h"

#include <QCollator>
#include <QCryptographicHash>
#include <QDateTime>
#include <QHash>
#include <QPair>
#include <QSet>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include <algorithm>

namespace {

const int SchemaVersion = 7;

// Schema version 1. Text columns are NOT NULL with '' as default so that
// the code never has to tell NULL and empty apart (exception: last_error,
// where NULL means "no error").
const char *const SchemaV1[] = {
    "CREATE TABLE podcasts ("
    " id INTEGER PRIMARY KEY,"
    " url TEXT NOT NULL UNIQUE,"
    " title TEXT NOT NULL DEFAULT '',"
    " custom_title TEXT NOT NULL DEFAULT '',"
    " link TEXT NOT NULL DEFAULT '',"
    " description TEXT NOT NULL DEFAULT '',"
    " author TEXT NOT NULL DEFAULT '',"
    " cover_url TEXT NOT NULL DEFAULT '',"
    " section TEXT NOT NULL DEFAULT '',"
    " auth_user TEXT NOT NULL DEFAULT '',"
    " etag TEXT NOT NULL DEFAULT '',"
    " last_modified TEXT NOT NULL DEFAULT '',"
    " last_update INTEGER NOT NULL DEFAULT 0,"
    " last_error TEXT,"
    " paused INTEGER NOT NULL DEFAULT 0,"
    " added INTEGER NOT NULL DEFAULT 0)",

    "CREATE TABLE episodes ("
    " id INTEGER PRIMARY KEY,"
    " podcast_id INTEGER NOT NULL REFERENCES podcasts(id) ON DELETE CASCADE,"
    " guid TEXT NOT NULL,"
    " url TEXT NOT NULL DEFAULT '',"
    " title TEXT NOT NULL DEFAULT '',"
    " description TEXT NOT NULL DEFAULT '',"
    " description_html TEXT NOT NULL DEFAULT '',"
    " link TEXT NOT NULL DEFAULT '',"
    " mime_type TEXT NOT NULL DEFAULT '',"
    " file_size INTEGER NOT NULL DEFAULT 0,"
    " published INTEGER NOT NULL DEFAULT 0,"
    " duration INTEGER NOT NULL DEFAULT 0,"
    " image_url TEXT NOT NULL DEFAULT '',"
    " chapters_url TEXT NOT NULL DEFAULT '',"
    " chapter_count INTEGER NOT NULL DEFAULT 0,"
    " meta_hash TEXT NOT NULL DEFAULT '',"
    " is_new INTEGER NOT NULL DEFAULT 0,"
    " played INTEGER NOT NULL DEFAULT 0,"
    " position INTEGER NOT NULL DEFAULT 0,"
    " position_updated INTEGER NOT NULL DEFAULT 0,"
    " last_played INTEGER NOT NULL DEFAULT 0,"
    " download_state INTEGER NOT NULL DEFAULT 0,"
    " download_path TEXT NOT NULL DEFAULT '',"
    " added INTEGER NOT NULL DEFAULT 0,"
    " UNIQUE(podcast_id, guid))",

    "CREATE INDEX episodes_by_podcast ON episodes(podcast_id, published DESC)",
    "CREATE INDEX episodes_by_date ON episodes(published DESC)",
    "CREATE INDEX episodes_unplayed ON episodes(podcast_id, played)",

    "CREATE TABLE chapters ("
    " episode_id INTEGER NOT NULL REFERENCES episodes(id) ON DELETE CASCADE,"
    " start_ms INTEGER NOT NULL,"
    " title TEXT NOT NULL DEFAULT '',"
    " href TEXT NOT NULL DEFAULT '',"
    " image TEXT NOT NULL DEFAULT '')",

    "CREATE INDEX chapters_by_episode ON chapters(episode_id, start_ms)",
    nullptr
};

// Schema version 2: settings and sync (stage 2)
const char *const SchemaV2[] = {
    "CREATE TABLE settings ("
    " key TEXT PRIMARY KEY,"
    " value TEXT NOT NULL DEFAULT '')",

    // Subscription changes not yet uploaded: one row per feed URL with the
    // latest change ("add" or "remove")
    "CREATE TABLE sync_subscriptions ("
    " url TEXT PRIMARY KEY,"
    " kind TEXT NOT NULL,"
    " created INTEGER NOT NULL DEFAULT 0)",

    // Episode actions not yet uploaded (only the latest per episode is kept)
    "CREATE TABLE sync_outbox ("
    " id INTEGER PRIMARY KEY,"
    " podcast_url TEXT NOT NULL DEFAULT '',"
    " episode_url TEXT NOT NULL DEFAULT '',"
    " guid TEXT NOT NULL DEFAULT '',"
    " action TEXT NOT NULL DEFAULT '',"
    " timestamp INTEGER NOT NULL DEFAULT 0,"
    " started INTEGER NOT NULL DEFAULT -1,"
    " position INTEGER NOT NULL DEFAULT -1,"
    " total INTEGER NOT NULL DEFAULT -1)",

    // Episode actions received from the server that did not match an
    // episode yet (e.g. the feed was not loaded yet)
    "CREATE TABLE sync_pending ("
    " id INTEGER PRIMARY KEY,"
    " podcast_url TEXT NOT NULL DEFAULT '',"
    " episode_url TEXT NOT NULL DEFAULT '',"
    " guid TEXT NOT NULL DEFAULT '',"
    " action TEXT NOT NULL DEFAULT '',"
    " timestamp INTEGER NOT NULL DEFAULT 0,"
    " started INTEGER NOT NULL DEFAULT -1,"
    " position INTEGER NOT NULL DEFAULT -1,"
    " total INTEGER NOT NULL DEFAULT -1,"
    " received INTEGER NOT NULL DEFAULT 0)",

    "CREATE INDEX episodes_by_guid ON episodes(guid)",
    "CREATE INDEX episodes_by_url ON episodes(url)",
    nullptr
};

// Schema version 3: downloads and archive (stage 3). download_state and
// download_path exist since version 1.
const char *const SchemaV3[] = {
    "ALTER TABLE podcasts ADD COLUMN archived INTEGER NOT NULL DEFAULT 0",
    "ALTER TABLE podcasts ADD COLUMN archive_folder TEXT NOT NULL DEFAULT ''",
    "ALTER TABLE episodes ADD COLUMN archive_file TEXT NOT NULL DEFAULT ''",
    "CREATE INDEX episodes_downloaded ON episodes(download_state)",
    nullptr
};

// Schema version 4: playlist (stage 4)
const char *const SchemaV4[] = {
    "CREATE TABLE queue ("
    " episode_id INTEGER PRIMARY KEY REFERENCES episodes(id) ON DELETE CASCADE,"
    " position INTEGER NOT NULL)",
    nullptr
};

// Schema version 5: folders for podcasts (45). One folder per podcast
// (folder_id NULL = none); manual_update leaves all podcasts of the folder
// out of "update all". Local only - the sync has no folders.
const char *const SchemaV5[] = {
    "CREATE TABLE folders ("
    " id INTEGER PRIMARY KEY,"
    " name TEXT NOT NULL DEFAULT '',"
    " sort_order INTEGER NOT NULL DEFAULT 0,"
    " manual_update INTEGER NOT NULL DEFAULT 0)",
    "ALTER TABLE podcasts ADD COLUMN folder_id INTEGER REFERENCES folders(id) ON DELETE SET NULL",
    "CREATE INDEX podcasts_folder ON podcasts(folder_id)",
    nullptr
};

// Schema version 6: bookmarks (47) - places in an episode to come back to,
// with an optional note. Local only (the sync has no bookmarks); deleted
// with their episode.
const char *const SchemaV6[] = {
    "CREATE TABLE bookmarks ("
    " id INTEGER PRIMARY KEY,"
    " episode_id INTEGER NOT NULL REFERENCES episodes(id) ON DELETE CASCADE,"
    " position INTEGER NOT NULL DEFAULT 0,"
    " note TEXT NOT NULL DEFAULT '',"
    " created INTEGER NOT NULL DEFAULT 0)",
    "CREATE INDEX bookmarks_episode ON bookmarks(episode_id, position)",
    nullptr
};

// Schema version 7: a name of its own for a bookmark (50), shown instead of
// "Bookmark"; the note stays separate
const char *const SchemaV7[] = {
    "ALTER TABLE bookmarks ADD COLUMN title TEXT NOT NULL DEFAULT ''",
    nullptr
};

// Same rule as gPodder's is_finished(): within the last 10 seconds or 99 %
bool isFinishedPosition(int position, int total)
{
    return position > 0 && total > 0 && (position + 10 >= total || position >= total * 0.99);
}

// Media URL without query string (tracking parameters often change)
QString withoutQuery(const QString &url)
{
    const int question = url.indexOf(QLatin1Char('?'));
    return question < 0 ? url : url.left(question);
}

// The other scheme of a feed URL (http <-> https), empty if neither
QString otherScheme(const QString &url)
{
    if (url.startsWith(QLatin1String("https://"), Qt::CaseInsensitive)) {
        return QStringLiteral("http://") + url.mid(8);
    }
    if (url.startsWith(QLatin1String("http://"), Qt::CaseInsensitive)) {
        return QStringLiteral("https://") + url.mid(7);
    }
    return QString();
}

// Fingerprint of everything the feed says about an episode: existing
// episodes are only written again when it changes.
QString metaHash(const ParsedEpisode &e)
{
    QCryptographicHash hash(QCryptographicHash::Md5);
    const QStringList fields = QStringList()
            << e.title << e.description << e.descriptionHtml << e.link << e.url
            << e.mimeType << QString::number(e.fileSize) << QString::number(e.published)
            << QString::number(e.duration) << e.imageUrl << e.chaptersUrl;
    for (int i = 0; i < fields.size(); ++i) {
        hash.addData(fields.at(i).toUtf8());
        hash.addData("\x1f", 1);
    }
    for (int i = 0; i < e.chapters.size(); ++i) {
        const ParsedChapter &c = e.chapters.at(i);
        hash.addData(QString::number(c.startMs).toUtf8());
        hash.addData(c.title.toUtf8());
        hash.addData(c.href.toUtf8());
        hash.addData(c.image.toUtf8());
        hash.addData("\x1e", 1);
    }
    return QString::fromLatin1(hash.result().toHex());
}

} // namespace

Database::Database()
    : m_connection(QStringLiteral("sailcast-%1").arg(reinterpret_cast<quintptr>(this)))
{
}

Database::~Database()
{
    close();
}

qint64 Database::now()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000;
}

bool Database::fail(const QString &context, const QString &error) const
{
    m_lastError = context + QStringLiteral(": ") + error;
    qWarning("Sailcast database: %s", qPrintable(m_lastError));
    return false;
}

QSqlDatabase Database::handle() const
{
    return QSqlDatabase::database(m_connection, false);
}

bool Database::isOpen() const
{
    return QSqlDatabase::contains(m_connection) && handle().isOpen();
}

bool Database::open(const QString &path)
{
    close();
    {
        QSqlDatabase db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), m_connection);
        db.setDatabaseName(path);
        if (!db.open()) {
            return fail(QStringLiteral("open"), db.lastError().text());
        }
    }
    // WAL: readers do not block the writer, fewer fsyncs on feed updates
    exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    exec(QStringLiteral("PRAGMA synchronous=NORMAL"));
    exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    if (!migrate()) {
        close();
        return false;
    }
    return true;
}

void Database::close()
{
    if (QSqlDatabase::contains(m_connection)) {
        {
            QSqlDatabase db = QSqlDatabase::database(m_connection, false);
            if (db.isOpen()) {
                db.close();
            }
        }
        QSqlDatabase::removeDatabase(m_connection);
    }
}

bool Database::exec(const QString &sql)
{
    QSqlQuery q(handle());
    if (!q.exec(sql)) {
        return fail(sql.left(60), q.lastError().text());
    }
    return true;
}

bool Database::migrate()
{
    QSqlQuery q(handle());
    if (!q.exec(QStringLiteral("PRAGMA user_version")) || !q.next()) {
        return fail(QStringLiteral("user_version"), q.lastError().text());
    }
    const int version = q.value(0).toInt();
    q.finish();
    if (version > SchemaVersion) {
        return fail(QStringLiteral("migrate"),
                    QStringLiteral("database is from a newer Sailcast (schema %1)").arg(version));
    }

    QSqlDatabase db = handle();
    if (version < 1) {
        db.transaction();
        for (int i = 0; SchemaV1[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV1[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 1"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v1"), db.lastError().text());
        }
    }
    if (version < 2) {
        db.transaction();
        for (int i = 0; SchemaV2[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV2[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 2"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v2"), db.lastError().text());
        }
    }
    if (version < 3) {
        db.transaction();
        for (int i = 0; SchemaV3[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV3[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 3"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v3"), db.lastError().text());
        }
    }
    if (version < 4) {
        db.transaction();
        for (int i = 0; SchemaV4[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV4[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 4"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v4"), db.lastError().text());
        }
    }
    if (version < 5) {
        db.transaction();
        for (int i = 0; SchemaV5[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV5[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 5"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v5"), db.lastError().text());
        }
    }
    if (version < 6) {
        db.transaction();
        for (int i = 0; SchemaV6[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV6[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 6"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v6"), db.lastError().text());
        }
    }
    if (version < 7) {
        db.transaction();
        for (int i = 0; SchemaV7[i]; ++i) {
            if (!exec(QString::fromLatin1(SchemaV7[i]))) {
                db.rollback();
                return false;
            }
        }
        exec(QStringLiteral("PRAGMA user_version = 7"));
        if (!db.commit()) {
            return fail(QStringLiteral("migrate v7"), db.lastError().text());
        }
    }
    // Future steps: if (version < 8) { ... PRAGMA user_version = 8 ... }
    return true;
}

// --- Subscriptions ---------------------------------------------------------

qint64 Database::findPodcast(const QString &url) const
{
    const QString trimmed = url.trimmed();
    QSqlQuery q(handle());
    // Case-insensitive: other apps sometimes store the same feed with
    // different case (e.g. ".../Wandertag.rss" and ".../wandertag.rss")
    q.prepare(QStringLiteral("SELECT id FROM podcasts WHERE url = ? COLLATE NOCASE"
                             " OR url = ? COLLATE NOCASE LIMIT 1"));
    q.addBindValue(sqlText(trimmed));
    const QString other = otherScheme(trimmed);
    q.addBindValue(sqlText(other.isEmpty() ? trimmed : other));
    if (q.exec() && q.next()) {
        return q.value(0).toLongLong();
    }
    return -1;
}

qint64 Database::addPodcast(const QString &url, const QString &title, bool queueForSync)
{
    const QString trimmed = url.trimmed();
    if (trimmed.isEmpty()) {
        fail(QStringLiteral("addPodcast"), QStringLiteral("empty URL"));
        return -1;
    }
    const qint64 existing = findPodcast(trimmed);
    if (existing >= 0) {
        return existing;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO podcasts (url, title, added) VALUES (?, ?, ?)"));
    q.addBindValue(sqlText(trimmed));
    q.addBindValue(sqlText(title));
    q.addBindValue(now());
    if (!q.exec()) {
        fail(QStringLiteral("addPodcast"), q.lastError().text());
        return -1;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    if (queueForSync) {
        queueSubscription(QStringLiteral("add"), trimmed);
    }
    return id;
}

bool Database::removePodcast(qint64 id, bool queueForSync)
{
    const QString url = podcast(id).url;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM podcasts WHERE id = ?"));
    q.addBindValue(id);
    if (!q.exec()) {
        return fail(QStringLiteral("removePodcast"), q.lastError().text());
    }
    const bool removed = q.numRowsAffected() > 0;
    if (removed && queueForSync && !url.isEmpty()) {
        queueSubscription(QStringLiteral("remove"), url);
    }
    return removed;
}

QStringList Database::podcastUrls() const
{
    QStringList urls;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT url FROM podcasts ORDER BY id"))) {
        while (q.next()) {
            urls.append(q.value(0).toString());
        }
    }
    return urls;
}

static const char *const PodcastColumns =
        "SELECT p.id, p.url, COALESCE(NULLIF(p.custom_title, ''), NULLIF(p.title, ''), p.url),"
        " p.link, p.description, p.author, p.cover_url, p.last_update, p.last_error, p.auth_user,"
        " COUNT(e.id),"
        " SUM(CASE WHEN e.played = 0 THEN 1 ELSE 0 END),"
        " SUM(CASE WHEN e.played = 0 AND e.is_new = 1 THEN 1 ELSE 0 END),"
        " p.archived, p.archive_folder,"
        " SUM(CASE WHEN e.download_state = 2 THEN 1 ELSE 0 END),"
        " SUM(CASE WHEN e.played = 1 THEN e.duration WHEN e.position > 0 THEN e.position ELSE 0 END),"
        " p.paused, COALESCE(p.folder_id, 0),"
        // New episodes arrived after the podcast page was last opened (50)
        " COALESCE((SELECT CAST(value AS INTEGER) FROM settings WHERE key = 'podcast.' || p.id || '.newAt'), 0)"
        " > COALESCE((SELECT CAST(value AS INTEGER) FROM settings WHERE key = 'podcast.' || p.id || '.seenAt'), 0)"
        " FROM podcasts p LEFT JOIN episodes e ON e.podcast_id = p.id";

static PodcastRow podcastFromQuery(const QSqlQuery &q)
{
    PodcastRow row;
    row.id = q.value(0).toLongLong();
    row.url = q.value(1).toString();
    row.title = q.value(2).toString();
    row.link = q.value(3).toString();
    row.description = q.value(4).toString();
    row.author = q.value(5).toString();
    row.coverUrl = q.value(6).toString();
    row.lastUpdate = q.value(7).toLongLong();
    row.lastError = q.value(8).toString();
    row.authUser = q.value(9).toString();
    row.episodeCount = q.value(10).toInt();
    row.unplayedCount = q.value(11).toInt();   // NULL (no episodes) -> 0
    row.newCount = q.value(12).toInt();
    row.archived = q.value(13).toInt() != 0;
    row.archiveFolder = q.value(14).toString();
    row.downloadedCount = q.value(15).toInt();
    row.listenedSeconds = q.value(16).toLongLong();
    row.manualUpdate = q.value(17).toInt() != 0;
    row.folderId = q.value(18).toLongLong();
    row.freshEpisodes = q.value(19).toInt() != 0;
    return row;
}

QList<PodcastRow> Database::podcasts() const
{
    QList<PodcastRow> result;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    if (!q.exec(QString::fromLatin1(PodcastColumns) + QStringLiteral(" GROUP BY p.id"))) {
        fail(QStringLiteral("podcasts"), q.lastError().text());
        return result;
    }
    while (q.next()) {
        result.append(podcastFromQuery(q));
    }
    // Sorted here instead of in SQL: QCollator sorts umlauts correctly
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    std::sort(result.begin(), result.end(), [&collator](const PodcastRow &a, const PodcastRow &b) {
        return collator.compare(a.title, b.title) < 0;
    });
    return result;
}

PodcastRow Database::podcast(qint64 id) const
{
    QSqlQuery q(handle());
    q.prepare(QString::fromLatin1(PodcastColumns) + QStringLiteral(" WHERE p.id = ? GROUP BY p.id"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return podcastFromQuery(q);
    }
    return PodcastRow();
}

QList<qint64> Database::podcastIdsForUpdate() const
{
    QList<qint64> ids;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    // Also not the podcasts of a folder set to "manual update" (45)
    if (q.exec(QStringLiteral("SELECT id FROM podcasts WHERE paused = 0 AND archived = 0"
                              " AND (folder_id IS NULL OR folder_id NOT IN"
                              " (SELECT id FROM folders WHERE manual_update = 1)) ORDER BY id"))) {
        while (q.next()) {
            ids.append(q.value(0).toLongLong());
        }
    }
    return ids;
}

bool Database::changeUrl(qint64 id, const QString &newUrl)
{
    const QString trimmed = newUrl.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    const qint64 other = findPodcast(trimmed);
    if (other >= 0 && other != id) {
        // Another subscription already has this URL - keep both as they are
        return fail(QStringLiteral("changeUrl"),
                    QStringLiteral("%1 is already subscribed").arg(trimmed));
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET url = ?, etag = '', last_modified = '' WHERE id = ?"));
    q.addBindValue(sqlText(trimmed));
    q.addBindValue(id);
    if (!q.exec()) {
        return fail(QStringLiteral("changeUrl"), q.lastError().text());
    }
    return true;
}

bool Database::setAuthUser(qint64 id, const QString &user)
{
    QSqlQuery q(handle());
    // Clearing the validators forces a full download with the new login
    q.prepare(QStringLiteral("UPDATE podcasts SET auth_user = ?, etag = '', last_modified = '' WHERE id = ?"));
    q.addBindValue(sqlText(user.trimmed()));
    q.addBindValue(id);
    if (!q.exec()) {
        return fail(QStringLiteral("setAuthUser"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

// --- Episodes --------------------------------------------------------------

static const char *const EpisodeColumns =
        "SELECT e.id, e.podcast_id, COALESCE(NULLIF(p.custom_title, ''), NULLIF(p.title, ''), p.url),"
        " e.guid, e.url, e.title, e.published, e.duration, e.position, e.played, e.is_new,"
        " e.image_url, COALESCE(NULLIF(e.image_url, ''), p.cover_url), e.chapter_count, e.last_played,"
        " e.mime_type, e.link, e.chapters_url, e.file_size, e.position_updated, e.download_state,"
        " e.download_path, e.archive_file,"
        " (SELECT COUNT(*) FROM bookmarks b WHERE b.episode_id = e.id)"
        " FROM episodes e JOIN podcasts p ON p.id = e.podcast_id";

static EpisodeRow episodeFromQuery(const QSqlQuery &q)
{
    EpisodeRow row;
    row.id = q.value(0).toLongLong();
    row.podcastId = q.value(1).toLongLong();
    row.podcastTitle = q.value(2).toString();
    row.guid = q.value(3).toString();
    row.url = q.value(4).toString();
    row.title = q.value(5).toString();
    row.published = q.value(6).toLongLong();
    row.duration = q.value(7).toInt();
    row.position = q.value(8).toInt();
    row.played = q.value(9).toInt() != 0;
    row.isNew = q.value(10).toInt() != 0;
    row.imageUrl = q.value(11).toString();
    row.displayImageUrl = q.value(12).toString();
    row.chapterCount = q.value(13).toInt();
    row.lastPlayed = q.value(14).toLongLong();
    row.mimeType = q.value(15).toString();
    row.link = q.value(16).toString();
    row.chaptersUrl = q.value(17).toString();
    row.fileSize = q.value(18).toLongLong();
    row.positionUpdated = q.value(19).toLongLong();
    row.downloaded = q.value(20).toInt() == 2;
    row.downloadPath = q.value(21).toString();
    row.archiveFile = q.value(22).toString();
    row.bookmarkCount = q.value(23).toInt();
    return row;
}

QList<EpisodeRow> Database::episodes(qint64 podcastId, int limit, EpisodeFilter filter,
                                     bool oldestFirst, const QString &search) const
{
    QList<EpisodeRow> result;
    QString sql = QString::fromLatin1(EpisodeColumns);
    QStringList where;
    if (podcastId >= 0) {
        where << QStringLiteral("e.podcast_id = ?");
    }
    if (filter == Unplayed) {
        where << QStringLiteral("e.played = 0");
    } else if (filter == InProgress) {
        where << QStringLiteral("e.played = 0 AND e.position > 0");
    } else if (filter == Downloaded) {
        where << QStringLiteral("e.download_state = 2");
    } else if (filter == NewEpisodes) {
        where << QStringLiteral("e.played = 0 AND e.is_new = 1");
    } else if (filter == Queue) {
        sql += QStringLiteral(" JOIN queue q ON q.episode_id = e.id");
    }
    // Search in the titles (53) - case-insensitive for ASCII (SQLite LIKE)
    const QString needle = search.trimmed();
    if (!needle.isEmpty()) {
        where << QStringLiteral("e.title LIKE ? ESCAPE '\\'");
    }
    if (!where.isEmpty()) {
        sql += QStringLiteral(" WHERE ") + where.join(QStringLiteral(" AND "));
    }
    // Episodes in progress: the most recently played first
    // The playlist in its own order
    if (filter == InProgress) {
        sql += QStringLiteral(" ORDER BY e.position_updated DESC, e.id DESC LIMIT ?");
    } else if (filter == Queue) {
        sql += QStringLiteral(" ORDER BY q.position, q.episode_id LIMIT ?");
    } else {
        sql += oldestFirst ? QStringLiteral(" ORDER BY e.published ASC, e.id ASC LIMIT ?")
                           : QStringLiteral(" ORDER BY e.published DESC, e.id DESC LIMIT ?");
    }

    QSqlQuery q(handle());
    q.setForwardOnly(true);
    q.prepare(sql);
    if (podcastId >= 0) {
        q.addBindValue(podcastId);
    }
    if (!needle.isEmpty()) {
        QString pattern = needle;
        pattern.replace(QLatin1Char('\\'), QStringLiteral("\\\\"));
        pattern.replace(QLatin1Char('%'), QStringLiteral("\\%"));
        pattern.replace(QLatin1Char('_'), QStringLiteral("\\_"));
        q.addBindValue(QLatin1Char('%') + pattern + QLatin1Char('%'));
    }
    q.addBindValue(limit < 0 ? -1 : limit);
    if (!q.exec()) {
        fail(QStringLiteral("episodes"), q.lastError().text());
        return result;
    }
    while (q.next()) {
        result.append(episodeFromQuery(q));
    }
    return result;
}

EpisodeRow Database::episode(qint64 id) const
{
    QSqlQuery q(handle());
    q.prepare(QString::fromLatin1(EpisodeColumns) + QStringLiteral(" WHERE e.id = ?"));
    q.addBindValue(id);
    if (q.exec() && q.next()) {
        return episodeFromQuery(q);
    }
    return EpisodeRow();
}

QList<ParsedChapter> Database::chapters(qint64 episodeId) const
{
    QList<ParsedChapter> result;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT start_ms, title, href, image FROM chapters"
                             " WHERE episode_id = ? ORDER BY start_ms"));
    q.addBindValue(episodeId);
    if (q.exec()) {
        while (q.next()) {
            ParsedChapter c;
            c.startMs = q.value(0).toLongLong();
            c.title = q.value(1).toString();
            c.href = q.value(2).toString();
            c.image = q.value(3).toString();
            result.append(c);
        }
    }
    return result;
}

DatabaseStats Database::stats() const
{
    DatabaseStats s;
    QSqlQuery q(handle());
    if (q.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM podcasts), COUNT(*),"
                              " SUM(played), SUM(CASE WHEN played = 0 THEN 1 ELSE 0 END),"
                              " SUM(CASE WHEN played = 0 AND position > 0 THEN 1 ELSE 0 END)"
                              " FROM episodes")) && q.next()) {
        s.podcasts = q.value(0).toInt();
        s.episodes = q.value(1).toInt();
        s.played = q.value(2).toInt();
        s.unplayed = q.value(3).toInt();
        s.inProgress = q.value(4).toInt();
    }
    return s;
}

// --- Feed updates ----------------------------------------------------------

Database::FeedState Database::feedState(qint64 podcastId) const
{
    FeedState state;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT url, etag, last_modified, last_update, auth_user FROM podcasts WHERE id = ?"));
    q.addBindValue(podcastId);
    if (q.exec() && q.next()) {
        state.url = q.value(0).toString();
        state.etag = q.value(1).toString();
        state.lastModified = q.value(2).toString();
        state.neverUpdated = q.value(3).toLongLong() == 0;
        state.authUser = q.value(4).toString();
    }
    return state;
}

void Database::markNotModified(qint64 podcastId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET last_update = ?, last_error = NULL WHERE id = ?"));
    q.addBindValue(now());
    q.addBindValue(podcastId);
    q.exec();
}

void Database::setUpdateError(qint64 podcastId, const QString &error)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET last_error = ? WHERE id = ?"));
    // Shown in the app: no tokens from the feed address (Qt's messages
    // often contain the whole URL)
    q.addBindValue(UrlMask::maskText(error));
    q.addBindValue(podcastId);
    q.exec();
}

void Database::clearConnectionErrors()
{
    exec(QStringLiteral("UPDATE podcasts SET last_error = '' WHERE last_error LIKE 'Network session error%'"
                        " OR last_error LIKE 'Temporary network failure%'"
                        " OR last_error LIKE 'Unknown network error%'"));
}

int Database::applyFeed(qint64 podcastId, const ParsedFeed &feed,
                        const QString &etag, const QString &lastModified)
{
    QSqlDatabase db = handle();
    const FeedState state = feedState(podcastId);
    const qint64 timestamp = now();

    // On the very first update only the newest episode counts as "new" -
    // otherwise subscribing would flood the new-episodes list with the
    // whole back catalog. (Unplayed are they all, of course.)
    qint64 newestPublished = 0;
    if (state.neverUpdated) {
        for (int i = 0; i < feed.episodes.size(); ++i) {
            newestPublished = qMax(newestPublished, feed.episodes.at(i).published);
        }
    }

    if (!db.transaction()) {
        fail(QStringLiteral("applyFeed"), db.lastError().text());
        return -1;
    }

    QSqlQuery meta(db);
    meta.prepare(QStringLiteral(
            "UPDATE podcasts SET title = COALESCE(NULLIF(?, ''), title), link = ?, description = ?,"
            " author = ?, cover_url = COALESCE(NULLIF(?, ''), cover_url), etag = ?, last_modified = ?,"
            " last_update = ?, last_error = NULL WHERE id = ?"));
    meta.addBindValue(sqlText(feed.title));
    meta.addBindValue(sqlText(feed.link));
    meta.addBindValue(sqlText(feed.description));
    meta.addBindValue(sqlText(feed.author));
    meta.addBindValue(sqlText(feed.coverUrl));
    meta.addBindValue(sqlText(etag));
    meta.addBindValue(sqlText(lastModified));
    meta.addBindValue(timestamp);
    meta.addBindValue(podcastId);
    if (!meta.exec()) {
        fail(QStringLiteral("applyFeed podcast"), meta.lastError().text());
        db.rollback();
        return -1;
    }

    // guid -> (id, meta_hash) of the episodes already stored
    QHash<QString, QPair<qint64, QString> > existing;
    {
        QSqlQuery q(db);
        q.setForwardOnly(true);
        q.prepare(QStringLiteral("SELECT guid, id, meta_hash FROM episodes WHERE podcast_id = ?"));
        q.addBindValue(podcastId);
        if (q.exec()) {
            while (q.next()) {
                existing.insert(q.value(0).toString(),
                                qMakePair(q.value(1).toLongLong(), q.value(2).toString()));
            }
        }
    }

    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
            "INSERT INTO episodes (podcast_id, guid, url, title, description, description_html, link,"
            " mime_type, file_size, published, duration, image_url, chapters_url, chapter_count,"
            " meta_hash, is_new, added) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    QSqlQuery update(db);
    update.prepare(QStringLiteral(
            "UPDATE episodes SET url = ?, title = ?, description = ?, description_html = ?, link = ?,"
            " mime_type = ?, file_size = ?, published = ?,"
            " duration = CASE WHEN ? > 0 THEN ? ELSE duration END,"
            " image_url = ?, chapters_url = ?, chapter_count = CASE WHEN ? THEN chapter_count ELSE ? END,"
            " meta_hash = ? WHERE id = ?"));
    QSqlQuery deleteChapters(db);
    deleteChapters.prepare(QStringLiteral("DELETE FROM chapters WHERE episode_id = ?"));
    QSqlQuery insertChapter(db);
    insertChapter.prepare(QStringLiteral(
            "INSERT INTO chapters (episode_id, start_ms, title, href, image) VALUES (?, ?, ?, ?, ?)"));

    int newEpisodes = 0;
    QSet<QString> seen;
    for (int i = 0; i < feed.episodes.size(); ++i) {
        const ParsedEpisode &e = feed.episodes.at(i);
        if (seen.contains(e.guid)) {
            continue;   // duplicate guid inside one feed: first one wins
        }
        seen.insert(e.guid);
        const QString hash = metaHash(e);
        qint64 episodeId = -1;
        bool writeChapters = false;

        if (existing.contains(e.guid)) {
            const QPair<qint64, QString> stored = existing.value(e.guid);
            if (stored.second == hash) {
                continue;   // unchanged - the common case
            }
            episodeId = stored.first;
            update.addBindValue(sqlText(e.url));
            update.addBindValue(sqlText(e.title));
            update.addBindValue(sqlText(e.description));
            update.addBindValue(sqlText(e.descriptionHtml));
            update.addBindValue(sqlText(e.link));
            update.addBindValue(sqlText(e.mimeType));
            update.addBindValue(e.fileSize);
            update.addBindValue(e.published > 0 ? e.published : timestamp);
            update.addBindValue(e.duration);
            update.addBindValue(e.duration);
            update.addBindValue(sqlText(e.imageUrl));
            update.addBindValue(sqlText(e.chaptersUrl));
            // Chapters loaded from podcast:chapters (JSON) are kept as long
            // as the feed only names the JSON file and has no own chapters
            const bool keepChapters = e.chapters.isEmpty() && !e.chaptersUrl.isEmpty();
            update.addBindValue(keepChapters ? 1 : 0);
            update.addBindValue(e.chapters.size());
            update.addBindValue(sqlText(hash));
            update.addBindValue(episodeId);
            if (!update.exec()) {
                fail(QStringLiteral("applyFeed update"), update.lastError().text());
                db.rollback();
                return -1;
            }
            writeChapters = !(e.chapters.isEmpty() && !e.chaptersUrl.isEmpty());
        } else {
            const bool isNew = !state.neverUpdated || (e.published == newestPublished && e.published > 0)
                    || (newestPublished == 0 && i == 0);
            insert.addBindValue(podcastId);
            insert.addBindValue(sqlText(e.guid));
            insert.addBindValue(sqlText(e.url));
            insert.addBindValue(sqlText(e.title));
            insert.addBindValue(sqlText(e.description));
            insert.addBindValue(sqlText(e.descriptionHtml));
            insert.addBindValue(sqlText(e.link));
            insert.addBindValue(sqlText(e.mimeType));
            insert.addBindValue(e.fileSize);
            insert.addBindValue(e.published > 0 ? e.published : timestamp);
            insert.addBindValue(e.duration);
            insert.addBindValue(sqlText(e.imageUrl));
            insert.addBindValue(sqlText(e.chaptersUrl));
            insert.addBindValue(e.chapters.size());
            insert.addBindValue(sqlText(hash));
            insert.addBindValue(isNew ? 1 : 0);
            insert.addBindValue(timestamp);
            if (!insert.exec()) {
                fail(QStringLiteral("applyFeed insert"), insert.lastError().text());
                db.rollback();
                return -1;
            }
            episodeId = insert.lastInsertId().toLongLong();
            writeChapters = !e.chapters.isEmpty();
            ++newEpisodes;
        }

        if (writeChapters) {
            deleteChapters.addBindValue(episodeId);
            deleteChapters.exec();
            for (int c = 0; c < e.chapters.size(); ++c) {
                const ParsedChapter &chapter = e.chapters.at(c);
                insertChapter.addBindValue(episodeId);
                insertChapter.addBindValue(chapter.startMs);
                insertChapter.addBindValue(sqlText(chapter.title));
                insertChapter.addBindValue(sqlText(chapter.href));
                insertChapter.addBindValue(sqlText(chapter.image));
                insertChapter.exec();
            }
        }
    }

    if (!db.commit()) {
        fail(QStringLiteral("applyFeed commit"), db.lastError().text());
        db.rollback();
        return -1;
    }
    return newEpisodes;
}

// --- Local playback state ---------------------------------------------------

bool Database::recordPlayback(qint64 episodeId, int started, int position, int total)
{
    const qint64 timestamp = now();
    const bool finished = isFinishedPosition(position, total);
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
            "UPDATE episodes SET played = ?, position = ?, position_updated = ?, last_played = ?,"
            " is_new = 0, duration = CASE WHEN duration > 0 THEN duration ELSE ? END WHERE id = ?"));
    q.addBindValue(finished ? 1 : 0);
    q.addBindValue(finished ? 0 : qMax(0, position));
    q.addBindValue(timestamp);
    q.addBindValue(timestamp);
    q.addBindValue(qMax(0, total));
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("recordPlayback"), q.lastError().text());
    }
    if (q.numRowsAffected() == 0) {
        return fail(QStringLiteral("recordPlayback"), QStringLiteral("no episode with id %1").arg(episodeId));
    }
    queueAction(episodeId, QStringLiteral("play"), qMax(0, started), qMax(0, position), qMax(0, total), timestamp);
    return true;
}

bool Database::markPlayed(qint64 episodeId)
{
    int duration = 0;
    {
        QSqlQuery q(handle());
        q.prepare(QStringLiteral("SELECT duration FROM episodes WHERE id = ?"));
        q.addBindValue(episodeId);
        if (!q.exec() || !q.next()) {
            return fail(QStringLiteral("markPlayed"), QStringLiteral("no episode with id %1").arg(episodeId));
        }
        duration = q.value(0).toInt();
    }
    // Played = position at the end; with unknown duration 1 of 1 second
    const int total = duration > 0 ? duration : 1;
    return recordPlayback(episodeId, 0, total, total);
}

bool Database::markUnplayed(qint64 episodeId)
{
    const qint64 timestamp = now();
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE episodes SET played = 0, position = 0, position_updated = ?"
                             " WHERE id = ?"));
    q.addBindValue(timestamp);
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("markUnplayed"), q.lastError().text());
    }
    if (q.numRowsAffected() == 0) {
        return fail(QStringLiteral("markUnplayed"), QStringLiteral("no episode with id %1").arg(episodeId));
    }
    queueAction(episodeId, QStringLiteral("new"), -1, -1, -1, timestamp);
    return true;
}

// --- Settings ---------------------------------------------------------------

QString Database::setting(const QString &key, const QString &defaultValue) const
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT value FROM settings WHERE key = ?"));
    q.addBindValue(sqlText(key));
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return defaultValue;
}

bool Database::setSetting(const QString &key, const QString &value)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO settings (key, value) VALUES (?, ?)"));
    q.addBindValue(sqlText(key));
    q.addBindValue(sqlText(value));
    if (!q.exec()) {
        return fail(QStringLiteral("setSetting"), q.lastError().text());
    }
    return true;
}

bool Database::removeSettings(const QString &prefix)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM settings WHERE substr(key, 1, length(?)) = ?"));
    q.addBindValue(sqlText(prefix));
    q.addBindValue(sqlText(prefix));
    if (!q.exec()) {
        return fail(QStringLiteral("removeSettings"), q.lastError().text());
    }
    return true;
}

bool Database::isSyncConfigured() const
{
    return !setting(QStringLiteral("sync.backend")).isEmpty()
            && !setting(QStringLiteral("sync.server")).isEmpty();
}

// --- Sync -------------------------------------------------------------------

void Database::queueSubscription(const QString &kind, const QString &url)
{
    if (!isSyncConfigured()) {
        return;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT OR REPLACE INTO sync_subscriptions (url, kind, created) VALUES (?, ?, ?)"));
    q.addBindValue(sqlText(url));
    q.addBindValue(sqlText(kind));
    q.addBindValue(now());
    if (!q.exec()) {
        fail(QStringLiteral("queueSubscription"), q.lastError().text());
    }
}

bool Database::queueAction(qint64 episodeId, const QString &action, int started, int position,
                           int total, qint64 timestamp)
{
    if (!isSyncConfigured()) {
        return true;
    }
    QSqlQuery find(handle());
    find.prepare(QStringLiteral("SELECT p.url, e.url, e.guid FROM episodes e"
                                " JOIN podcasts p ON p.id = e.podcast_id WHERE e.id = ?"));
    find.addBindValue(episodeId);
    if (!find.exec() || !find.next()) {
        return false;
    }
    const QString podcastUrl = find.value(0).toString();
    const QString episodeUrl = find.value(1).toString();
    const QString guid = find.value(2).toString();
    find.finish();

    // Only the latest action per episode matters (newest timestamp wins)
    QSqlQuery remove(handle());
    remove.prepare(QStringLiteral("DELETE FROM sync_outbox WHERE podcast_url = ? AND episode_url = ?"));
    remove.addBindValue(sqlText(podcastUrl));
    remove.addBindValue(sqlText(episodeUrl));
    remove.exec();

    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO sync_outbox (podcast_url, episode_url, guid, action, timestamp,"
                             " started, position, total) VALUES (?, ?, ?, ?, ?, ?, ?, ?)"));
    q.addBindValue(sqlText(podcastUrl));
    q.addBindValue(sqlText(episodeUrl));
    q.addBindValue(sqlText(guid));
    q.addBindValue(sqlText(action));
    q.addBindValue(timestamp);
    q.addBindValue(started);
    q.addBindValue(position);
    q.addBindValue(total);
    if (!q.exec()) {
        return fail(QStringLiteral("queueAction"), q.lastError().text());
    }
    return true;
}

QStringList Database::outboxSubscriptions(const QString &kind) const
{
    QStringList urls;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT url FROM sync_subscriptions WHERE kind = ? ORDER BY created"));
    q.addBindValue(sqlText(kind));
    if (q.exec()) {
        while (q.next()) {
            urls.append(q.value(0).toString());
        }
    }
    return urls;
}

void Database::removeOutboxSubscriptions(const QStringList &urls)
{
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM sync_subscriptions WHERE url = ?"));
    for (int i = 0; i < urls.size(); ++i) {
        q.addBindValue(sqlText(urls.at(i)));
        q.exec();
    }
    db.commit();
}

static EpisodeAction actionFromQuery(const QSqlQuery &q)
{
    // Columns: id, podcast_url, episode_url, guid, action, timestamp, started, position, total
    EpisodeAction a;
    a.id = q.value(0).toLongLong();
    a.podcastUrl = q.value(1).toString();
    a.episodeUrl = q.value(2).toString();
    a.guid = q.value(3).toString();
    a.action = q.value(4).toString();
    a.timestamp = q.value(5).toLongLong();
    a.started = q.value(6).toInt();
    a.position = q.value(7).toInt();
    a.total = q.value(8).toInt();
    return a;
}

QList<EpisodeAction> Database::outboxActions(int limit) const
{
    QList<EpisodeAction> result;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    q.prepare(QStringLiteral("SELECT id, podcast_url, episode_url, guid, action, timestamp, started,"
                             " position, total FROM sync_outbox ORDER BY id LIMIT ?"));
    q.addBindValue(limit);
    if (q.exec()) {
        while (q.next()) {
            result.append(actionFromQuery(q));
        }
    }
    return result;
}

void Database::removeOutboxActions(const QList<qint64> &ids)
{
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("DELETE FROM sync_outbox WHERE id = ?"));
    for (int i = 0; i < ids.size(); ++i) {
        q.addBindValue(ids.at(i));
        q.exec();
    }
    db.commit();
}

void Database::addPendingActions(const QList<EpisodeAction> &actions)
{
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("INSERT INTO sync_pending (podcast_url, episode_url, guid, action, timestamp,"
                             " started, position, total, received) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
    const qint64 timestamp = now();
    for (int i = 0; i < actions.size(); ++i) {
        const EpisodeAction &a = actions.at(i);
        q.addBindValue(sqlText(a.podcastUrl));
        q.addBindValue(sqlText(a.episodeUrl));
        q.addBindValue(sqlText(a.guid));
        q.addBindValue(sqlText(a.action));
        q.addBindValue(a.timestamp);
        q.addBindValue(a.started);
        q.addBindValue(a.position);
        q.addBindValue(a.total);
        q.addBindValue(timestamp);
        if (!q.exec()) {
            fail(QStringLiteral("addPendingActions"), q.lastError().text());
        }
    }
    db.commit();
}

qint64 Database::findEpisodeForAction(const EpisodeAction &action) const
{
    QSqlQuery q(handle());
    const qint64 podcastId = action.podcastUrl.isEmpty() ? -1 : findPodcast(action.podcastUrl);

    if (podcastId >= 0) {
        if (!action.guid.isEmpty()) {
            q.prepare(QStringLiteral("SELECT id FROM episodes WHERE podcast_id = ? AND guid = ? LIMIT 1"));
            q.addBindValue(podcastId);
            q.addBindValue(sqlText(action.guid));
            if (q.exec() && q.next()) {
                return q.value(0).toLongLong();
            }
        }
        if (!action.episodeUrl.isEmpty()) {
            q.prepare(QStringLiteral("SELECT id FROM episodes WHERE podcast_id = ? AND url = ? LIMIT 1"));
            q.addBindValue(podcastId);
            q.addBindValue(sqlText(action.episodeUrl));
            if (q.exec() && q.next()) {
                return q.value(0).toLongLong();
            }
            // Same file, different tracking parameters
            const QString base = withoutQuery(action.episodeUrl);
            q.prepare(QStringLiteral("SELECT id FROM episodes WHERE podcast_id = ?"
                                     " AND (url = ? OR substr(url, 1, ?) = ?) LIMIT 1"));
            q.addBindValue(podcastId);
            q.addBindValue(sqlText(base));
            q.addBindValue(base.size() + 1);
            q.addBindValue(sqlText(base + QLatin1Char('?')));
            if (q.exec() && q.next()) {
                return q.value(0).toLongLong();
            }
        }
    }

    // Podcast not found (e.g. a slightly different feed URL on another
    // device): accept a unique match by GUID or media URL across all podcasts
    if (!action.guid.isEmpty()) {
        q.prepare(QStringLiteral("SELECT id FROM episodes WHERE guid = ? LIMIT 2"));
        q.addBindValue(sqlText(action.guid));
        if (q.exec() && q.next()) {
            const qint64 id = q.value(0).toLongLong();
            if (!q.next()) {
                return id;
            }
        }
    }
    if (!action.episodeUrl.isEmpty()) {
        q.prepare(QStringLiteral("SELECT id FROM episodes WHERE url = ? LIMIT 2"));
        q.addBindValue(sqlText(action.episodeUrl));
        if (q.exec() && q.next()) {
            const qint64 id = q.value(0).toLongLong();
            if (!q.next()) {
                return id;
            }
        }
    }
    return -1;
}

ApplyResult Database::applyPendingActions()
{
    ApplyResult result;
    QList<EpisodeAction> actions;
    {
        QSqlQuery q(handle());
        q.setForwardOnly(true);
        // Oldest first, so the newest action of an episode is applied last
        if (q.exec(QStringLiteral("SELECT id, podcast_url, episode_url, guid, action, timestamp, started,"
                                  " position, total FROM sync_pending ORDER BY timestamp, id"))) {
            while (q.next()) {
                actions.append(actionFromQuery(q));
            }
        }
    }
    if (actions.isEmpty()) {
        return result;
    }

    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery state(db);
    state.prepare(QStringLiteral("SELECT position_updated, duration FROM episodes WHERE id = ?"));
    QSqlQuery play(db);
    play.prepare(QStringLiteral(
            "UPDATE episodes SET played = ?, position = ?, position_updated = ?, is_new = 0,"
            " last_played = MAX(last_played, ?),"
            " duration = CASE WHEN duration > 0 THEN duration ELSE ? END WHERE id = ?"));
    QSqlQuery markNew(db);
    markNew.prepare(QStringLiteral("UPDATE episodes SET played = 0, position = 0, is_new = 1,"
                                   " position_updated = ? WHERE id = ?"));
    QSqlQuery done(db);
    done.prepare(QStringLiteral("DELETE FROM sync_pending WHERE id = ?"));

    for (int i = 0; i < actions.size(); ++i) {
        const EpisodeAction &a = actions.at(i);
        const qint64 episodeId = findEpisodeForAction(a);
        if (episodeId < 0) {
            ++result.pending;
            continue;
        }
        state.addBindValue(episodeId);
        qint64 updated = 0;
        int duration = 0;
        if (state.exec() && state.next()) {
            updated = state.value(0).toLongLong();
            duration = state.value(1).toInt();
        }
        state.finish();

        const QString action = a.action.toLower();
        if (a.timestamp <= updated) {
            ++result.older;
        } else if (action == QLatin1String("play") && a.position >= 0) {
            const int total = a.total > 0 ? a.total : duration;
            const bool finished = isFinishedPosition(a.position, total);
            play.addBindValue(finished ? 1 : 0);
            play.addBindValue(finished ? 0 : a.position);
            play.addBindValue(a.timestamp);
            play.addBindValue(a.timestamp);
            play.addBindValue(qMax(0, a.total));
            play.addBindValue(episodeId);
            play.exec();
            ++result.applied;
        } else if (action == QLatin1String("new")) {
            markNew.addBindValue(a.timestamp);
            markNew.addBindValue(episodeId);
            markNew.exec();
            ++result.applied;
        } else {
            ++result.older;     // download / delete: nothing to change here
        }
        done.addBindValue(a.id);
        done.exec();
    }
    if (!db.commit()) {
        fail(QStringLiteral("applyPendingActions"), db.lastError().text());
        db.rollback();
    }
    return result;
}

void Database::resetSyncState()
{
    exec(QStringLiteral("DELETE FROM sync_subscriptions"));
    exec(QStringLiteral("DELETE FROM sync_outbox"));
    exec(QStringLiteral("DELETE FROM sync_pending"));
    exec(QStringLiteral("DELETE FROM settings WHERE key IN"
                        " ('sync.subs_since', 'sync.actions_since', 'sync.last_sync')"));
}

SyncCounts Database::syncCounts() const
{
    SyncCounts c;
    QSqlQuery q(handle());
    if (q.exec(QStringLiteral("SELECT (SELECT COUNT(*) FROM sync_subscriptions),"
                              " (SELECT COUNT(*) FROM sync_outbox), (SELECT COUNT(*) FROM sync_pending)"))
            && q.next()) {
        c.outboxSubscriptions = q.value(0).toInt();
        c.outboxActions = q.value(1).toInt();
        c.pendingActions = q.value(2).toInt();
    }
    return c;
}

// --- Downloads and archive ---------------------------------------------------

bool Database::setArchived(qint64 podcastId, bool archived)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET archived = ? WHERE id = ?"));
    q.addBindValue(archived ? 1 : 0);
    q.addBindValue(podcastId);
    if (!q.exec()) {
        return fail(QStringLiteral("setArchived"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

int Database::prunePendingActions(int maxAgeDays)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM sync_pending WHERE received > 0 AND received < ?"));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch() / 1000 - qint64(maxAgeDays) * 24 * 3600);
    if (!q.exec()) {
        fail(QStringLiteral("prunePendingActions"), q.lastError().text());
        return 0;
    }
    return q.numRowsAffected();
}

// --- Bookmarks (47) -----------------------------------------------------------

static BookmarkRow bookmarkFromQuery(const QSqlQuery &q)
{
    BookmarkRow row;
    row.id = q.value(0).toLongLong();
    row.episodeId = q.value(1).toLongLong();
    row.position = q.value(2).toInt();
    row.note = q.value(3).toString();
    row.created = q.value(4).toLongLong();
    row.episodeTitle = q.value(5).toString();
    row.podcastTitle = q.value(6).toString();
    row.imageUrl = q.value(7).toString();
    row.title = q.value(8).toString();
    return row;
}

static const char *const BookmarkColumns =
        "SELECT b.id, b.episode_id, b.position, b.note, b.created, e.title,"
        " COALESCE(NULLIF(p.custom_title, ''), NULLIF(p.title, ''), p.url),"
        " COALESCE(NULLIF(e.image_url, ''), p.cover_url), b.title"
        " FROM bookmarks b JOIN episodes e ON e.id = b.episode_id JOIN podcasts p ON p.id = e.podcast_id";

QList<BookmarkRow> Database::bookmarks(qint64 episodeId) const
{
    QList<BookmarkRow> result;
    QSqlQuery q(handle());
    if (episodeId > 0) {
        q.prepare(QString::fromLatin1(BookmarkColumns) + QStringLiteral(" WHERE b.episode_id = ? ORDER BY b.position, b.id"));
        q.addBindValue(episodeId);
    } else {
        q.prepare(QString::fromLatin1(BookmarkColumns) + QStringLiteral(" ORDER BY b.created DESC, b.id DESC"));
    }
    if (!q.exec()) {
        fail(QStringLiteral("bookmarks"), q.lastError().text());
        return result;
    }
    while (q.next()) {
        result.append(bookmarkFromQuery(q));
    }
    return result;
}

int Database::bookmarkCount() const
{
    QSqlQuery q(handle());
    if (q.exec(QStringLiteral("SELECT COUNT(*) FROM bookmarks")) && q.next()) {
        return q.value(0).toInt();
    }
    return 0;
}

qint64 Database::addBookmark(qint64 episodeId, int position, qint64 created)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO bookmarks (episode_id, position, created) VALUES (?, ?, ?)"));
    q.addBindValue(episodeId);
    q.addBindValue(qMax(0, position));
    q.addBindValue(created > 0 ? created : QDateTime::currentMSecsSinceEpoch() / 1000);
    if (!q.exec()) {
        fail(QStringLiteral("addBookmark"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::setBookmarkNote(qint64 bookmarkId, const QString &note)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE bookmarks SET note = ? WHERE id = ?"));
    q.addBindValue(sqlText(note.trimmed()));
    q.addBindValue(bookmarkId);
    if (!q.exec()) {
        return fail(QStringLiteral("setBookmarkNote"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

bool Database::setBookmarkTitle(qint64 bookmarkId, const QString &title)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE bookmarks SET title = ? WHERE id = ?"));
    q.addBindValue(sqlText(title.trimmed()));
    q.addBindValue(bookmarkId);
    if (!q.exec()) {
        return fail(QStringLiteral("setBookmarkTitle"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

qint64 Database::removeBookmark(qint64 bookmarkId)
{
    qint64 episodeId = 0;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT episode_id FROM bookmarks WHERE id = ?"));
    q.addBindValue(bookmarkId);
    if (q.exec() && q.next()) {
        episodeId = q.value(0).toLongLong();
    }
    q.prepare(QStringLiteral("DELETE FROM bookmarks WHERE id = ?"));
    q.addBindValue(bookmarkId);
    if (!q.exec()) {
        fail(QStringLiteral("removeBookmark"), q.lastError().text());
        return 0;
    }
    return episodeId;
}

// --- Folders (45) -----------------------------------------------------------

QList<FolderRow> Database::folders() const
{
    QList<FolderRow> result;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    if (!q.exec(QStringLiteral(
            "SELECT f.id, f.name, f.sort_order, f.manual_update,"
            " (SELECT COUNT(*) FROM podcasts p WHERE p.folder_id = f.id),"
            " (SELECT COUNT(*) FROM episodes e JOIN podcasts p ON p.id = e.podcast_id"
            "  WHERE p.folder_id = f.id AND e.played = 0)"
            " FROM folders f ORDER BY f.sort_order, f.id"))) {
        fail(QStringLiteral("folders"), q.lastError().text());
        return result;
    }
    while (q.next()) {
        FolderRow row;
        row.id = q.value(0).toLongLong();
        row.name = q.value(1).toString();
        row.sortOrder = q.value(2).toInt();
        row.manualUpdate = q.value(3).toInt() != 0;
        row.podcastCount = q.value(4).toInt();
        row.unplayedCount = q.value(5).toInt();
        result.append(row);
    }
    return result;
}

FolderRow Database::unfiledFolder() const
{
    FolderRow row;
    QSqlQuery q(handle());
    if (q.exec(QStringLiteral(
            "SELECT (SELECT COUNT(*) FROM podcasts WHERE folder_id IS NULL),"
            " (SELECT COUNT(*) FROM episodes e JOIN podcasts p ON p.id = e.podcast_id"
            "  WHERE p.folder_id IS NULL AND e.played = 0)")) && q.next()) {
        row.podcastCount = q.value(0).toInt();
        row.unplayedCount = q.value(1).toInt();
    }
    return row;
}

qint64 Database::folderByName(const QString &name) const
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT id FROM folders WHERE lower(name) = lower(?)"));
    q.addBindValue(sqlText(name.trimmed()));
    if (q.exec() && q.next()) {
        return q.value(0).toLongLong();
    }
    return 0;
}

qint64 Database::createFolder(const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }
    const qint64 existing = folderByName(trimmed);
    if (existing > 0) {
        return existing;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("INSERT INTO folders (name, sort_order)"
                             " VALUES (?, (SELECT COALESCE(MAX(sort_order), 0) + 1 FROM folders))"));
    q.addBindValue(sqlText(trimmed));
    if (!q.exec()) {
        fail(QStringLiteral("createFolder"), q.lastError().text());
        return 0;
    }
    return q.lastInsertId().toLongLong();
}

bool Database::renameFolder(qint64 folderId, const QString &name)
{
    const QString trimmed = name.trimmed();
    if (trimmed.isEmpty()) {
        return false;
    }
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE folders SET name = ? WHERE id = ?"));
    q.addBindValue(sqlText(trimmed));
    q.addBindValue(folderId);
    if (!q.exec()) {
        return fail(QStringLiteral("renameFolder"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

bool Database::moveFolder(qint64 folderId, int step)
{
    QList<FolderRow> list = folders();
    int index = -1;
    for (int i = 0; i < list.size(); ++i) {
        if (list.at(i).id == folderId) {
            index = i;
        }
    }
    const int target = index + (step < 0 ? -1 : 1);
    if (index < 0 || target < 0 || target >= list.size()) {
        return false;
    }
#if QT_VERSION < QT_VERSION_CHECK(5, 13, 0)
    list.swap(index, target);
#else
    list.swapItemsAt(index, target);
#endif
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE folders SET sort_order = ? WHERE id = ?"));
    for (int i = 0; i < list.size(); ++i) {
        q.addBindValue(i + 1);
        q.addBindValue(list.at(i).id);
        if (!q.exec()) {
            db.rollback();
            return fail(QStringLiteral("moveFolder"), q.lastError().text());
        }
    }
    return db.commit();
}

bool Database::setFolderManualUpdate(qint64 folderId, bool manual)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE folders SET manual_update = ? WHERE id = ?"));
    q.addBindValue(manual ? 1 : 0);
    q.addBindValue(folderId);
    if (!q.exec()) {
        return fail(QStringLiteral("setFolderManualUpdate"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

bool Database::deleteFolder(qint64 folderId)
{
    QSqlQuery q(handle());
    // Explicitly, in case foreign keys are off for this connection
    q.prepare(QStringLiteral("UPDATE podcasts SET folder_id = NULL WHERE folder_id = ?"));
    q.addBindValue(folderId);
    q.exec();
    q.prepare(QStringLiteral("DELETE FROM folders WHERE id = ?"));
    q.addBindValue(folderId);
    if (!q.exec()) {
        return fail(QStringLiteral("deleteFolder"), q.lastError().text());
    }
    return true;
}

bool Database::setPodcastFolder(qint64 podcastId, qint64 folderId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET folder_id = ? WHERE id = ?"));
    q.addBindValue(folderId > 0 ? QVariant(folderId) : QVariant(QVariant::LongLong));
    q.addBindValue(podcastId);
    if (!q.exec()) {
        return fail(QStringLiteral("setPodcastFolder"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

QList<qint64> Database::podcastIdsInFolder(qint64 folderId) const
{
    QList<qint64> ids;
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT id FROM podcasts WHERE folder_id = ?"));
    q.addBindValue(folderId);
    if (q.exec()) {
        while (q.next()) {
            ids.append(q.value(0).toLongLong());
        }
    }
    return ids;
}

bool Database::setManualUpdate(qint64 podcastId, bool manual)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET paused = ? WHERE id = ?"));
    q.addBindValue(manual ? 1 : 0);
    q.addBindValue(podcastId);
    if (!q.exec()) {
        return fail(QStringLiteral("setManualUpdate"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

bool Database::setArchiveFolder(qint64 podcastId, const QString &folder)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE podcasts SET archive_folder = ? WHERE id = ?"));
    q.addBindValue(sqlText(folder));
    q.addBindValue(podcastId);
    if (!q.exec()) {
        return fail(QStringLiteral("setArchiveFolder"), q.lastError().text());
    }
    return true;
}

bool Database::isArchiveFolderUsed(const QString &folder, qint64 exceptPodcastId) const
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT COUNT(*) FROM podcasts WHERE archive_folder = ? COLLATE NOCASE AND id <> ?"));
    q.addBindValue(sqlText(folder));
    q.addBindValue(exceptPodcastId);
    return q.exec() && q.next() && q.value(0).toInt() > 0;
}

bool Database::setDownloaded(qint64 episodeId, const QString &path)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE episodes SET download_state = 2, download_path = ? WHERE id = ?"));
    q.addBindValue(sqlText(path));
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("setDownloaded"), q.lastError().text());
    }
    return true;
}

bool Database::clearDownload(qint64 episodeId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE episodes SET download_state = 0, download_path = '' WHERE id = ?"));
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("clearDownload"), q.lastError().text());
    }
    return true;
}

bool Database::setArchiveFile(qint64 episodeId, const QString &fileName)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("UPDATE episodes SET archive_file = ? WHERE id = ?"));
    q.addBindValue(sqlText(fileName));
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("setArchiveFile"), q.lastError().text());
    }
    return true;
}

QString Database::episodeDescription(qint64 episodeId, bool html) const
{
    QSqlQuery q(handle());
    q.prepare(html ? QStringLiteral("SELECT description_html FROM episodes WHERE id = ?")
                   : QStringLiteral("SELECT description FROM episodes WHERE id = ?"));
    q.addBindValue(episodeId);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return QString();
}

QString Database::podcastCoverUrl(qint64 podcastId) const
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT cover_url FROM podcasts WHERE id = ?"));
    q.addBindValue(podcastId);
    if (q.exec() && q.next()) {
        return q.value(0).toString();
    }
    return QString();
}

bool Database::setChapters(qint64 episodeId, const QList<ParsedChapter> &chapters)
{
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery remove(db);
    remove.prepare(QStringLiteral("DELETE FROM chapters WHERE episode_id = ?"));
    remove.addBindValue(episodeId);
    remove.exec();
    QSqlQuery insert(db);
    insert.prepare(QStringLiteral(
            "INSERT INTO chapters (episode_id, start_ms, title, href, image) VALUES (?, ?, ?, ?, ?)"));
    for (int i = 0; i < chapters.size(); ++i) {
        insert.addBindValue(episodeId);
        insert.addBindValue(chapters.at(i).startMs);
        insert.addBindValue(sqlText(chapters.at(i).title));
        insert.addBindValue(sqlText(chapters.at(i).href));
        insert.addBindValue(sqlText(chapters.at(i).image));
        insert.exec();
    }
    QSqlQuery count(db);
    count.prepare(QStringLiteral("UPDATE episodes SET chapter_count = ? WHERE id = ?"));
    count.addBindValue(chapters.size());
    count.addBindValue(episodeId);
    count.exec();
    if (!db.commit()) {
        db.rollback();
        return fail(QStringLiteral("setChapters"), db.lastError().text());
    }
    return true;
}

QList<qint64> Database::episodesMissingChapters(qint64 podcastId) const
{
    QList<qint64> ids;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    if (podcastId >= 0) {
        q.prepare(QStringLiteral("SELECT id FROM episodes WHERE chapters_url <> '' AND chapter_count = 0"
                                 " AND podcast_id = ? ORDER BY published DESC"));
        q.addBindValue(podcastId);
    } else {
        q.prepare(QStringLiteral("SELECT id FROM episodes WHERE chapters_url <> '' AND chapter_count = 0"
                                 " ORDER BY published DESC"));
    }
    if (q.exec()) {
        while (q.next()) {
            ids.append(q.value(0).toLongLong());
        }
    }
    return ids;
}

bool Database::mergeListeningState(qint64 episodeId, bool played, int position, qint64 timestamp,
                                   qint64 lastPlayed)
{
    // Same rule as for synced actions: only a newer state is taken over
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
            "UPDATE episodes SET played = ?, position = ?, position_updated = ?,"
            " last_played = MAX(last_played, ?), is_new = CASE WHEN ? THEN 0 ELSE is_new END"
            " WHERE id = ? AND position_updated < ?"));
    q.addBindValue(played ? 1 : 0);
    q.addBindValue(played ? 0 : qMax(0, position));
    q.addBindValue(timestamp);
    q.addBindValue(lastPlayed);
    q.addBindValue(played || position > 0 ? 1 : 0);
    q.addBindValue(episodeId);
    q.addBindValue(timestamp);
    if (!q.exec()) {
        return fail(QStringLiteral("mergeListeningState"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

qint64 Database::findEpisodeByGuid(qint64 podcastId, const QString &guid) const
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("SELECT id FROM episodes WHERE podcast_id = ? AND guid = ? LIMIT 1"));
    q.addBindValue(podcastId);
    q.addBindValue(sqlText(guid));
    if (q.exec() && q.next()) {
        return q.value(0).toLongLong();
    }
    return -1;
}

bool Database::setPodcastMetadata(qint64 podcastId, const QString &title, const QString &link,
                                  const QString &description, const QString &author,
                                  const QString &coverUrl)
{
    // Only fills fields that are still empty (e.g. after importing an archive)
    QSqlQuery q(handle());
    q.prepare(QStringLiteral(
            "UPDATE podcasts SET title = CASE WHEN title = '' THEN ? ELSE title END,"
            " link = CASE WHEN link = '' THEN ? ELSE link END,"
            " description = CASE WHEN description = '' THEN ? ELSE description END,"
            " author = CASE WHEN author = '' THEN ? ELSE author END,"
            " cover_url = CASE WHEN cover_url = '' THEN ? ELSE cover_url END WHERE id = ?"));
    q.addBindValue(sqlText(title));
    q.addBindValue(sqlText(link));
    q.addBindValue(sqlText(description));
    q.addBindValue(sqlText(author));
    q.addBindValue(sqlText(coverUrl));
    q.addBindValue(podcastId);
    if (!q.exec()) {
        return fail(QStringLiteral("setPodcastMetadata"), q.lastError().text());
    }
    return true;
}

// --- Playlist ----------------------------------------------------------------

bool Database::queueAdd(qint64 episodeId, bool atFront)
{
    QSqlQuery q(handle());
    // Already in the playlist: moved (to the front or to the end)
    q.prepare(atFront
              ? QStringLiteral("INSERT OR REPLACE INTO queue (episode_id, position)"
                               " VALUES (?, (SELECT COALESCE(MIN(position), 0) - 1 FROM queue))")
              : QStringLiteral("INSERT OR REPLACE INTO queue (episode_id, position)"
                               " VALUES (?, (SELECT COALESCE(MAX(position), 0) + 1 FROM queue))"));
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("queueAdd"), q.lastError().text());
    }
    return true;
}

bool Database::queueRemove(qint64 episodeId)
{
    QSqlQuery q(handle());
    q.prepare(QStringLiteral("DELETE FROM queue WHERE episode_id = ?"));
    q.addBindValue(episodeId);
    if (!q.exec()) {
        return fail(QStringLiteral("queueRemove"), q.lastError().text());
    }
    return q.numRowsAffected() > 0;
}

bool Database::queueMove(qint64 episodeId, int newIndex)
{
    QList<qint64> ids = queueIds();
    const int from = ids.indexOf(episodeId);
    if (from < 0) {
        return false;
    }
    ids.removeAt(from);
    ids.insert(qBound(0, newIndex, ids.size()), episodeId);
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE queue SET position = ? WHERE episode_id = ?"));
    for (int i = 0; i < ids.size(); ++i) {
        q.addBindValue(i);
        q.addBindValue(ids.at(i));
        q.exec();
    }
    return db.commit();
}

bool Database::queueSetOrder(const QList<qint64> &episodeIds)
{
    const QList<qint64> current = queueIds();
    QList<qint64> ids;
    for (int i = 0; i < episodeIds.size(); ++i) {
        if (current.contains(episodeIds.at(i)) && !ids.contains(episodeIds.at(i))) {
            ids.append(episodeIds.at(i));
        }
    }
    for (int i = 0; i < current.size(); ++i) {
        if (!ids.contains(current.at(i))) {
            ids.append(current.at(i));
        }
    }
    QSqlDatabase db = handle();
    db.transaction();
    QSqlQuery q(db);
    q.prepare(QStringLiteral("UPDATE queue SET position = ? WHERE episode_id = ?"));
    for (int i = 0; i < ids.size(); ++i) {
        q.addBindValue(i);
        q.addBindValue(ids.at(i));
        q.exec();
    }
    return db.commit();
}

void Database::queueClear()
{
    exec(QStringLiteral("DELETE FROM queue"));
}

QList<qint64> Database::queueIds() const
{
    QList<qint64> ids;
    QSqlQuery q(handle());
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT episode_id FROM queue ORDER BY position, episode_id"))) {
        while (q.next()) {
            ids.append(q.value(0).toLongLong());
        }
    }
    return ids;
}

int Database::markOlderPlayed(qint64 episodeId)
{
    // The older unplayed episodes of the same podcast (53), one by one like
    // markAllPlayed (sync)
    const EpisodeRow e = episode(episodeId);
    if (!e.id) {
        return 0;
    }
    QList<qint64> ids;
    {
        QSqlQuery q(handle());
        q.setForwardOnly(true);
        q.prepare(QStringLiteral("SELECT id FROM episodes WHERE podcast_id = ? AND played = 0"
                                 " AND (published < ? OR (published = ? AND id < ?))"));
        q.addBindValue(e.podcastId);
        q.addBindValue(e.published);
        q.addBindValue(e.published);
        q.addBindValue(e.id);
        if (q.exec()) {
            while (q.next()) {
                ids.append(q.value(0).toLongLong());
            }
        }
    }
    QSqlDatabase db = handle();
    db.transaction();
    int count = 0;
    for (int i = 0; i < ids.size(); ++i) {
        if (markPlayed(ids.at(i))) {
            ++count;
        }
    }
    db.commit();
    return count;
}

int Database::markAllPlayed(qint64 podcastId)
{
    // Marked like markPlayed() one by one, so the sync gets every change
    QList<qint64> ids;
    {
        QSqlQuery q(handle());
        q.setForwardOnly(true);
        q.prepare(QStringLiteral("SELECT id FROM episodes WHERE podcast_id = ? AND played = 0"));
        q.addBindValue(podcastId);
        if (q.exec()) {
            while (q.next()) {
                ids.append(q.value(0).toLongLong());
            }
        }
    }
    QSqlDatabase db = handle();
    db.transaction();
    int count = 0;
    for (int i = 0; i < ids.size(); ++i) {
        if (markPlayed(ids.at(i))) {
            ++count;
        }
    }
    db.commit();
    return count;
}

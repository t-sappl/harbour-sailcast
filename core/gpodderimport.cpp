// SPDX-License-Identifier: GPL-3.0-or-later
#include "gpodderimport.h"

#include <QFileInfo>
#include <QHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>
#include <QSqlDatabase>
#include <QSqlError>
#include <QSqlQuery>
#include <QStringList>
#include <QVariant>

#include "database.h"
#include "sqltext.h"

namespace {

const QString SourceConnection = QStringLiteral("sailcast-gpodder-import");

QSet<QString> columnsOf(QSqlDatabase &db, const QString &table)
{
    QSet<QString> columns;
    QSqlQuery q(db);
    if (q.exec(QStringLiteral("PRAGMA table_info(%1)").arg(table))) {
        while (q.next()) {
            columns.insert(q.value(1).toString());
        }
    }
    return columns;
}

// "name" if the column exists, otherwise a NULL placeholder: older
// gPodder versions lack some columns (minidb adds them on upgrade only)
QString col(const QSet<QString> &columns, const QString &name)
{
    return columns.contains(name) ? name : QStringLiteral("NULL");
}

bool isFinished(int position, int total)
{
    return position > 0 && total > 0 && (position + 10 >= total || position >= total * 0.99);
}

GpodderImportResult importFrom(QSqlDatabase &source, Database *database)
{
    GpodderImportResult result;
    QSqlDatabase target = database->handle();

    const QSet<QString> channelColumns = columnsOf(source, QStringLiteral("PodcastChannel"));
    const QSet<QString> episodeColumns = columnsOf(source, QStringLiteral("PodcastEpisode"));
    if (!channelColumns.contains(QStringLiteral("url")) || !episodeColumns.contains(QStringLiteral("guid"))) {
        result.error = QStringLiteral("Not a gPodder 4 database (tables PodcastChannel/PodcastEpisode missing)");
        return result;
    }

    if (!target.transaction()) {
        result.error = target.lastError().text();
        return result;
    }

    // --- Subscriptions ---
    QHash<qint64, qint64> podcastIds;   // gPodder id -> Sailcast id
    {
        QSqlQuery q(source);
        q.setForwardOnly(true);
        if (!q.exec(QStringLiteral("SELECT id, url, %1, %2, %3, %4, %5, %6, %7 FROM PodcastChannel")
                    .arg(col(channelColumns, QStringLiteral("title")),
                         col(channelColumns, QStringLiteral("link")),
                         col(channelColumns, QStringLiteral("description")),
                         col(channelColumns, QStringLiteral("cover_url")),
                         col(channelColumns, QStringLiteral("section")),
                         col(channelColumns, QStringLiteral("auth_username")),
                         col(channelColumns, QStringLiteral("pause_subscription"))))) {
            result.error = q.lastError().text();
            target.rollback();
            return result;
        }
        QSqlQuery insert(target);
        insert.prepare(QStringLiteral(
                "INSERT INTO podcasts (url, title, link, description, cover_url, section, auth_user,"
                " paused, added) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)"));
        while (q.next()) {
            const qint64 oldId = q.value(0).toLongLong();
            const QString url = q.value(1).toString().trimmed();
            if (url.isEmpty()) {
                continue;
            }
            qint64 newId = database->findPodcast(url);
            if (newId >= 0) {
                ++result.podcastsExisting;
            } else {
                insert.addBindValue(sqlText(url));
                insert.addBindValue(sqlText(q.value(2).toString()));
                insert.addBindValue(sqlText(q.value(3).toString()));
                insert.addBindValue(sqlText(q.value(4).toString()));
                insert.addBindValue(sqlText(q.value(5).toString()));
                insert.addBindValue(sqlText(q.value(6).toString()));
                insert.addBindValue(sqlText(q.value(7).toString()));
                insert.addBindValue(q.value(8).toInt() != 0 ? 1 : 0);
                insert.addBindValue(Database::now());
                if (!insert.exec()) {
                    result.error = insert.lastError().text();
                    target.rollback();
                    return result;
                }
                newId = insert.lastInsertId().toLongLong();
                ++result.podcastsAdded;
            }
            podcastIds.insert(oldId, newId);
        }
    }

    // --- Episodes ---
    QSqlQuery q(source);
    q.setForwardOnly(true);
    const QStringList wanted = QStringList()
            << QStringLiteral("podcast_id") << QStringLiteral("guid") << QStringLiteral("url")
            << QStringLiteral("title") << QStringLiteral("description")
            << QStringLiteral("description_html") << QStringLiteral("link")
            << QStringLiteral("mime_type") << QStringLiteral("file_size")
            << QStringLiteral("published") << QStringLiteral("total_time")
            << QStringLiteral("current_position") << QStringLiteral("current_position_updated")
            << QStringLiteral("last_playback") << QStringLiteral("is_new")
            << QStringLiteral("chapters") << QStringLiteral("episode_art_url");
    QStringList select;
    for (int i = 0; i < wanted.size(); ++i) {
        select << col(episodeColumns, wanted.at(i));
    }
    if (!q.exec(QStringLiteral("SELECT %1 FROM PodcastEpisode").arg(select.join(QStringLiteral(", "))))) {
        result.error = q.lastError().text();
        target.rollback();
        return result;
    }

    QSqlQuery find(target);
    find.prepare(QStringLiteral("SELECT id, played, position_updated FROM episodes"
                                " WHERE podcast_id = ? AND guid = ?"));
    QSqlQuery merge(target);
    merge.prepare(QStringLiteral(
            "UPDATE episodes SET played = MAX(played, ?),"
            " position = CASE WHEN ? > position_updated THEN ? ELSE position END,"
            " position_updated = MAX(position_updated, ?),"
            " last_played = MAX(last_played, ?) WHERE id = ?"));
    QSqlQuery insert(target);
    insert.prepare(QStringLiteral(
            "INSERT INTO episodes (podcast_id, guid, url, title, description, description_html, link,"
            " mime_type, file_size, published, duration, image_url, chapter_count, is_new, played,"
            " position, position_updated, last_played, added)"
            " VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?, 0, ?, ?, ?, ?, ?)"));
    QSqlQuery insertChapter(target);
    insertChapter.prepare(QStringLiteral(
            "INSERT INTO chapters (episode_id, start_ms, title, href, image) VALUES (?, ?, ?, ?, ?)"));

    while (q.next()) {
        const qint64 podcastId = podcastIds.value(q.value(0).toLongLong(), -1);
        if (podcastId < 0) {
            continue;   // episode of a podcast that no longer exists
        }
        const QString url = q.value(2).toString();
        QString guid = q.value(1).toString();
        if (guid.isEmpty()) {
            guid = url;
        }
        if (guid.isEmpty()) {
            continue;
        }
        const int total = q.value(10).toInt();
        const int position = q.value(11).toInt();
        const qint64 positionUpdated = q.value(12).toLongLong();
        const qint64 lastPlayback = q.value(13).toLongLong();
        // is_new is NULL only if the column is missing - then assume unplayed
        const bool isNew = q.value(14).isNull() || q.value(14).toInt() != 0;
        const bool played = !isNew || isFinished(position, total);
        const int storedPosition = played ? 0 : position;

        find.addBindValue(podcastId);
        find.addBindValue(sqlText(guid));
        if (find.exec() && find.next()) {
            const qint64 episodeId = find.value(0).toLongLong();
            find.finish();
            merge.addBindValue(played ? 1 : 0);
            merge.addBindValue(positionUpdated);
            merge.addBindValue(storedPosition);
            merge.addBindValue(positionUpdated);
            merge.addBindValue(lastPlayback);
            merge.addBindValue(episodeId);
            if (!merge.exec()) {
                result.error = merge.lastError().text();
                target.rollback();
                return result;
            }
            ++result.episodesMerged;
        } else {
            find.finish();
            QList<ParsedChapter> chapters;
            const QJsonArray array = QJsonDocument::fromJson(q.value(15).toString().toUtf8()).array();
            for (int i = 0; i < array.size(); ++i) {
                const QJsonObject object = array.at(i).toObject();
                ParsedChapter chapter;
                chapter.startMs = static_cast<qint64>(object.value(QStringLiteral("start")).toDouble() * 1000.0);
                chapter.title = object.value(QStringLiteral("title")).toString();
                chapter.href = object.value(QStringLiteral("href")).toString();
                chapter.image = object.value(QStringLiteral("image")).toString();
                if (!chapter.title.isEmpty()) {
                    chapters.append(chapter);
                }
            }

            insert.addBindValue(podcastId);
            insert.addBindValue(sqlText(guid));
            insert.addBindValue(sqlText(url));
            insert.addBindValue(sqlText(q.value(3).toString()));
            insert.addBindValue(sqlText(q.value(4).toString()));
            insert.addBindValue(sqlText(q.value(5).toString()));
            insert.addBindValue(sqlText(q.value(6).toString()));
            insert.addBindValue(sqlText(q.value(7).toString()));
            insert.addBindValue(q.value(8).toLongLong());
            insert.addBindValue(q.value(9).toLongLong());
            insert.addBindValue(total);
            insert.addBindValue(sqlText(q.value(16).toString()));
            insert.addBindValue(chapters.size());
            insert.addBindValue(played ? 1 : 0);
            insert.addBindValue(storedPosition);
            insert.addBindValue(positionUpdated);
            insert.addBindValue(lastPlayback);
            insert.addBindValue(Database::now());
            if (!insert.exec()) {
                result.error = insert.lastError().text();
                target.rollback();
                return result;
            }
            const qint64 episodeId = insert.lastInsertId().toLongLong();
            for (int i = 0; i < chapters.size(); ++i) {
                insertChapter.addBindValue(episodeId);
                insertChapter.addBindValue(chapters.at(i).startMs);
                insertChapter.addBindValue(sqlText(chapters.at(i).title));
                insertChapter.addBindValue(sqlText(chapters.at(i).href));
                insertChapter.addBindValue(sqlText(chapters.at(i).image));
                insertChapter.exec();
            }
            ++result.episodesAdded;
        }
        if (played) {
            ++result.played;
        } else if (storedPosition > 0) {
            ++result.inProgress;
        }
    }

    if (!target.commit()) {
        result.error = target.lastError().text();
        target.rollback();
        return result;
    }
    result.ok = true;
    return result;
}

} // namespace

namespace GpodderImport {

GpodderImportResult run(Database *database, const QString &path)
{
    GpodderImportResult result;
    if (!QFileInfo(path).isFile()) {
        result.error = QStringLiteral("File not found: %1").arg(path);
        return result;
    }
    if (!database || !database->isOpen()) {
        result.error = QStringLiteral("Sailcast database is not open");
        return result;
    }
    {
        QSqlDatabase source = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), SourceConnection);
        source.setDatabaseName(path);
        source.setConnectOptions(QStringLiteral("QSQLITE_OPEN_READONLY"));
        if (!source.open()) {
            result.error = source.lastError().text();
        } else {
            result = importFrom(source, database);
            source.close();
        }
    }
    QSqlDatabase::removeDatabase(SourceConnection);
    return result;
}

} // namespace GpodderImport

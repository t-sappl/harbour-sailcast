// SPDX-License-Identifier: GPL-3.0-or-later
#include "chaptersloader.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QUrl>

#include <algorithm>

#include "database.h"
#include "netutil.h"

namespace {
const int Parallel = 4;
const int TimeoutMs = 30000;
}

ChaptersLoader::ChaptersLoader(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_network(new QNetworkAccessManager(this))
{
    NetworkAccess::keepAccessible(m_network);
}

QList<ParsedChapter> ChaptersLoader::parse(const QByteArray &json, QString *error)
{
    QList<ParsedChapter> chapters;
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        if (error) {
            *error = QStringLiteral("Invalid JSON: %1").arg(parseError.errorString());
        }
        return chapters;
    }
    const QJsonArray array = document.object().value(QStringLiteral("chapters")).toArray();
    for (int i = 0; i < array.size(); ++i) {
        const QJsonObject object = array.at(i).toObject();
        if (object.value(QStringLiteral("toc")) == QJsonValue(false)) {
            continue;
        }
        const QJsonValue start = object.value(QStringLiteral("startTime"));
        if (!start.isDouble()) {
            continue;
        }
        ParsedChapter chapter;
        chapter.startMs = static_cast<qint64>(start.toDouble() * 1000.0 + 0.5);
        chapter.title = object.value(QStringLiteral("title")).toString().trimmed();
        chapter.image = object.value(QStringLiteral("img")).toString();
        chapter.href = object.value(QStringLiteral("url")).toString();
        if (chapter.title.isEmpty()) {
            chapter.title = QStringLiteral("Chapter %1").arg(chapters.size() + 1);
        }
        chapters.append(chapter);
    }
    std::sort(chapters.begin(), chapters.end(), [](const ParsedChapter &a, const ParsedChapter &b) {
        return a.startMs < b.startMs;
    });
    if (chapters.isEmpty() && error) {
        *error = QStringLiteral("No chapters in the file");
    }
    return chapters;
}

void ChaptersLoader::load(const QList<qint64> &episodeIds)
{
    m_queue.append(episodeIds);
    // From the event loop, so callers can connect first
    QTimer::singleShot(0, this, [this]() {
        while (m_running < Parallel && !m_queue.isEmpty()) {
            next();
        }
        if (m_running == 0 && m_queue.isEmpty()) {
            emit finished();
        }
    });
}

void ChaptersLoader::next()
{
    const qint64 episodeId = m_queue.takeFirst();
    const EpisodeRow episode = m_database->episode(episodeId);
    if (!episode.id || episode.chaptersUrl.isEmpty()) {
        emit failed(episodeId, QStringLiteral("No chapters URL"));
        return;
    }
    ++m_running;
    QNetworkRequest request{QUrl(episode.chaptersUrl)};
    request.setRawHeader("User-Agent", NetworkAccess::userAgent());
    request.setAttribute(QNetworkRequest::FollowRedirectsAttribute, true);
    QNetworkReply *reply = m_network->get(request);
    QTimer::singleShot(TimeoutMs, reply, SLOT(abort()));
    connect(reply, &QNetworkReply::finished, this, [this, reply, episodeId]() {
        reply->deleteLater();
        --m_running;
        if (reply->error() != QNetworkReply::NoError) {
            emit failed(episodeId, reply->errorString());
        } else {
            QString error;
            const QList<ParsedChapter> chapters = parse(reply->readAll(), &error);
            if (chapters.isEmpty()) {
                emit failed(episodeId, error);
            } else if (m_database->setChapters(episodeId, chapters)) {
                emit loaded(episodeId, chapters.size());
            } else {
                emit failed(episodeId, m_database->lastError());
            }
        }
        while (m_running < Parallel && !m_queue.isEmpty()) {
            next();
        }
        if (m_running == 0 && m_queue.isEmpty()) {
            emit finished();
        }
    });
}

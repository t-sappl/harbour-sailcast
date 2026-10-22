// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef CHAPTERSLOADER_H
#define CHAPTERSLOADER_H

#include <QList>
#include <QObject>
#include <QString>

#include "feedtypes.h"

class Database;
class QNetworkAccessManager;

// Loads chapters in the JSON format of Podcasting 2.0 (<podcast:chapters
// url="..." type="application/json+chapters">) and stores them with the
// episode. Feeds with Podlove Simple Chapters have them in the feed itself;
// the JSON file is only loaded for episodes without chapters.
//
//   {"version": "1.2.0", "chapters": [{"startTime": 0, "title": "Intro",
//     "img": "https://...", "url": "https://...", "toc": true}, ...]}
//
// Chapters with "toc": false are hidden markers and skipped.
class ChaptersLoader : public QObject
{
    Q_OBJECT
public:
    explicit ChaptersLoader(Database *database, QObject *parent = nullptr);

    // Loads the chapters of the given episodes (4 at a time)
    void load(const QList<qint64> &episodeIds);
    bool isBusy() const { return m_running > 0 || !m_queue.isEmpty(); }

    // The parser on its own (for tests)
    static QList<ParsedChapter> parse(const QByteArray &json, QString *error = nullptr);

signals:
    void loaded(qint64 episodeId, int chapters);
    void failed(qint64 episodeId, const QString &error);
    void finished();

private:
    void next();

    Database *m_database;
    QNetworkAccessManager *m_network;
    QList<qint64> m_queue;
    int m_running = 0;
};

#endif // CHAPTERSLOADER_H

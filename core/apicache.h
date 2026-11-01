// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef APICACHE_H
#define APICACHE_H

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QUrl>

// Keeps the answers of directory APIs (fyyd, Podcast Index trending) for a
// while (126): Discover shows the same lists many times a day, they change
// at most daily on the server, and an old answer is still better than
// nothing while offline.
//
// One file per request URL (SHA-1 of the encoded URL, so request headers -
// the Podcast Index signature - are not part of the key and never stored)
// in the cache folder; the age is the file's modification time. Files older
// than a week are removed now and then.
class ApiCache
{
public:
    // <cache location>/discover (app: ~/.cache/harbour-sailcast/harbour-sailcast/discover)
    static QString defaultDir();

    explicit ApiCache(const QString &dir = defaultDir());

    QString dir() const { return m_dir; }

    // Entries younger than this count as fresh (default 24 h; 0 = never fresh,
    // every request goes to the network but answers are still stored)
    void setMaxAge(int seconds) { m_maxAge = seconds; }
    int maxAge() const { return m_maxAge; }

    // A fresh entry for the URL
    bool fresh(const QUrl &url, QByteArray *data) const;
    // Any entry, also an old one (when the network fails); stored = its age
    bool stale(const QUrl &url, QByteArray *data, QDateTime *stored = nullptr) const;
    void store(const QUrl &url, const QByteArray &data) const;
    // Removes entries older than the given number of days
    void prune(int days = 7) const;
    void clear() const;

private:
    QString path(const QUrl &url) const;
    bool read(const QUrl &url, QByteArray *data, QDateTime *stored) const;

    QString m_dir;
    int m_maxAge = 24 * 3600;
};

#endif // APICACHE_H

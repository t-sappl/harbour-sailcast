// SPDX-License-Identifier: GPL-3.0-or-later
#include "apicache.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

QString ApiCache::defaultDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + QStringLiteral("/discover");
}

ApiCache::ApiCache(const QString &dir)
    : m_dir(dir)
{
}

QString ApiCache::path(const QUrl &url) const
{
    const QByteArray hash = QCryptographicHash::hash(url.toEncoded(), QCryptographicHash::Sha1).toHex();
    return m_dir + QLatin1Char('/') + QString::fromLatin1(hash) + QStringLiteral(".json");
}

bool ApiCache::read(const QUrl &url, QByteArray *data, QDateTime *stored) const
{
    QFile file(path(url));
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    const QByteArray content = file.readAll();
    if (content.isEmpty()) {
        return false;
    }
    if (data) {
        *data = content;
    }
    if (stored) {
        *stored = QFileInfo(file).lastModified();
    }
    return true;
}

bool ApiCache::fresh(const QUrl &url, QByteArray *data) const
{
    if (m_maxAge <= 0) {
        return false;
    }
    QDateTime stored;
    QByteArray content;
    if (!read(url, &content, &stored)) {
        return false;
    }
    const qint64 age = stored.secsTo(QDateTime::currentDateTime());
    // A clock set back makes the age negative - not fresh then
    if (age < 0 || age >= m_maxAge) {
        return false;
    }
    if (data) {
        *data = content;
    }
    return true;
}

bool ApiCache::stale(const QUrl &url, QByteArray *data, QDateTime *stored) const
{
    return read(url, data, stored);
}

void ApiCache::store(const QUrl &url, const QByteArray &data) const
{
    static bool pruned = false;
    if (!pruned) {
        pruned = true;
        prune();
    }
    if (!QDir().mkpath(m_dir)) {
        qWarning("ApiCache: cannot create %s", qPrintable(m_dir));
        return;
    }
    QSaveFile file(path(url));
    if (!file.open(QIODevice::WriteOnly)) {
        return;
    }
    file.write(data);
    file.commit();
}

void ApiCache::prune(int days) const
{
    QDir dir(m_dir);
    if (!dir.exists()) {
        return;
    }
    const QDateTime limit = QDateTime::currentDateTime().addDays(-days);
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (int i = 0; i < files.size(); ++i) {
        if (files.at(i).lastModified() < limit) {
            QFile::remove(files.at(i).absoluteFilePath());
        }
    }
}

void ApiCache::clear() const
{
    QDir dir(m_dir);
    if (!dir.exists()) {
        return;
    }
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("*.json"), QDir::Files);
    for (int i = 0; i < files.size(); ++i) {
        QFile::remove(files.at(i).absoluteFilePath());
    }
}

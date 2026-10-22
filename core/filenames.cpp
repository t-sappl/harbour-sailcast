// SPDX-License-Identifier: GPL-3.0-or-later
#include "filenames.h"

#include <QDateTime>
#include <QFileInfo>
#include <QRegularExpression>
#include <QUrl>

#include "database.h"

namespace FileNames {

QString safe(const QString &name, int maxLength)
{
    QString result;
    result.reserve(name.size());
    for (int i = 0; i < name.size(); ++i) {
        const QChar c = name.at(i);
        if (c.unicode() < 0x20 || c == QLatin1Char('/') || c == QLatin1Char('\\') || c == QLatin1Char(':')
                || c == QLatin1Char('*') || c == QLatin1Char('?') || c == QLatin1Char('"')
                || c == QLatin1Char('<') || c == QLatin1Char('>') || c == QLatin1Char('|')) {
            result.append(QLatin1Char('-'));
        } else {
            result.append(c);
        }
    }
    result = result.simplified();
    if (result.size() > maxLength) {
        result = result.left(maxLength).trimmed();
    }
    // Leading dots would hide the file, trailing dots/spaces are not
    // allowed on Windows file systems
    while (result.startsWith(QLatin1Char('.'))) {
        result.remove(0, 1);
    }
    while (result.endsWith(QLatin1Char('.')) || result.endsWith(QLatin1Char(' '))) {
        result.chop(1);
    }
    return result.isEmpty() ? QStringLiteral("unnamed") : result;
}

QString extension(const QString &url, const QString &mimeType)
{
    const QString suffix = QFileInfo(QUrl(url).path()).suffix().toLower();
    static const QRegularExpression knownSuffix(QStringLiteral("^(mp3|m4a|m4b|mp4|aac|ogg|oga|opus|flac|wav|mov|m4v|webm)$"));
    if (knownSuffix.match(suffix).hasMatch()) {
        return suffix;
    }
    const QString mime = mimeType.toLower();
    if (mime == QLatin1String("audio/mpeg") || mime == QLatin1String("audio/mp3")) return QStringLiteral("mp3");
    if (mime == QLatin1String("audio/mp4") || mime == QLatin1String("audio/x-m4a")
            || mime == QLatin1String("audio/m4a")) return QStringLiteral("m4a");
    if (mime == QLatin1String("audio/aac")) return QStringLiteral("aac");
    if (mime == QLatin1String("audio/ogg") || mime == QLatin1String("application/ogg")) return QStringLiteral("ogg");
    if (mime == QLatin1String("audio/opus")) return QStringLiteral("opus");
    if (mime == QLatin1String("audio/flac")) return QStringLiteral("flac");
    if (mime == QLatin1String("video/mp4")) return QStringLiteral("mp4");
    if (mime == QLatin1String("video/quicktime")) return QStringLiteral("mov");
    if (mime == QLatin1String("video/webm")) return QStringLiteral("webm");
    return QStringLiteral("mp3");
}

QString forEpisode(const EpisodeRow &episode)
{
    QString date;
    if (episode.published > 0) {
        date = QDateTime::fromMSecsSinceEpoch(episode.published * 1000, Qt::UTC)
                .toString(QStringLiteral("yyyy-MM-dd")) + QLatin1Char(' ');
    }
    const QString title = episode.title.trimmed().isEmpty() ? QStringLiteral("Episode %1").arg(episode.id)
                                                            : episode.title;
    return date + safe(title, 150) + QLatin1Char('.') + extension(episode.url, episode.mimeType);
}

} // namespace FileNames

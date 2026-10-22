// SPDX-License-Identifier: GPL-3.0-or-later
#include "timeparse.h"

#include <QDate>
#include <QDateTime>
#include <QRegularExpression>
#include <QStringList>
#include <QTime>

namespace TimeParse {

qint64 durationMs(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return -1;
    }
    const QStringList parts = trimmed.split(QLatin1Char(':'));
    if (parts.size() > 3) {
        return -1;
    }
    double total = 0;
    for (int i = 0; i < parts.size(); ++i) {
        bool ok = false;
        // Only the last part may have a fraction
        const double value = (i == parts.size() - 1) ? parts.at(i).toDouble(&ok)
                                                     : parts.at(i).toInt(&ok);
        if (!ok || value < 0) {
            return -1;
        }
        total = total * 60 + value;
    }
    return static_cast<qint64>(total * 1000.0 + 0.5);
}

int durationSeconds(const QString &text)
{
    const qint64 ms = durationMs(text);
    return ms > 0 ? static_cast<int>(ms / 1000) : 0;
}

static int monthFromName(const QString &name)
{
    static const char *const months[] = { "jan", "feb", "mar", "apr", "may", "jun",
                                          "jul", "aug", "sep", "oct", "nov", "dec" };
    const QString key = name.left(3).toLower();
    for (int i = 0; i < 12; ++i) {
        if (key == QLatin1String(months[i])) {
            return i + 1;
        }
    }
    return 0;
}

// Offset of a zone in seconds east of UTC; unknown zones count as UTC
static int zoneOffset(const QString &zone)
{
    if (zone.isEmpty()) {
        return 0;
    }
    const QChar sign = zone.at(0);
    if ((sign == QLatin1Char('+') || sign == QLatin1Char('-')) && zone.size() >= 5) {
        QString digits = zone.mid(1);
        digits.remove(QLatin1Char(':'));
        bool okH = false, okM = false;
        const int hours = digits.left(2).toInt(&okH);
        const int minutes = digits.mid(2, 2).toInt(&okM);
        if (!okH || !okM) {
            return 0;
        }
        const int offset = hours * 3600 + minutes * 60;
        return sign == QLatin1Char('-') ? -offset : offset;
    }
    const QString name = zone.toUpper();
    if (name == QLatin1String("EST")) return -5 * 3600;
    if (name == QLatin1String("EDT")) return -4 * 3600;
    if (name == QLatin1String("CST")) return -6 * 3600;
    if (name == QLatin1String("CDT")) return -5 * 3600;
    if (name == QLatin1String("MST")) return -7 * 3600;
    if (name == QLatin1String("MDT")) return -6 * 3600;
    if (name == QLatin1String("PST")) return -8 * 3600;
    if (name == QLatin1String("PDT")) return -7 * 3600;
    if (name == QLatin1String("CET")) return 1 * 3600;
    if (name == QLatin1String("CEST")) return 2 * 3600;
    return 0;   // GMT, UT, UTC, Z and anything unknown
}

qint64 pubDate(const QString &text)
{
    const QString trimmed = text.trimmed();
    if (trimmed.isEmpty()) {
        return 0;
    }

    // RFC 822 with optional weekday and seconds
    static const QRegularExpression rfc822(QStringLiteral(
        "^(?:[A-Za-z]+,?\\s*)?(\\d{1,2})[\\s-]+([A-Za-z]+)\\.?[\\s-]+(\\d{2,4})\\s+"
        "(\\d{1,2}):(\\d{2})(?::(\\d{2}))?(?:\\.\\d+)?\\s*([A-Za-z]+|[+-]\\d{2}:?\\d{2})?"));
    const QRegularExpressionMatch m = rfc822.match(trimmed);
    if (m.hasMatch()) {
        const int day = m.captured(1).toInt();
        const int month = monthFromName(m.captured(2));
        int year = m.captured(3).toInt();
        if (year < 100) {
            year += (year < 70) ? 2000 : 1900;
        }
        const QDate date(year, month, day);
        const QTime time(m.captured(4).toInt(), m.captured(5).toInt(),
                         m.captured(6).isEmpty() ? 0 : m.captured(6).toInt());
        if (month > 0 && date.isValid() && time.isValid()) {
            const QDateTime utc(date, time, Qt::UTC);
            return utc.toMSecsSinceEpoch() / 1000 - zoneOffset(m.captured(7));
        }
    }

    // ISO 8601 (Atom, dc:date)
    QDateTime iso = QDateTime::fromString(trimmed, Qt::ISODate);
    if (!iso.isValid() && trimmed.size() > 19) {
        // Qt 5.6 does not accept fractional seconds together with a zone in
        // every variant - retry without the fraction
        QString noFraction = trimmed;
        noFraction.remove(QRegularExpression(QStringLiteral("\\.\\d+")));
        iso = QDateTime::fromString(noFraction, Qt::ISODate);
    }
    if (iso.isValid()) {
        return iso.toMSecsSinceEpoch() / 1000;
    }

    const QDateTime rfc2822 = QDateTime::fromString(trimmed, Qt::RFC2822Date);
    if (rfc2822.isValid()) {
        return rfc2822.toMSecsSinceEpoch() / 1000;
    }
    return 0;
}

} // namespace TimeParse

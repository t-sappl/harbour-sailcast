// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SQLTEXT_H
#define SQLTEXT_H

#include <QLatin1String>
#include <QString>
#include <QVariant>

// QtSql binds a null QString (QString(), e.g. a feed without author) as
// SQL NULL, not as ''. All text columns are NOT NULL DEFAULT '', so every
// string goes through this helper before addBindValue().
inline QVariant sqlText(const QString &text)
{
    return text.isNull() ? QVariant(QString(QLatin1String(""))) : QVariant(text);
}

#endif // SQLTEXT_H

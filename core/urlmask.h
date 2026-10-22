// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef URLMASK_H
#define URLMASK_H

#include <QRegularExpression>
#include <QString>

// Hides secrets in feed and media addresses shown to the user (error
// messages, podcast errors, logs): values of query parameters that usually
// carry a token (Patreon "?auth=…", "token", "key", "sig", …) and a user
// name/password in the address. Works on any text, so a whole error
// message can be passed in. The full address stays where it is needed
// (database, sync, OPML export, the "Add podcast" field).
namespace UrlMask {

inline QString maskText(const QString &text)
{
    static const QRegularExpression query(QStringLiteral(
        "([?&;](?:auth|token|access_token|key|apikey|api_key|secret|sig|signature|password|pass|pw|hash|session|sid)=)"
        "[^&#\\s\"'<>]+"), QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression userInfo(QStringLiteral("(://)[^/@\\s\"'<>]+@"));
    QString result = text;
    result.replace(query, QStringLiteral("\\1…"));
    result.replace(userInfo, QStringLiteral("\\1…@"));
    return result;
}

} // namespace UrlMask

#endif // URLMASK_H

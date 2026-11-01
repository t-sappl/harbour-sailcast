// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef TOPICWORDS_H
#define TOPICWORDS_H

#include <QString>
#include <QStringList>

// A small German <-> English word list of podcast topics (135): a keyword
// of "Meine Interessen" ("Klassik") is also searched in the other language
// ("classical") - on the phone, no translation service. Words that are the
// same in both languages (Jazz, Rap, Linux) are not in it.
namespace TopicWords {

// The word in the other language(s), empty if unknown; case is ignored
QStringList translate(const QString &word);

// Words of a text for matching, lower case, split at everything that is
// not a letter or digit ("Hip-Hop" = "hip", "hop")
QStringList tokens(const QString &text);

// Whether all words of phrase appear one after another in the words of text
bool containsPhrase(const QStringList &textTokens, const QString &phrase);

} // namespace TopicWords

#endif // TOPICWORDS_H

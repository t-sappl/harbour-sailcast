// SPDX-License-Identifier: GPL-3.0-or-later
#include "topicwords.h"

namespace {

struct Pair { const char *de; const char *en; };

// German | English, UTF-8; extend as needed
const Pair Words[] = {
    { "Klassik", "classical" },
    { "klassische Musik", "classical music" },
    { "Oper", "opera" },
    { "Orchester", "orchestra" },
    { "Komponisten", "composers" },
    { "Komponist", "composer" },
    { "Klavier", "piano" },
    { "Geige", "violin" },
    { "Chor", "choir" },
    { "Barock", "baroque" },
    { "Volksmusik", "folk music" },
    { "elektronische Musik", "electronic music" },
    { "Musik", "music" },
    { "Musikgeschichte", "music history" },
    { "Tanz", "dance" },
    { "Geschichte", "history" },
    { "Mittelalter", "middle ages" },
    { "Antike", "antiquity" },
    { "Römer", "romans" },
    { "Krieg", "war" },
    { "Weltkrieg", "world war" },
    { "Archäologie", "archaeology" },
    { "Mythologie", "mythology" },
    { "Militär", "military" },
    { "Motorrad", "motorcycle" },
    { "Motorräder", "motorcycles" },
    { "Motorradreise", "motorcycle travel" },
    { "Auto", "car" },
    { "Autos", "cars" },
    { "Reisen", "travel" },
    { "Reise", "travel" },
    { "Abenteuer", "adventure" },
    { "Wandern", "hiking" },
    { "Bergsteigen", "mountaineering" },
    { "Klettern", "climbing" },
    { "Berge", "mountains" },
    { "Alpen", "alps" },
    { "Skifahren", "skiing" },
    { "Radfahren", "cycling" },
    { "Fahrrad", "bicycle" },
    { "Laufen", "running" },
    { "Fußball", "football" },
    { "Sport", "sports" },
    { "Segeln", "sailing" },
    { "Wissenschaft", "science" },
    { "Raumfahrt", "spaceflight" },
    { "Weltraum", "space" },
    { "Astronomie", "astronomy" },
    { "Physik", "physics" },
    { "Chemie", "chemistry" },
    { "Biologie", "biology" },
    { "Mathematik", "mathematics" },
    { "Geologie", "geology" },
    { "Medizin", "medicine" },
    { "Gesundheit", "health" },
    { "Ernährung", "nutrition" },
    { "Psychologie", "psychology" },
    { "Philosophie", "philosophy" },
    { "Spiritualität", "spirituality" },
    { "Kirche", "church" },
    { "Politik", "politics" },
    { "Gesellschaft", "society" },
    { "Kultur", "culture" },
    { "Wirtschaft", "economy" },
    { "Finanzen", "finance" },
    { "Geld", "money" },
    { "Börse", "stock market" },
    { "Investieren", "investing" },
    { "Unternehmertum", "entrepreneurship" },
    { "Karriere", "career" },
    { "Arbeit", "work" },
    { "Technik", "technology" },
    { "Technologie", "technology" },
    { "Programmierung", "programming" },
    { "Programmieren", "programming" },
    { "Softwareentwicklung", "software development" },
    { "Sicherheit", "security" },
    { "IT-Sicherheit", "cybersecurity" },
    { "Datenschutz", "privacy" },
    { "Künstliche Intelligenz", "artificial intelligence" },
    { "KI", "AI" },
    { "Elektronik", "electronics" },
    { "Amateurfunk", "ham radio" },
    { "Kunst", "art" },
    { "Literatur", "literature" },
    { "Bücher", "books" },
    { "Buch", "book" },
    { "Lesen", "reading" },
    { "Filme", "movies" },
    { "Kino", "cinema" },
    { "Fernsehen", "television" },
    { "Serien", "tv series" },
    { "Theater", "theatre" },
    { "Humor", "humour" },
    { "Krimi", "crime" },
    { "Verbrechen", "crime" },
    { "wahre Verbrechen", "true crime" },
    { "Hörspiel", "audio drama" },
    { "Geschichten", "stories" },
    { "Märchen", "fairy tales" },
    { "Gespräche", "conversations" },
    { "Kochen", "cooking" },
    { "Essen", "food" },
    { "Wein", "wine" },
    { "Bier", "beer" },
    { "Garten", "gardening" },
    { "Natur", "nature" },
    { "Tiere", "animals" },
    { "Umwelt", "environment" },
    { "Klima", "climate" },
    { "Klimawandel", "climate change" },
    { "Energie", "energy" },
    { "Luftfahrt", "aviation" },
    { "Fliegen", "flying" },
    { "Flugzeuge", "aircraft" },
    { "Eisenbahn", "railway" },
    { "Züge", "trains" },
    { "Schifffahrt", "shipping" },
    { "Bildung", "education" },
    { "Sprachen", "languages" },
    { "Sprachen lernen", "language learning" },
    { "Erziehung", "parenting" },
    { "Familie", "family" },
    { "Kinder", "kids" },
    { "Beziehungen", "relationships" },
    { "Liebe", "love" },
    { "Spiele", "games" },
    { "Videospiele", "video games" },
    { "Brettspiele", "board games" },
    { "Rollenspiele", "role-playing games" },
    { "Fotografie", "photography" },
    { "Architektur", "architecture" },
    { "Mode", "fashion" },
    { "Nachrichten", "news" },
    { "Recht", "law" },
    { "Justiz", "justice" },
    { "Landwirtschaft", "agriculture" },
    { "Handwerk", "craft" },
    { "Heimwerken", "diy" },
    { "Dokumentation", "documentary" },
    { "Österreich", "austria" },
    { "Deutschland", "germany" },
    { "Europa", "europe" },
    { "Ozean", "ocean" },
    { "Meer", "sea" },
    { "Esoterik", "esotericism" }
};

QString normalized(const QString &word)
{
    return TopicWords::tokens(word).join(QLatin1Char(' '));
}

} // namespace

namespace TopicWords {

QStringList tokens(const QString &text)
{
    QStringList result;
    QString current;
    const QString lower = text.toLower();
    for (int i = 0; i < lower.size(); ++i) {
        const QChar c = lower.at(i);
        if (c.isLetterOrNumber()) {
            current.append(c);
        } else if (!current.isEmpty()) {
            result.append(current);
            current.clear();
        }
    }
    if (!current.isEmpty()) {
        result.append(current);
    }
    return result;
}

bool containsPhrase(const QStringList &textTokens, const QString &phrase)
{
    const QStringList words = tokens(phrase);
    if (words.isEmpty() || words.size() > textTokens.size()) {
        return false;
    }
    for (int i = 0; i + words.size() <= textTokens.size(); ++i) {
        bool all = true;
        for (int j = 0; j < words.size() && all; ++j) {
            all = textTokens.at(i + j) == words.at(j);
        }
        if (all) {
            return true;
        }
    }
    return false;
}

QStringList translate(const QString &word)
{
    QStringList result;
    const QString w = normalized(word);
    if (w.isEmpty()) {
        return result;
    }
    for (size_t i = 0; i < sizeof(Words) / sizeof(Words[0]); ++i) {
        const QString de = QString::fromUtf8(Words[i].de);
        const QString en = QString::fromUtf8(Words[i].en);
        QString other;
        if (normalized(de) == w) {
            other = en;
        } else if (normalized(en) == w) {
            other = de;
        }
        if (!other.isEmpty() && !result.contains(other, Qt::CaseInsensitive)) {
            result.append(other);
        }
    }
    return result;
}

} // namespace TopicWords

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef FORYOU_H
#define FORYOU_H

#include <QHash>
#include <QList>
#include <QObject>
#include <QPair>
#include <QSet>
#include <QString>
#include <QStringList>

#include "fyyd.h"
#include "podcastindex.h"

class Database;

// The listening profile behind "Für dich" (129), computed from the
// subscriptions on this phone - nothing of it leaves the phone
struct ForYouProfile
{
    struct Podcast
    {
        qint64 id = 0;
        QString title;
        QString author;
        QString url;
        double weight = 0;      // 0.2 (subscribed) + log2(1 + plays) + 1 if played in the last 30 days
        int plays = 0;          // episodes really played (Database::playStats)
        qint64 lastPlayed = 0;
        QStringList categories; // current Apple names, "Top" and "Top/Sub"
        QString language;       // primary subtag, may be empty
    };
    QList<Podcast> podcasts;    // strongest first; the same podcast subscribed twice counts once
    // Share per category ("History", "Leisure/Automotive") after the user's
    // interests (133), strongest first; categories set to "never" are not in it
    QList<QPair<QString, double> > categories;
    QHash<QString, double> listened;    // share from listening alone
    QHash<QString, int> interests;      // ForYou::Interest set by the user (not Normal)
    QStringList never;                  // categories never to suggest
    // Every language of at least 20 % of the subscriptions, or the user's
    // choice (135); none: the phone's language
    QStringList languages;
    bool phoneLanguage = false; // languages is the phone's language (no subscriptions with one)
    bool languagesChosen = false; // languages set by the user (135)
    // Per language: listening weight relative to the strongest language (135);
    // a chosen language without listening 0.5
    QHash<QString, double> languageShares;
    // Keywords (135): each the word as typed and its other forms ("Klassik", "classical")
    QList<QStringList> moreKeywords;
    QList<QStringList> neverKeywords;

    double share(const QString &category) const;
};

// One suggestion of "Für dich" with its points (shown by the CLI for tuning)
struct ForYouItem
{
    enum Reason { ReasonCategory, ReasonSimilar, ReasonCollections, ReasonTrending, ReasonKeyword };

    QString title;
    QString author;
    QString url;
    QString imageUrl;
    QString description;
    QString language;
    QStringList categories;     // as the source names them
    qint64 newest = 0;          // Unix time of the newest episode, 0 = unknown
    int episodeCount = 0;

    double score = 0;
    double categoryPoints = 0;
    double languagePoints = 0;
    double similarPoints = 0;
    double collectionPoints = 0;
    double trendPoints = 0;
    double keywordPoints = 0;   // 135

    bool trending = false;      // Podcast Index trending
    bool hot = false;           // fyyd "hot"
    QStringList similarTo;      // own podcasts fyyd finds it similar to
    QSet<int> collections;      // fyyd collections it shares with own podcasts
    QString collectionWith;     // an own podcast in one of those collections

    Reason reason = ReasonTrending;
    QString reasonCategory;     // current Apple name ("History")
    QString reasonCategoryDe;   // German name from fyyd ("Geschichte"), may be empty
    QString reasonPodcast;      // own podcast for ReasonSimilar / ReasonCollections
    int reasonCount = 0;        // number of collections for ReasonCollections
    QString reasonKeyword;      // the keyword as typed for ReasonKeyword (135)
};

// "Für dich" (129, DESIGN 8a): suggestions from a local profile.
//
// 1. Profile (profile()): weight per subscribed podcast, its categories
//    (old Apple names mapped to the current ones; the feed's first top
//    category gets 2/3 of the weight, 132), the languages of at least 20 %
//    of the subscriptions (none: the phone's language); then the user's
//    interests (133): more = twice the share, at least the strongest
//    (134); less = half; never = out.
// 2. Candidates, anonymous - only a category and a language are sent: fyyd
//    "hot" per language (200, fyyd ignores a category - matched here) and
//    Podcast Index trending of the 3 strongest categories (a sub-category
//    instead of its top category when it holds at least half of it); with
//    setUseSubscriptions(true) ("Empfehlungen aus meinen Abos", default on
//    since 133) also fyyd's similar podcasts of the 10 strongest podcasts
//    and the collections of the 3 strongest (their addresses are sent).
// 3. Filter: subscribed (address, or title + author), hidden ("Nicht
//    interessiert"), a category set to "never", newest episode older than
//    24 months, another language; the same podcast under two addresses once
//    (the newer feed).
// 4. Points: category 3 × its share relative to the strongest category
//    (+1 for a matching sub-category), language 4 × its listening share
//    relative to the strongest language (135), keyword of "Mehr davon" 3
//    (and searched as a source, 135; "Nie" keywords leave a podcast out),
//    similar 5 (+1 per further own podcast, max 7), 2 per shared
//    collection (max 6), trending 1, hot 2. Only categories with at least
//    1/3 of the strongest one count (132). "Für dich" is personal (133):
//    a podcast from the general lists (hot, trending) only with a specific
//    match - a strong sub-category (not Hobbies, 134), a top category without sub-categories
//    (History, Technology, True Crime, Government), one set to "more" or a
//    keyword of "Mehr davon" (135);
//    the general lists themselves are the sections "Im Trend" and "Gerade
//    aktiv". The best (15, the app 100 since 140), the categories taking
//    turns (130).
//
// Directory answers are cached for 24 h (ApiCache), so running it again is
// cheap; the result itself is not stored.
class ForYou : public QObject
{
    Q_OBJECT
public:
    // "Meine Interessen" (133): the user's level per category, setting
    // discover.interest.<category> = more / less / never (normal = none)
    enum Interest { Normal, More, Less, Never };

    explicit ForYou(Database *database, QObject *parent = nullptr);

    Fyyd *fyyd() { return m_fyyd; }
    PodcastIndex *podcastIndex() { return m_index; }

    // "de" etc.; used when no subscription has a language
    void setPhoneLanguage(const QString &language) { m_phoneLanguage = language.left(2).toLower(); }
    void setUseSubscriptions(bool on) { m_useSubscriptions = on; }
    void setMaxItems(int count) { m_maxItems = count; }

    ForYouProfile profile() const;
    void start();
    bool isRunning() const { return m_pending > 0; }

    // Sets the level of a category (current Apple name, "History" or
    // "Leisure/Automotive"); start() again to see the effect
    void setInterest(const QString &category, Interest level);
    QHash<QString, int> interests() const;

    // Languages chosen by the user ("de", "en"); empty = from the
    // subscriptions (135, setting discover.languages)
    void setLanguages(const QStringList &languages);
    QStringList chosenLanguages() const;

    // Keywords (135): More = "Mehr davon" (searched and rewarded), Never =
    // "Nie" (podcasts with it are left out). addKeyword() stores the word
    // with its forms in the other language - the given ones, or found in
    // TopicWords and fyyd's category names when none are given - and
    // returns all forms. Settings discover.keywords.more / .never.
    QStringList addKeyword(Interest kind, const QString &word, const QStringList &forms = QStringList());
    void removeKeyword(Interest kind, const QString &word);
    QList<QStringList> keywords(Interest kind) const;
    // The word in the other language: TopicWords, then fyyd's German and
    // English category names
    QStringList translate(const QString &word) const;

    // "Nicht interessiert": never suggested again (setting discover.hidden)
    void hide(const QString &feedUrl, const QString &title, const QString &author);

    // For the general lists of the Discover page too (137): spam (title in
    // another script than the language, spam domain in the title), and a
    // podcast with a keyword of "Nie" in title, description or categories
    static bool isSpam(const QString &title, const QString &language);
    bool isUnwanted(const QString &title, const QString &description, const QStringList &categories) const;
    // "Kein Interesse" given for this podcast (149, also for the "Neu" tab)
    bool isHidden(const QString &feedUrl, const QString &title, const QString &author) const;
    // (149) A collection worth counting: not a default name ("new collection
    // for …", "initial collection …") and no advertising text
    static bool usefulCollection(const QString &title, const QString &description);

    // Old Apple category names ("Science & Medicine", "Games & Hobbies/Automotive")
    // mapped to the current ones ("Science", "Leisure/Automotive")
    static QString currentCategory(const QString &category);
    // (193) Whether a category ("History", "Leisure/Automotive") is among
    // categories as Podcast Index names them (single words: "Society",
    // "Culture") or as Apple does - every word of it (and of its top
    // category) is there
    static bool categoryIn(const QString &category, const QStringList &categories);

signals:
    void finished(const ForYouProfile &profile, const QList<ForYouItem> &items);

private:
    struct Query
    {
        QString category;       // "History" or "Leisure/Automotive"
    };

    void request() { ++m_pending; }
    void done();
    void startFyydHot();
    void finish();
    void addCandidate(const ForYouItem &item);
    void addIndexResults(const QList<PodcastSearchResult> &results, bool trending);
    void addFyyd(const QList<FyydPodcast> &podcasts, bool hot, const QString &similarTo, int collection,
                 const QString &collectionWith);
    QString ownTitle(const QString &feedUrl) const;
    QString germanName(const QString &category) const;

    Database *m_database;
    Fyyd *m_fyyd;
    PodcastIndex *m_index;
    QString m_phoneLanguage;
    bool m_useSubscriptions = true;     // default on (decision of 28 Oct, 133)
    int m_maxItems = 15;

    int m_pending = 0;
    ForYouProfile m_profile;
    QList<Query> m_queries;
    QList<FyydCategory> m_fyydCategories;
    QHash<int, QString> m_collectionOwner;  // collection id -> own podcast in it
    QHash<QString, ForYouItem> m_candidates; // by title + author
};

#endif // FORYOU_H

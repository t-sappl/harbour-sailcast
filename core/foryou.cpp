// SPDX-License-Identifier: GPL-3.0-or-later
#include "foryou.h"

#include <algorithm>
#include <cmath>

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMap>
#include <QRegExp>

#include "database.h"
#include "topicwords.h"

namespace {

const char HiddenKey[] = "discover.hidden";
const char InterestPrefix[] = "discover.interest.";
const char LanguagesKey[] = "discover.languages";
const char MoreKeywordsKey[] = "discover.keywords.more";
const char NeverKeywordsKey[] = "discover.keywords.never";
const int MaxKeywordForms = 3;              // searched forms per keyword (135)
// Apple's top categories without sub-categories: specific on their own (133)
const char *const SpecificTops[] = { "History", "Technology", "True Crime", "Government" };
// Sub-categories too broad to be specific (134: a comic and a football
// podcast came through "Hobbies") - only with "more"
const char *const BroadSubs[] = { "Leisure/Hobbies" };
const qint64 Day = 24 * 3600;
const qint64 RecentSeconds = 30 * Day;      // "played in the last 30 days"
const qint64 InactiveSeconds = 730 * Day;   // newest episode older than 24 months
const double LanguageShare = 0.2;           // languages of at least 20 % of the subscriptions
const int MaxLanguages = 2;
const int MaxQueries = 3;
const int SimilarPodcasts = 10;          // 134: 5 found similar podcasts only for one of them
const int CollectionPodcasts = 3;
const int CollectionsPerPodcast = 5;
const double SubscriptionWeight = 0.2;      // the subscription alone (130: 1 let unheard subscriptions dominate)
const int HotCount = 200;                   // fyyd hot per language, filtered here (fyyd ignores a category)
const double MainShare = 2.0 / 3;           // the feed's first top category (132)
const double StrongShare = 1.0 / 3;         // categories with at least 1/3 of the strongest count (132)

qint64 now()
{
    return QDateTime::currentMSecsSinceEpoch() / 1000;
}

// Same podcast = same title and author (letters and digits, case ignored)
QString podcastKey(const QString &title, const QString &author)
{
    return Fyyd::normalized(title) + QLatin1Char('|') + Fyyd::normalized(author);
}

QString primaryLanguage(const QString &language)
{
    return language.trimmed().toLower().section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0);
}

// Words of a category name for matching between the sources: Podcast
// Index splits "Society & Culture" into "Society" and "Culture", "Natural
// Sciences" into "Natural" and "Science"; fyyd and the feeds keep Apple's
// names. A category matches when all its words are among the candidate's.
QStringList words(const QString &name)
{
    QStringList result;
#if QT_VERSION >= QT_VERSION_CHECK(5, 14, 0)
    const QStringList parts = name.toLower().split(QRegExp(QStringLiteral("[^\\w]+")), Qt::SkipEmptyParts);
#else
    const QStringList parts = name.toLower().split(QRegExp(QStringLiteral("[^\\w]+")), QString::SkipEmptyParts);
#endif
    for (int i = 0; i < parts.size(); ++i) {
        QString w = parts.at(i);
        if (w == QLatin1String("and")) {
            continue;
        }
        if (w.size() > 4 && w.endsWith(QLatin1Char('s'))) {
            w.chop(1);      // "sciences" = "science"
        }
        if (!result.contains(w)) {
            result.append(w);
        }
    }
    return result;
}

QSet<QString> wordSet(const QStringList &categories)
{
    QSet<QString> set;
    for (int i = 0; i < categories.size(); ++i) {
        const QStringList parts = categories.at(i).split(QLatin1Char('/'));
        for (int j = 0; j < parts.size(); ++j) {
            const QStringList w = words(parts.at(j));
            for (int k = 0; k < w.size(); ++k) {
                set.insert(w.at(k));
            }
        }
    }
    return set;
}

bool matches(const QString &categoryPart, const QSet<QString> &candidateWords)
{
    const QStringList w = words(categoryPart);
    if (w.isEmpty()) {
        return false;
    }
    for (int i = 0; i < w.size(); ++i) {
        if (!candidateWords.contains(w.at(i))) {
            return false;
        }
    }
    return true;
}

// Spam in the directories (136): Podcast Index trending brought a Chinese
// feed claiming German, with a domain in its title and stuffed categories
const char *const NonLatinLanguages[] = { "zh", "ja", "ko", "ru", "uk", "bg", "sr", "mk", "be", "el", "ar",
                                          "fa", "ur", "he", "yi", "hi", "bn", "ta", "te", "th", "ka", "hy" };
const int MinTrendingEpisodes = 5;

// A title mostly in another script than the language claims (a Latin
// script language with a Chinese title)
bool wrongScript(const QString &title, const QString &language)
{
    for (size_t i = 0; i < sizeof(NonLatinLanguages) / sizeof(NonLatinLanguages[0]); ++i) {
        if (language == QLatin1String(NonLatinLanguages[i])) {
            return false;
        }
    }
    int letters = 0;
    int latin = 0;
    for (int i = 0; i < title.size(); ++i) {
        const QChar c = title.at(i);
        if (!c.isLetter()) {
            continue;
        }
        ++letters;
        if (c.script() == QChar::Script_Latin) {
            ++latin;
        }
    }
    return letters >= 4 && latin * 2 < letters;
}

// A web address in the title ("1000fen.cc") - top-level domains of spam;
// not .fm, .de and the like, which real podcasts carry ("nerds.fm")
bool domainInTitle(const QString &title)
{
    static const QRegExp domain(QStringLiteral(
            "[A-Za-z0-9-]+\\.(cc|top|xyz|vip|cn|bet|win|icu|buzz|club|site|online|shop|work|loan|click|link)\\b"));
    return domain.indexIn(title) >= 0;
}

// Apple's categories before the change of 2019 -> today's (normalized keys)
struct Mapping { const char *from; const char *to; };
const Mapping SubMappings[] = {
    { "Arts/Literature", "Arts/Books" },
    { "Business/Business News", "News/Business News" },
    { "Business/Management & Marketing", "Business/Management" },
    { "Business/Shopping", "Business" },
    { "Education/Educational Technology", "Technology" },
    { "Education/Higher Education", "Education" },
    { "Education/K-12", "Education" },
    { "Education/Language Courses", "Education/Language Learning" },
    { "Education/Training", "Education/How To" },
    { "Games & Hobbies/Automotive", "Leisure/Automotive" },
    { "Games & Hobbies/Aviation", "Leisure/Aviation" },
    { "Games & Hobbies/Hobbies", "Leisure/Hobbies" },
    { "Games & Hobbies/Other Games", "Leisure/Games" },
    { "Games & Hobbies/Video Games", "Leisure/Video Games" },
    { "Government & Organizations/Non-Profit", "Business/Non-Profit" },
    { "Health/Alternative Health", "Health & Fitness/Alternative Health" },
    { "Health/Fitness & Nutrition", "Health & Fitness/Fitness" },
    { "Health/Self-Help", "Education/Self-Improvement" },
    { "Health/Sexuality", "Health & Fitness/Sexuality" },
    { "Science & Medicine/Medicine", "Health & Fitness/Medicine" },
    { "Science & Medicine/Natural Sciences", "Science/Natural Sciences" },
    { "Science & Medicine/Social Sciences", "Science/Social Sciences" },
    { "Society & Culture/History", "History" },
    { "Sports & Recreation/Outdoor", "Sports/Wilderness" },
    { "Technology/Tech News", "News/Tech News" },
    { "Technology/Gadgets", "Technology" },
    { "Technology/Podcasting", "Technology" },
    { "Technology/Software How-To", "Technology" },
};
// Renamed top categories; their old sub-categories without an entry above
// go to the new top category
const Mapping TopMappings[] = {
    { "Games & Hobbies", "Leisure" },
    { "Government & Organizations", "Government" },
    { "Health", "Health & Fitness" },
    { "News & Politics", "News" },
    { "Science & Medicine", "Science" },
    { "Sports & Recreation", "Sports" },
};

QString key(const QString &category)
{
    const QStringList parts = category.split(QLatin1Char('/'));
    QStringList normalized;
    for (int i = 0; i < parts.size(); ++i) {
        normalized.append(Fyyd::normalized(parts.at(i)));
    }
    return normalized.join(QLatin1Char('/'));
}

} // namespace

double ForYouProfile::share(const QString &category) const
{
    for (int i = 0; i < categories.size(); ++i) {
        if (categories.at(i).first == category) {
            return categories.at(i).second;
        }
    }
    return 0;
}

QString ForYou::currentCategory(const QString &category)
{
    const QString name = category.trimmed();
    const QString k = key(name);
    for (size_t i = 0; i < sizeof(SubMappings) / sizeof(SubMappings[0]); ++i) {
        if (key(QString::fromLatin1(SubMappings[i].from)) == k) {
            return QString::fromLatin1(SubMappings[i].to);
        }
    }
    const QString top = name.section(QLatin1Char('/'), 0, 0);
    const QString topKey = key(top);
    for (size_t i = 0; i < sizeof(TopMappings) / sizeof(TopMappings[0]); ++i) {
        if (key(QString::fromLatin1(TopMappings[i].from)) == topKey) {
            return QString::fromLatin1(TopMappings[i].to);
        }
    }
    return name;
}

ForYou::ForYou(Database *database, QObject *parent)
    : QObject(parent)
    , m_database(database)
    , m_fyyd(new Fyyd(this))
    , m_index(new PodcastIndex(this))
{
    connect(m_fyyd, &Fyyd::categoriesReady, this, [this](const QList<FyydCategory> &categories) {
        m_fyydCategories = categories;
        startFyydHot();
        done();
    });
    connect(m_fyyd, &Fyyd::hotReady, this, [this](const QString &, const QList<FyydPodcast> &list) {
        addFyyd(list, true, QString(), 0, QString());
        done();
    });
    connect(m_fyyd, &Fyyd::similarReady, this, [this](const QString &feedUrl, int, const QList<FyydPodcast> &list) {
        addFyyd(list, false, ownTitle(feedUrl), 0, QString());
        done();
    });
    connect(m_fyyd, &Fyyd::podcastCollectionsReady, this,
            [this](const QString &feedUrl, int, const QList<FyydCollection> &collections) {
        const QString owner = ownTitle(feedUrl);
        int taken = 0;
        for (int i = 0; i < collections.size() && taken < CollectionsPerPodcast; ++i) {
            // 149: default names and advertising do not count (CRE was in
            // "Dry Fruit Zone" and "new collection for …")
            if (!usefulCollection(collections.at(i).title, collections.at(i).description)) {
                continue;
            }
            ++taken;
            const int id = collections.at(i).id;
            if (m_collectionOwner.contains(id)) {
                continue;   // asked already for another own podcast
            }
            m_collectionOwner.insert(id, owner);
            request();
            m_fyyd->collection(id);
        }
        done();
    });
    connect(m_fyyd, &Fyyd::collectionReady, this,
            [this](const FyydCollection &collection, const QList<FyydPodcast> &list) {
        addFyyd(list, false, QString(), collection.id, m_collectionOwner.value(collection.id));
        done();
    });
    connect(m_fyyd, &Fyyd::failed, this, [this](const QString &request, const QString &) {
        if (request == QLatin1String("categories")) {
            startFyydHot();     // still: hot works without the tree (no German names then)
        }
        done();
    });
    connect(m_index, &PodcastIndex::trendingFinished, this,
            [this](const QString &, const QString &, const QList<PodcastSearchResult> &results) {
        addIndexResults(results, true);
        done();
    });
    connect(m_index, &PodcastIndex::searchCachedFinished, this,
            [this](const QString &, const QList<PodcastSearchResult> &results) {
        addIndexResults(results, false);
        done();
    });
    connect(m_index, &PodcastIndex::searchCachedFailed, this, [this](const QString &, const QString &) {
        done();
    });
    connect(m_fyyd, &Fyyd::searchReady, this, [this](const QString &, const QList<FyydPodcast> &list) {
        addFyyd(list, false, QString(), 0, QString());
        done();
    });
    connect(m_index, &PodcastIndex::trendingFailed, this, [this](const QString &, const QString &, const QString &) {
        done();
    });
}

void ForYou::addIndexResults(const QList<PodcastSearchResult> &results, bool trending)
{
    for (int i = 0; i < results.size(); ++i) {
        const PodcastSearchResult &r = results.at(i);
        ForYouItem item;
        item.title = r.title.trimmed();
        item.author = r.author.trimmed();
        item.url = r.url;
        item.imageUrl = r.imageUrl;
        item.description = r.description;
        item.language = primaryLanguage(r.language);
        item.categories = r.categories;
        // Without the newest episode the last change of the feed (135)
        item.newest = r.newestItemTime > 0 ? r.newestItemTime : r.lastUpdate;
        item.episodeCount = r.episodeCount;
        item.trending = trending;
        addCandidate(item);
    }
}

ForYouProfile ForYou::profile() const
{
    ForYouProfile profile;
    const QList<PodcastRow> rows = m_database->podcasts();
    const QHash<qint64, PlayStats> stats = m_database->playStats();
    const qint64 t = now();

    // The same podcast subscribed twice (two addresses) counts once
    QMap<QString, int> byKey;
    QHash<QString, int> languageCount;
    int withLanguage = 0;
    for (int i = 0; i < rows.size(); ++i) {
        const PodcastRow &row = rows.at(i);
        const QString k = podcastKey(row.title, row.author);
        const PlayStats s = stats.value(row.id);
        QStringList categories;
        for (int j = 0; j < row.categories.size(); ++j) {
            const QString c = currentCategory(row.categories.at(j));
            if (!c.isEmpty() && !categories.contains(c)) {
                categories.append(c);
            }
            // A mapped sub-category ("History" from "Society & Culture/History")
            // may stand without its top category - add the top as well
            const QString top = c.section(QLatin1Char('/'), 0, 0);
            if (!top.isEmpty() && !categories.contains(top)) {
                categories.append(top);
            }
        }
        if (byKey.contains(k)) {
            ForYouProfile::Podcast &p = profile.podcasts[byKey.value(k)];
            p.plays += s.plays;
            p.lastPlayed = qMax(p.lastPlayed, s.lastPlayed);
            for (int j = 0; j < categories.size(); ++j) {
                if (!p.categories.contains(categories.at(j))) {
                    p.categories.append(categories.at(j));
                }
            }
            continue;
        }
        ForYouProfile::Podcast p;
        p.id = row.id;
        p.title = row.title;
        p.author = row.author;
        p.url = row.url;
        p.plays = s.plays;
        p.lastPlayed = s.lastPlayed;
        p.categories = categories;
        p.language = primaryLanguage(row.language);
        byKey.insert(k, profile.podcasts.size());
        profile.podcasts.append(p);
        const QString language = primaryLanguage(row.language);
        if (!language.isEmpty()) {
            ++languageCount[language];
            ++withLanguage;
        }
    }

    // Weights and category shares
    QHash<QString, double> categoryWeight;
    double total = 0;
    for (int i = 0; i < profile.podcasts.size(); ++i) {
        ForYouProfile::Podcast &p = profile.podcasts[i];
        p.weight = SubscriptionWeight + std::log(1.0 + p.plays) / std::log(2.0)
                + ((p.lastPlayed > 0 && t - p.lastPlayed < RecentSeconds) ? 1.0 : 0.0);
        QStringList tops;
        for (int j = 0; j < p.categories.size(); ++j) {
            if (!p.categories.at(j).contains(QLatin1Char('/'))) {
                tops.append(p.categories.at(j));
            }
        }
        if (tops.isEmpty()) {
            continue;
        }
        total += p.weight;
        // The first top category is the feed's main one (Apple's rule):
        // it gets 2/3 of the weight, the others share the rest (132; an
        // even split left History at 1/3 of His2Go - "History, Science,
        // Education"); a sub-category gets the part of its top category
        QHash<QString, double> part;
        for (int j = 0; j < tops.size(); ++j) {
            part.insert(tops.at(j), tops.size() == 1 ? p.weight
                        : (j == 0 ? p.weight * MainShare : p.weight * (1.0 - MainShare) / (tops.size() - 1)));
        }
        for (int j = 0; j < p.categories.size(); ++j) {
            const QString c = p.categories.at(j);
            categoryWeight[c] += part.value(c.section(QLatin1Char('/'), 0, 0));
        }
    }
    std::sort(profile.podcasts.begin(), profile.podcasts.end(),
              [](const ForYouProfile::Podcast &a, const ForYouProfile::Podcast &b) { return a.weight > b.weight; });
    for (QHash<QString, double>::const_iterator it = categoryWeight.constBegin(); it != categoryWeight.constEnd(); ++it) {
        profile.categories.append(qMakePair(it.key(), total > 0 ? it.value() / total : 0.0));
    }
    std::sort(profile.categories.begin(), profile.categories.end(),
              [](const QPair<QString, double> &a, const QPair<QString, double> &b) { return a.second > b.second; });

    // The user's interests (133): more = twice the share, but at least half
    // of the strongest (a category not listened to yet becomes strong);
    // less = half; never = out, with its sub-categories
    double strongest = 0;
    for (int i = 0; i < profile.categories.size(); ++i) {
        profile.listened.insert(profile.categories.at(i).first, profile.categories.at(i).second);
        if (!profile.categories.at(i).first.contains(QLatin1Char('/'))) {
            strongest = qMax(strongest, profile.categories.at(i).second);
        }
    }
    profile.interests = interests();
    if (!profile.interests.isEmpty()) {
        // 134: at least the strongest (half of it gave 1.5 instead of 3 points)
        const double floor = strongest > 0 ? strongest : 0.5;
        QHash<QString, double> share;
        for (int i = 0; i < profile.categories.size(); ++i) {
            share.insert(profile.categories.at(i).first, profile.categories.at(i).second);
        }
        for (QHash<QString, int>::const_iterator it = profile.interests.constBegin();
             it != profile.interests.constEnd(); ++it) {
            const double base = share.value(it.key());
            if (it.value() == More) {
                share.insert(it.key(), qMax(2 * base, floor));
            } else if (it.value() == Less) {
                share.insert(it.key(), base / 2);
            } else if (it.value() == Never) {
                profile.never.append(it.key());
            }
        }
        profile.categories.clear();
        for (QHash<QString, double>::const_iterator it = share.constBegin(); it != share.constEnd(); ++it) {
            bool excluded = false;
            for (int j = 0; j < profile.never.size() && !excluded; ++j) {
                excluded = it.key() == profile.never.at(j) || it.key().startsWith(profile.never.at(j) + QLatin1Char('/'));
            }
            if (!excluded) {
                profile.categories.append(qMakePair(it.key(), it.value()));
            }
        }
        std::sort(profile.categories.begin(), profile.categories.end(),
                  [](const QPair<QString, double> &a, const QPair<QString, double> &b) { return a.second > b.second; });
    }

    // Languages of at least 20 % of the subscriptions
    QList<QPair<int, QString> > languages;
    for (QHash<QString, int>::const_iterator it = languageCount.constBegin(); it != languageCount.constEnd(); ++it) {
        if (withLanguage > 0 && double(it.value()) / withLanguage >= LanguageShare) {
            languages.append(qMakePair(it.value(), it.key()));
        }
    }
    std::sort(languages.begin(), languages.end(),
              [](const QPair<int, QString> &a, const QPair<int, QString> &b) { return a.first > b.first; });
    for (int i = 0; i < languages.size(); ++i) {
        profile.languages.append(languages.at(i).second);
    }
    // Chosen by the user (135)
    const QStringList chosen = chosenLanguages();
    if (!chosen.isEmpty()) {
        profile.languages = chosen;
        profile.languagesChosen = true;
    }
    if (profile.languages.isEmpty() && !m_phoneLanguage.isEmpty()) {
        profile.languages.append(m_phoneLanguage);
        profile.phoneLanguage = true;
    }
    // Language points by listening (135): 70 % German, 30 % English ->
    // German 4, English 1.7; a language without listening 0.5 of the strongest
    QHash<QString, double> languageWeight;
    double strongestLanguage = 0;
    for (int i = 0; i < profile.podcasts.size(); ++i) {
        const ForYouProfile::Podcast &p = profile.podcasts.at(i);
        if (profile.languages.contains(p.language)) {
            languageWeight[p.language] += p.weight;
            strongestLanguage = qMax(strongestLanguage, languageWeight.value(p.language));
        }
    }
    for (int i = 0; i < profile.languages.size(); ++i) {
        const QString l = profile.languages.at(i);
        const double w = languageWeight.value(l);
        profile.languageShares.insert(l, strongestLanguage > 0 && w > 0 ? w / strongestLanguage : 0.5);
    }
    if (profile.languages.size() == 1) {
        profile.languageShares.insert(profile.languages.first(), 1.0);
    }
    profile.moreKeywords = keywords(More);
    profile.neverKeywords = keywords(Never);
    return profile;
}

void ForYou::start()
{
    if (m_pending > 0) {
        return;
    }
    m_profile = profile();
    m_candidates.clear();
    m_collectionOwner.clear();
    m_queries.clear();

    // The 3 strongest top categories - or their strongest sub-category when
    // it holds at least half of the top category's weight
    for (int i = 0; i < m_profile.categories.size() && m_queries.size() < MaxQueries; ++i) {
        const QString top = m_profile.categories.at(i).first;
        if (top.contains(QLatin1Char('/'))) {
            continue;
        }
        const double topShare = m_profile.categories.at(i).second;
        Query query;
        query.category = top;
        for (int j = 0; j < m_profile.categories.size(); ++j) {
            const QString c = m_profile.categories.at(j).first;
            if (c.startsWith(top + QLatin1Char('/'))) {
                if (m_profile.categories.at(j).second >= topShare / 2) {
                    query.category = c;
                }
                break;      // sorted: the first one is the strongest
            }
        }
        m_queries.append(query);
    }

    request();      // held until everything is started
    request();
    m_fyyd->categories();       // for category names (and German ones), then fyyd hot
    const QStringList languages = m_profile.languages.mid(0, MaxLanguages);
    for (int l = 0; l < languages.size(); ++l) {
        if (m_queries.isEmpty()) {
            request();
            m_index->trending(languages.at(l), QString(), 40);
        }
        for (int q = 0; q < m_queries.size(); ++q) {
            // Podcast Index takes its own (split) names, comma = or
            const QString last = m_queries.at(q).category.section(QLatin1Char('/'), -1);
            request();
            m_index->trending(languages.at(l), words(last).join(QLatin1Char(',')), 40);
        }
    }
    // Keywords of "Mehr davon" (135): searched in both directories, every
    // form (the word and its translations) - the terms leave the phone as a
    // typed search would
    for (int k = 0; k < m_profile.moreKeywords.size(); ++k) {
        const QStringList &forms = m_profile.moreKeywords.at(k);
        for (int f = 0; f < forms.size() && f < MaxKeywordForms; ++f) {
            request();
            m_index->searchCached(forms.at(f), 40);
            request();
            m_fyyd->search(forms.at(f), 30);
        }
    }
    if (m_useSubscriptions) {
        for (int i = 0; i < m_profile.podcasts.size() && i < SimilarPodcasts; ++i) {
            const ForYouProfile::Podcast &p = m_profile.podcasts.at(i);
            request();
            m_fyyd->similarForFeed(p.url, 20, p.title, p.author);
        }
        for (int i = 0; i < m_profile.podcasts.size() && i < CollectionPodcasts; ++i) {
            const ForYouProfile::Podcast &p = m_profile.podcasts.at(i);
            request();
            m_fyyd->podcastCollectionsForFeed(p.url, CollectionsPerPodcast, p.title, p.author);
        }
    }
    done();
}

void ForYou::startFyydHot()
{
    // fyyd ignores a category for "hot" (4.88: category=technology brought
    // a start-up podcast first) - one long list per language, matched with
    // the profile's categories here
    const QStringList languages = m_profile.languages.mid(0, MaxLanguages);
    for (int l = 0; l < languages.size(); ++l) {
        request();
        m_fyyd->hot(languages.at(l), HotCount);
    }
}

void ForYou::done()
{
    if (m_pending > 0 && --m_pending == 0) {
        finish();
    }
}

QString ForYou::ownTitle(const QString &feedUrl) const
{
    for (int i = 0; i < m_profile.podcasts.size(); ++i) {
        if (Fyyd::sameFeed(m_profile.podcasts.at(i).url, feedUrl)) {
            return m_profile.podcasts.at(i).title;
        }
    }
    return QString();
}

QString ForYou::germanName(const QString &category) const
{
    const QString wanted = Fyyd::normalized(category.section(QLatin1Char('/'), -1));
    for (int i = 0; i < m_fyydCategories.size(); ++i) {
        if (Fyyd::normalized(m_fyydCategories.at(i).name) == wanted) {
            return m_fyydCategories.at(i).nameDe;
        }
    }
    return QString();
}

void ForYou::addFyyd(const QList<FyydPodcast> &podcasts, bool hot, const QString &similarTo, int collection,
                     const QString &collectionWith)
{
    QHash<int, FyydCategory> byId;
    for (int i = 0; i < m_fyydCategories.size(); ++i) {
        byId.insert(m_fyydCategories.at(i).id, m_fyydCategories.at(i));
    }
    for (int i = 0; i < podcasts.size(); ++i) {
        const FyydPodcast &p = podcasts.at(i);
        ForYouItem item;
        item.title = p.title;
        item.author = p.author;
        item.url = p.url;
        item.imageUrl = p.imageUrl;
        item.description = p.description;
        item.language = primaryLanguage(p.language);
        for (int j = 0; j < p.categories.size(); ++j) {
            const FyydCategory c = byId.value(p.categories.at(j));
            if (c.id <= 0) {
                continue;
            }
            const FyydCategory parent = byId.value(c.parentId);
            item.categories.append(parent.id > 0 ? parent.name + QLatin1Char('/') + c.name : c.name);
        }
        item.newest = p.lastPub;
        item.episodeCount = p.episodeCount;
        item.hot = hot;
        if (!similarTo.isEmpty()) {
            item.similarTo.append(similarTo);
        }
        if (collection > 0) {
            item.collections.insert(collection);
            item.collectionWith = collectionWith;
        }
        addCandidate(item);
    }
}

void ForYou::addCandidate(const ForYouItem &item)
{
    if (item.url.isEmpty() || item.title.isEmpty()) {
        return;
    }
    const QString k = podcastKey(item.title, item.author);
    if (!m_candidates.contains(k)) {
        m_candidates.insert(k, item);
        return;
    }
    // The same podcast from another source or under another address:
    // signals are merged, the data of the feed with the newer episode kept
    ForYouItem &have = m_candidates[k];
    ForYouItem merged = item.newest > have.newest ? item : have;
    merged.trending = have.trending || item.trending;
    merged.hot = have.hot || item.hot;
    merged.similarTo = have.similarTo;
    for (int i = 0; i < item.similarTo.size(); ++i) {
        if (!merged.similarTo.contains(item.similarTo.at(i))) {
            merged.similarTo.append(item.similarTo.at(i));
        }
    }
    merged.collections = have.collections + item.collections;
    merged.collectionWith = have.collectionWith.isEmpty() ? item.collectionWith : have.collectionWith;
    for (int i = 0; i < item.categories.size(); ++i) {
        if (!merged.categories.contains(item.categories.at(i))) {
            merged.categories.append(item.categories.at(i));
        }
    }
    for (int i = 0; i < have.categories.size(); ++i) {
        if (!merged.categories.contains(have.categories.at(i))) {
            merged.categories.append(have.categories.at(i));
        }
    }
    if (merged.language.isEmpty()) {
        merged.language = item.language.isEmpty() ? have.language : item.language;
    }
    have = merged;
}

void ForYou::finish()
{
    // Subscribed and hidden podcasts
    QSet<QString> ownKeys;
    QStringList ownUrls;
    const QList<PodcastRow> rows = m_database->podcasts();
    for (int i = 0; i < rows.size(); ++i) {
        ownKeys.insert(podcastKey(rows.at(i).title, rows.at(i).author));
        ownKeys.insert(podcastKey(rows.at(i).feedTitle, rows.at(i).author));
        ownUrls.append(rows.at(i).url);
    }
    const QJsonArray hidden = QJsonDocument::fromJson(m_database->setting(QLatin1String(HiddenKey)).toUtf8()).array();
    for (int i = 0; i < hidden.size(); ++i) {
        const QString value = hidden.at(i).toString();
        if (value.startsWith(QLatin1String("key:"))) {
            ownKeys.insert(value.mid(4));
        } else {
            ownUrls.append(value);
        }
    }

    double strongest = 0;
    for (int i = 0; i < m_profile.categories.size(); ++i) {
        if (!m_profile.categories.at(i).first.contains(QLatin1Char('/'))) {
            strongest = qMax(strongest, m_profile.categories.at(i).second);
        }
    }
    const bool haveProfile = strongest > 0;
    // Only strong categories bring a podcast in (132: one suggestion each
    // from categories with a tiny share - sexuality, religion, sports)
    const double minShare = strongest * StrongShare;
    const qint64 t = now();

    QList<ForYouItem> items;
    for (QHash<QString, ForYouItem>::iterator it = m_candidates.begin(); it != m_candidates.end(); ++it) {
        ForYouItem item = it.value();
        if (ownKeys.contains(it.key())) {
            continue;
        }
        bool own = false;
        for (int i = 0; i < ownUrls.size() && !own; ++i) {
            own = Fyyd::sameFeed(ownUrls.at(i), item.url);
        }
        if (own) {
            continue;
        }
        if (item.newest > 0 && t - item.newest > InactiveSeconds) {
            continue;
        }
        if (!item.language.isEmpty() && !m_profile.languages.isEmpty()
                && !m_profile.languages.contains(item.language)) {
            continue;
        }

        // Spam (136): a title in another script than the language, or a
        // spam domain in the title
        if (wrongScript(item.title, item.language) || domainInTitle(item.title)) {
            continue;
        }
        // A keyword of "Nie" (135) in title, description or categories: out
        const QStringList textTokens = TopicWords::tokens(item.title + QLatin1Char(' ') + item.description
                                                           + QLatin1Char(' ') + item.categories.join(QLatin1Char(' ')));
        bool unwanted = false;
        for (int k = 0; k < m_profile.neverKeywords.size() && !unwanted; ++k) {
            const QStringList &forms = m_profile.neverKeywords.at(k);
            for (int f = 0; f < forms.size() && !unwanted; ++f) {
                unwanted = TopicWords::containsPhrase(textTokens, forms.at(f));
            }
        }
        if (unwanted) {
            continue;
        }
        // A keyword of "Mehr davon" in title or description
        const QStringList ownTokens = TopicWords::tokens(item.title + QLatin1Char(' ') + item.description);
        QString keyword;
        for (int k = 0; k < m_profile.moreKeywords.size() && keyword.isEmpty(); ++k) {
            const QStringList &forms = m_profile.moreKeywords.at(k);
            for (int f = 0; f < forms.size() && keyword.isEmpty(); ++f) {
                if (TopicWords::containsPhrase(ownTokens, forms.at(f))) {
                    keyword = forms.first();
                }
            }
        }
        item.keywordPoints = keyword.isEmpty() ? 0.0 : 3.0;
        // Only from Podcast Index trending (136): at least 5 episodes when
        // the number is known - spam and demo feeds are new (trending
        // answers carry no number; the other sources do)
        if (item.trending && !item.hot && item.similarTo.isEmpty() && item.collections.isEmpty() && keyword.isEmpty()
                && item.episodeCount > 0 && item.episodeCount < MinTrendingEpisodes) {
            continue;
        }

        // A category set to "never" (133): out, whatever else speaks for it
        const QSet<QString> candidateWords = wordSet(item.categories);
        bool never = false;
        for (int i = 0; i < m_profile.never.size() && !never; ++i) {
            never = matches(m_profile.never.at(i).section(QLatin1Char('/'), -1), candidateWords);
        }
        if (never) {
            continue;
        }

        // Category: the best matching top category, relative to the
        // strongest one; +1 for a matching sub-category of the profile
        QString bestTop;
        double bestShare = 0;
        for (int i = 0; i < m_profile.categories.size(); ++i) {
            const QString c = m_profile.categories.at(i).first;
            if (!c.contains(QLatin1Char('/')) && m_profile.categories.at(i).second > bestShare
                    && m_profile.categories.at(i).second >= minShare && matches(c, candidateWords)) {
                bestTop = c;
                bestShare = m_profile.categories.at(i).second;
            }
        }
        // The bonus only for a sub-category of the matched top category
        // (130: "News/Business News" gave it to technology podcasts)
        QString bestSub;
        if (!bestTop.isEmpty()) {
            for (int i = 0; i < m_profile.categories.size(); ++i) {
                const QString c = m_profile.categories.at(i).first;
                if (c.startsWith(bestTop + QLatin1Char('/'))
                        && matches(c.section(QLatin1Char('/'), -1), candidateWords)) {
                    bestSub = c;    // sorted: the strongest one
                    break;
                }
            }
        }
        const bool subMatch = !bestSub.isEmpty();
        const QString bestCategory = bestSub.isEmpty() ? bestTop : bestSub;
        item.categoryPoints = (strongest > 0 ? 3.0 * bestShare / strongest : 0.0) + (subMatch ? 1.0 : 0.0);
        if (!item.language.isEmpty() && m_profile.languages.contains(item.language)) {
            item.languagePoints = 4.0 * m_profile.languageShares.value(item.language, 1.0);
        }
        if (!item.similarTo.isEmpty()) {
            item.similarPoints = qMin(7.0, 5.0 + (item.similarTo.size() - 1));
        }
        item.collectionPoints = qMin(6.0, 2.0 * item.collections.size());
        // Podcast Index trending is mostly business and AI shows: 1; fyyd hot 2
        item.trendPoints = qMin(3.0, (item.trending ? 1.0 : 0.0) + (item.hot ? 2.0 : 0.0));
        if (haveProfile && item.categoryPoints <= 0 && item.similarPoints <= 0 && item.collectionPoints <= 0
                && item.keywordPoints <= 0) {
            continue;
        }
        // "Für dich" is personal (133): from the general lists only with a
        // specific match - a strong sub-category, a top category without
        // sub-categories, or one the user wants more of
        if (haveProfile && item.similarTo.isEmpty() && item.collections.isEmpty() && keyword.isEmpty()) {
            bool specific = !bestSub.isEmpty() && m_profile.share(bestSub) >= minShare;
            for (size_t i = 0; i < sizeof(BroadSubs) / sizeof(BroadSubs[0]) && specific; ++i) {
                if (bestSub == QLatin1String(BroadSubs[i]) && m_profile.interests.value(bestSub) != More) {
                    specific = false;
                }
            }
            for (size_t i = 0; i < sizeof(SpecificTops) / sizeof(SpecificTops[0]) && !specific; ++i) {
                specific = bestTop == QLatin1String(SpecificTops[i]);
            }
            if (!specific && m_profile.interests.value(bestTop) == More) {
                specific = true;
            }
            if (!specific) {
                continue;
            }
        }
        item.score = item.categoryPoints + item.languagePoints + item.similarPoints + item.collectionPoints
                + item.trendPoints + item.keywordPoints;

        if (!item.similarTo.isEmpty()) {
            item.reason = ForYouItem::ReasonSimilar;
            item.reasonPodcast = item.similarTo.first();
        } else if (!keyword.isEmpty()) {
            item.reason = ForYouItem::ReasonKeyword;
            item.reasonKeyword = keyword;
        } else if (item.collections.size() >= 2) {
            item.reason = ForYouItem::ReasonCollections;
            item.reasonPodcast = item.collectionWith;
            item.reasonCount = item.collections.size();
        } else if (!bestCategory.isEmpty()) {
            item.reason = ForYouItem::ReasonCategory;
        } else {
            item.reason = ForYouItem::ReasonTrending;
        }
        item.reasonCategory = bestCategory;
        item.reasonCategoryDe = germanName(bestCategory);
        items.append(item);
    }
    std::sort(items.begin(), items.end(), [](const ForYouItem &a, const ForYouItem &b) {
        return a.score != b.score ? a.score > b.score : a.newest > b.newest;
    });
    // Variety (130): the categories (or kinds of reason) take turns in the
    // order of their best suggestion
    QStringList groupOrder;
    QHash<QString, QList<ForYouItem> > groups;
    for (int i = 0; i < items.size(); ++i) {
        const ForYouItem &item = items.at(i);
        QString group;
        switch (item.reason) {
        case ForYouItem::ReasonCategory:
            group = item.reasonCategory.section(QLatin1Char('/'), 0, 0);
            break;
        case ForYouItem::ReasonSimilar:
            group = QStringLiteral(":similar");
            break;
        case ForYouItem::ReasonCollections:
            group = QStringLiteral(":collections");
            break;
        case ForYouItem::ReasonTrending:
            group = QStringLiteral(":trending");
            break;
        case ForYouItem::ReasonKeyword:
            group = QStringLiteral(":keyword:") + item.reasonKeyword;
            break;
        }
        if (!groups.contains(group)) {
            groupOrder.append(group);
        }
        groups[group].append(item);
    }
    // 140: no limit per group any more (the list is long and loads on
    // scrolling) - the groups keep taking turns until all are used
    QList<ForYouItem> result;
    bool any = true;
    for (int round = 0; any && result.size() < m_maxItems; ++round) {
        any = false;
        for (int g = 0; g < groupOrder.size() && result.size() < m_maxItems; ++g) {
            const QList<ForYouItem> &list = groups[groupOrder.at(g)];
            if (round < list.size()) {
                result.append(list.at(round));
                any = true;
            }
        }
    }
    emit finished(m_profile, result);
}

void ForYou::setInterest(const QString &category, Interest level)
{
    static const char *const names[] = { "", "more", "less", "never" };
    m_database->setSetting(QLatin1String(InterestPrefix) + category.trimmed(),
                           QString::fromLatin1(names[level]));
}

QHash<QString, int> ForYou::interests() const
{
    QHash<QString, int> result;
    const QHash<QString, QString> values = m_database->settingsWithPrefix(QLatin1String(InterestPrefix));
    for (QHash<QString, QString>::const_iterator it = values.constBegin(); it != values.constEnd(); ++it) {
        if (it.value() == QLatin1String("more")) {
            result.insert(it.key(), More);
        } else if (it.value() == QLatin1String("less")) {
            result.insert(it.key(), Less);
        } else if (it.value() == QLatin1String("never")) {
            result.insert(it.key(), Never);
        }
    }
    return result;
}

bool ForYou::isSpam(const QString &title, const QString &language)
{
    return wrongScript(title, primaryLanguage(language)) || domainInTitle(title);
}

bool ForYou::isUnwanted(const QString &title, const QString &description, const QStringList &categories) const
{
    const QList<QStringList> never = keywords(Never);
    if (never.isEmpty()) {
        return false;
    }
    const QStringList tokens = TopicWords::tokens(title + QLatin1Char(' ') + description + QLatin1Char(' ')
                                                  + categories.join(QLatin1Char(' ')));
    for (int k = 0; k < never.size(); ++k) {
        for (int f = 0; f < never.at(k).size(); ++f) {
            if (TopicWords::containsPhrase(tokens, never.at(k).at(f))) {
                return true;
            }
        }
    }
    return false;
}

bool ForYou::categoryIn(const QString &category, const QStringList &categories)
{
    const QSet<QString> candidateWords = wordSet(categories);
    const QString top = category.section(QLatin1Char('/'), 0, 0);
    if (!matches(top, candidateWords)) {
        return false;
    }
    return !category.contains(QLatin1Char('/')) || matches(category.section(QLatin1Char('/'), 1), candidateWords);
}

void ForYou::setLanguages(const QStringList &languages)
{
    QStringList clean;
    for (int i = 0; i < languages.size(); ++i) {
        const QString l = primaryLanguage(languages.at(i));
        if (!l.isEmpty() && !clean.contains(l)) {
            clean.append(l);
        }
    }
    m_database->setSetting(QLatin1String(LanguagesKey), clean.join(QLatin1Char(',')));
}

QStringList ForYou::chosenLanguages() const
{
    QStringList result;
    const QStringList parts = m_database->setting(QLatin1String(LanguagesKey)).split(QLatin1Char(','));
    for (int i = 0; i < parts.size(); ++i) {
        const QString l = parts.at(i).trimmed();
        if (!l.isEmpty()) {
            result.append(l);
        }
    }
    return result;
}

QList<QStringList> ForYou::keywords(Interest kind) const
{
    QList<QStringList> result;
    const QJsonArray all = QJsonDocument::fromJson(
            m_database->setting(QLatin1String(kind == Never ? NeverKeywordsKey : MoreKeywordsKey)).toUtf8()).array();
    for (int i = 0; i < all.size(); ++i) {
        QStringList forms;
        const QJsonArray a = all.at(i).toArray();
        for (int j = 0; j < a.size(); ++j) {
            if (!a.at(j).toString().trimmed().isEmpty()) {
                forms.append(a.at(j).toString().trimmed());
            }
        }
        if (!forms.isEmpty()) {
            result.append(forms);
        }
    }
    return result;
}

QStringList ForYou::translate(const QString &word) const
{
    QStringList result = TopicWords::translate(word);
    QList<FyydCategory> categories = m_fyydCategories;
    if (categories.isEmpty()) {
        categories = m_fyyd->cachedCategories();
    }
    const QString w = Fyyd::normalized(word);
    for (int i = 0; i < categories.size(); ++i) {
        QString other;
        if (Fyyd::normalized(categories.at(i).nameDe) == w) {
            other = categories.at(i).name;
        } else if (Fyyd::normalized(categories.at(i).name) == w) {
            other = categories.at(i).nameDe;
        }
        if (!other.isEmpty() && Fyyd::normalized(other) != w && !result.contains(other, Qt::CaseInsensitive)) {
            result.append(other);
        }
    }
    return result;
}

QStringList ForYou::addKeyword(Interest kind, const QString &word, const QStringList &forms)
{
    const QString w = word.trimmed();
    if (w.isEmpty()) {
        return QStringList();
    }
    QStringList all;
    all.append(w);
    const QStringList others = forms.isEmpty() ? translate(w) : forms;
    for (int i = 0; i < others.size(); ++i) {
        if (!others.at(i).trimmed().isEmpty() && !all.contains(others.at(i).trimmed(), Qt::CaseInsensitive)) {
            all.append(others.at(i).trimmed());
        }
    }
    removeKeyword(kind, w);
    const char *key = kind == Never ? NeverKeywordsKey : MoreKeywordsKey;
    QJsonArray list = QJsonDocument::fromJson(m_database->setting(QLatin1String(key)).toUtf8()).array();
    list.append(QJsonArray::fromStringList(all));
    m_database->setSetting(QLatin1String(key), QString::fromUtf8(QJsonDocument(list).toJson(QJsonDocument::Compact)));
    return all;
}

void ForYou::removeKeyword(Interest kind, const QString &word)
{
    const char *key = kind == Never ? NeverKeywordsKey : MoreKeywordsKey;
    const QJsonArray list = QJsonDocument::fromJson(m_database->setting(QLatin1String(key)).toUtf8()).array();
    QJsonArray kept;
    for (int i = 0; i < list.size(); ++i) {
        const QJsonArray forms = list.at(i).toArray();
        if (!forms.isEmpty() && forms.at(0).toString().compare(word.trimmed(), Qt::CaseInsensitive) == 0) {
            continue;
        }
        kept.append(forms);
    }
    m_database->setSetting(QLatin1String(key), QString::fromUtf8(QJsonDocument(kept).toJson(QJsonDocument::Compact)));
}

bool ForYou::isHidden(const QString &feedUrl, const QString &title, const QString &author) const
{
    const QJsonArray hidden = QJsonDocument::fromJson(m_database->setting(QLatin1String(HiddenKey)).toUtf8()).array();
    const QString key = title.trimmed().isEmpty() ? QString() : podcastKey(title, author);
    for (int i = 0; i < hidden.size(); ++i) {
        const QString value = hidden.at(i).toString();
        if (value.startsWith(QLatin1String("key:"))) {
            if (!key.isEmpty() && value.mid(4) == key) {
                return true;
            }
        } else if (!feedUrl.isEmpty() && Fyyd::sameFeed(value, feedUrl)) {
            return true;
        }
    }
    return false;
}

bool ForYou::usefulCollection(const QString &title, const QString &description)
{
    const QString t = title.trimmed().toLower();
    if (t.isEmpty() || t.startsWith(QLatin1String("new collection for"))
            || t.startsWith(QLatin1String("initial collection"))) {
        return false;
    }
    // Advertising: several shop words and nothing about podcasts, or a long
    // text that never mentions podcasts or listening ("Azautodetailing")
    const QString d = description.toLower();
    if (d.contains(QLatin1String("podcast"))) {
        return true;
    }
    if (d.length() >= 150 && !d.contains(QStringLiteral("h\u00f6r")) && !d.contains(QLatin1String("listen"))) {
        return false;
    }
    static const char *const Shop[] = { "order", "buy", "online", "services", "shop", "price",
                                        "cheap", "quality", "delivery", "discount", "license" };
    int hits = 0;
    for (size_t i = 0; i < sizeof(Shop) / sizeof(Shop[0]); ++i) {
        if (d.contains(QLatin1String(Shop[i])) || t.contains(QLatin1String(Shop[i]))) {
            ++hits;
        }
    }
    return hits < 2;
}

void ForYou::hide(const QString &feedUrl, const QString &title, const QString &author)
{
    QJsonArray hidden = QJsonDocument::fromJson(m_database->setting(QLatin1String(HiddenKey)).toUtf8()).array();
    if (!feedUrl.trimmed().isEmpty()) {
        hidden.append(feedUrl.trimmed());
    }
    if (!title.trimmed().isEmpty()) {
        hidden.append(QStringLiteral("key:") + podcastKey(title, author));
    }
    m_database->setSetting(QLatin1String(HiddenKey),
                           QString::fromUtf8(QJsonDocument(hidden).toJson(QJsonDocument::Compact)));
}

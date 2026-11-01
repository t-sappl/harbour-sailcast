// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcore.h"

#include <algorithm>
#include <functional>

#include <QCollator>
#include <QHash>
#include <QSet>
#include "urlmask.h"
#include "nextcloudlogin.h"
#include "gpodderdevices.h"
#include "webdavbackup.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStorageInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QRegularExpression>
#include <QSqlQuery>
#include <QStandardPaths>
#include <QTimer>
#include <QUrl>
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
#include <QNetworkConfiguration>
#include <QNetworkConfigurationManager>
#endif

#include "archiver.h"
#include "chaptersloader.h"
#include "database.h"
#include "downloadmanager.h"
#include "feedprobe.h"
#include "feedupdater.h"
#include "foryou.h"
#include "episodelistmodel.h"
#include "topicwords.h"
#include "fyyd.h"
#include "gpodderimport.h"
#include "opml.h"
#include "podcastindex.h"
#include "syncengine.h"

namespace {

AppCore *s_instance = nullptr;

// Automatic downloads: at most this many new episodes per podcast and update
const int AutoDownloadLimit = 3;
// episodes.json of an archived podcast is written this long after the last
// change of its listening state
const int MetadataDelayMs = 60000;

bool isInside(const QString &path, const QString &folder)
{
    if (folder.isEmpty()) {
        return false;
    }
    return QFileInfo(path).absoluteFilePath().startsWith(QDir(folder).absolutePath() + QLatin1Char('/'));
}

QString today()
{
    return QDate::currentDate().toString(QStringLiteral("yyyy-MM-dd"));
}

// Show notes may contain images and scripts - QML's StyledText would load
// remote images while scrolling. Keep text and links only.
QString cleanDescription(const QString &html)
{
    QString text = html;
    text.remove(QRegularExpression(QStringLiteral("<(script|style)[^>]*>.*?</\\1>"),
                                   QRegularExpression::CaseInsensitiveOption
                                   | QRegularExpression::DotMatchesEverythingOption));
    text.remove(QRegularExpression(QStringLiteral("<img[^>]*>"), QRegularExpression::CaseInsensitiveOption));
    return text.trimmed();
}

} // namespace

AppCore::AppCore(QObject *parent)
    : QObject(parent)
    , m_database(new Database)
{
    s_instance = this;
    openDatabase();
    // Left over from builds before 16 (see FeedUpdater)
    m_database->clearConnectionErrors();

    m_updater = new FeedUpdater(m_database, this);
    m_downloads = new DownloadManager(m_database, m_updater, this);
    m_downloads->setDownloadDirectory(QStandardPaths::writableLocation(QStandardPaths::AppDataLocation)
                                      + QStringLiteral("/downloads"));
    m_sync = new SyncEngine(m_database, m_updater, this);
    m_archiver = new Archiver(m_database, m_updater, m_downloads, this);
    m_archiver->setArchiveRoot(archiveRoot());
    m_chapters = new ChaptersLoader(m_database, this);
    m_probe = new FeedProbe(this);
    connect(m_probe, &FeedProbe::finished, this, &AppCore::onFeedChecked);
    m_index = new PodcastIndex(this);
    connect(m_index, &PodcastIndex::searchFinished, this,
            [this](const QString &term, const QList<PodcastSearchResult> &results) {
        QVariantList list;
        for (int i = 0; i < results.size(); ++i) {
            list.append(podcastMap(results.at(i)));
        }
        emit searchFinished(term, list);
    });
    // Discover (137)
    m_forYou = new ForYou(m_database, this);
    m_forYou->setPhoneLanguage(QLocale::system().name());
    // A long list, loaded page by page while scrolling (140)
    m_forYou->setMaxItems(100);
    connect(m_forYou, &ForYou::finished, this, [this](const ForYouProfile &, const QList<ForYouItem> &items) {
        if (m_forYouAgain) {
            // Changed while it was running: compute again, show that one
            m_forYouAgain = false;
            refreshForYou();
            return;
        }
        QVariantList list;
        for (int i = 0; i < items.size(); ++i) {
            list.append(suggestionMap(items.at(i)));
        }
        emit forYouReady(list);
    });
    m_discoverFyyd = new Fyyd(this);
    // "Ähnliche Podcasts" (141)
    m_similarFyyd = new Fyyd(this);
    connect(m_similarFyyd, &Fyyd::similarReady, this,
            [this](const QString &feedUrl, int podcastId, const QList<FyydPodcast> &podcasts) {
        QVariantList list;
        for (int i = 0; i < podcasts.size(); ++i) {
            const FyydPodcast &p = podcasts.at(i);
            if (!ForYou::isSpam(p.title, p.language) && !m_forYou->isUnwanted(p.title, p.description, QStringList())) {
                list.append(podcastMap(p));
            }
        }
        emit similarReady(feedUrl, list, podcastId > 0);
    });
    connect(m_similarFyyd, &Fyyd::failed, this, [this](const QString &, const QString &error) {
        emit similarFailed(error);
    });
    // "Neu" (149): Hörsuppe's curation of first episodes, and their podcasts
    m_newFyyd = new Fyyd(this);
    connect(m_newFyyd, &Fyyd::curationReady, this, [this](const FyydCuration &, const QList<FyydEpisode> &episodes) {
        QList<FyydEpisode> sorted = episodes;
        std::stable_sort(sorted.begin(), sorted.end(), [](const FyydEpisode &a, const FyydEpisode &b) {
            return a.added > b.added;
        });
        QVariantList list;
        for (int i = 0; i < sorted.size(); ++i) {
            const FyydEpisode &e = sorted.at(i);
            const QString description = cleanDescription(e.description);
            if (e.podcastId <= 0 || m_forYou->isUnwanted(e.title, description, QStringList())) {
                continue;
            }
            QVariantMap map;
            map.insert(QStringLiteral("key"), QString::number(e.podcastId));
            map.insert(QStringLiteral("episodeTitle"), e.title);
            map.insert(QStringLiteral("guid"), e.guid);
            map.insert(QStringLiteral("imageUrl"), e.imageUrl);
            map.insert(QStringLiteral("published"), double(e.published));
            map.insert(QStringLiteral("duration"), e.duration);
            map.insert(QStringLiteral("added"), double(e.added));
            map.insert(QStringLiteral("description"), description.left(400));
            list.append(map);
        }
        emit newReady(QStringLiteral("de"), list);
    });
    connect(m_newFyyd, &Fyyd::failed, this, [this](const QString &, const QString &error) {
        emit newFailed(QStringLiteral("de"), error);
    });
    m_newInfoFyyd = new Fyyd(this);
    m_newInfoFyyd->cache().setMaxAge(7 * 24 * 3600);
    connect(m_newInfoFyyd, &Fyyd::podcastReady, this, [this](int requested, const FyydPodcast &p) {
        emit newPodcastReady(QString::number(requested), newPodcastMap(p));
    });
    connect(m_newInfoFyyd, &Fyyd::failed, this, [this](const QString &request, const QString &) {
        // "podcast 1234": the row stays without its podcast
        const int id = request.section(QLatin1Char(' '), 1).toInt();
        if (request.startsWith(QLatin1String("podcast ")) && id > 0) {
            emit newPodcastReady(QString::number(id), QVariantMap());
        }
    });
    // "Neu" in English (193): Podnews' trailers of new shows, and the shows
    // in Podcast Index (by the feed address Podnews names)
    m_podnews = new Podnews(this);
    connect(m_podnews, &Podnews::trailersReady, this, [this](const QList<PodnewsTrailer> &trailers) {
        m_newTrailers.clear();
        QVariantList list;
        for (int i = 0; i < trailers.size(); ++i) {
            const PodnewsTrailer &t = trailers.at(i);
            if (m_forYou->isUnwanted(t.title, t.description, QStringList() << t.genre)) {
                continue;
            }
            m_newTrailers.insert(t.feedUrl, t);
            QVariantMap map;
            map.insert(QStringLiteral("key"), t.feedUrl);
            map.insert(QStringLiteral("title"), t.title);
            map.insert(QStringLiteral("author"), t.author);
            map.insert(QStringLiteral("episodeTitle"), QString());
            map.insert(QStringLiteral("guid"), QString());
            map.insert(QStringLiteral("imageUrl"), t.imageUrl);
            map.insert(QStringLiteral("published"), double(t.published));
            map.insert(QStringLiteral("duration"), t.duration);
            map.insert(QStringLiteral("added"), double(t.published));
            map.insert(QStringLiteral("description"), t.description.left(400));
            map.insert(QStringLiteral("trailer"), true);
            list.append(map);
        }
        emit newReady(QStringLiteral("en"), list);
    });
    connect(m_podnews, &Podnews::failed, this, [this](const QString &error) {
        emit newFailed(QStringLiteral("en"), error);
    });
    m_newIndex = new PodcastIndex(this);
    m_newIndex->cache().setMaxAge(7 * 24 * 3600);
    connect(m_newIndex, &PodcastIndex::byFeedUrlFinished, this,
            [this](const QString &feedUrl, const QList<PodcastSearchResult> &results) {
        if (m_newTrailers.contains(feedUrl)) {
            const PodcastSearchResult r = results.isEmpty() ? PodcastSearchResult() : results.first();
            emit newPodcastReady(feedUrl, newPodcastMap(m_newTrailers.value(feedUrl), r));
        }
    });
    connect(m_newIndex, &PodcastIndex::byFeedUrlFailed, this, [this](const QString &feedUrl, const QString &) {
        // Not reachable (or no key): the row keeps Podnews' data
        if (m_newTrailers.contains(feedUrl)) {
            emit newPodcastReady(feedUrl, newPodcastMap(m_newTrailers.value(feedUrl), PodcastSearchResult()));
        }
    });

    m_discoverIndex = new PodcastIndex(this);
    connect(m_discoverIndex, &PodcastIndex::trendingFinished, this,
            [this](const QString &language, const QString &, const QList<PodcastSearchResult> &results) {
        QVariantList list;
        const QStringList never = m_forYou->profile().never;
        for (int i = 0; i < results.size(); ++i) {
            const PodcastSearchResult &r = results.at(i);
            if (!ForYou::isSpam(r.title, r.language) && !m_forYou->isUnwanted(r.title, r.description, r.categories)) {
                QVariantMap map = podcastMap(r);
                QStringList names;
                QStringList labels;
                categoriesOf(r.categories, &names, &labels);
                if (trendingRow(map, names, labels, never)) {
                    list.append(map);
                }
            }
        }
        trendingPartReady(language, false, list);
    });
    connect(m_discoverIndex, &PodcastIndex::trendingFailed, this,
            [this](const QString &language, const QString &, const QString &) {
        trendingPartReady(language, false, QVariantList());
    });
    // Browsing a category (150)
    m_catIndex = new PodcastIndex(this);
    connect(m_catIndex, &PodcastIndex::trendingFinished, this,
            [this](const QString &language, const QString &category, const QList<PodcastSearchResult> &results) {
        if (category != indexCategory(m_catName)) {
            return;
        }
        QVariantList list;
        for (int i = 0; i < results.size(); ++i) {
            const PodcastSearchResult &r = results.at(i);
            const QVariantMap map = podcastMap(r);
            if (!ForYou::isSpam(r.title, r.language) && !m_forYou->isUnwanted(r.title, r.description, r.categories)
                    && categoryWanted(map, r.newestItemTime)) {
                list.append(map);
            }
        }
        categoryPartReady(QStringLiteral("pi:") + language, list);
    });
    connect(m_catIndex, &PodcastIndex::trendingFailed, this,
            [this](const QString &language, const QString &category, const QString &) {
        if (category == indexCategory(m_catName)) {
            categoryPartReady(QStringLiteral("pi:") + language, QVariantList());
        }
    });
    m_catFyyd = new Fyyd(this);
    connect(m_catFyyd, &Fyyd::hotReady, this, [this](const QString &language, const QList<FyydPodcast> &podcasts) {
        // Only those of the category (fyyd's hot list ignores a category, 130)
        const QList<FyydCategory> tree = m_discoverFyyd->cachedCategories();
        QHash<int, FyydCategory> byId;
        for (int i = 0; i < tree.size(); ++i) {
            byId.insert(tree.at(i).id, tree.at(i));
        }
        const QString wanted = Fyyd::normalized(m_catName);
        const bool sub = m_catName.contains(QLatin1Char('/'));
        QVariantList list;
        for (int i = 0; i < podcasts.size(); ++i) {
            const FyydPodcast &p = podcasts.at(i);
            bool match = false;
            for (int c = 0; c < p.categories.size() && !match; ++c) {
                const FyydCategory cat = byId.value(p.categories.at(c));
                if (cat.id <= 0) {
                    continue;
                }
                const QString top = cat.parentId == 0 ? cat.name : byId.value(cat.parentId).name;
                const QString full = cat.parentId == 0 ? cat.name : top + QLatin1Char('/') + cat.name;
                match = Fyyd::normalized(sub ? full : top) == wanted;
            }
            const QVariantMap map = podcastMap(p);
            if (match && !ForYou::isSpam(p.title, p.language)
                    && !m_forYou->isUnwanted(p.title, p.description, QStringList())
                    && categoryWanted(map, p.lastPub)) {
                list.append(map);
            }
        }
        categoryPartReady(QStringLiteral("fy:") + language, list);
    });
    connect(m_catFyyd, &Fyyd::failed, this, [this](const QString &request, const QString &) {
        if (request.startsWith(QLatin1String("hot"))) {
            categoryPartReady(QStringLiteral("fy:") + request.mid(4).trimmed(), QVariantList());
        }
    });

    // The category tree arrived: German names for "Meine Interessen"
    connect(m_discoverFyyd, &Fyyd::categoriesReady, this, [this](const QList<FyydCategory> &) {
        m_tree.clear();     // (197) read again when next needed
        emit interestsChanged();
    });
    connect(m_discoverFyyd, &Fyyd::hotReady, this, [this](const QString &language, const QList<FyydPodcast> &podcasts) {
        QVariantList list;
        const QStringList never = m_forYou->profile().never;
        for (int i = 0; i < podcasts.size(); ++i) {
            const FyydPodcast &p = podcasts.at(i);
            if (!ForYou::isSpam(p.title, p.language) && !m_forYou->isUnwanted(p.title, p.description, QStringList())) {
                QVariantMap map = podcastMap(p);
                QStringList names;
                QStringList labels;
                categoriesOf(p.categories, &names, &labels);
                if (trendingRow(map, names, labels, never)) {
                    list.append(map);
                }
            }
        }
        trendingPartReady(language, true, list);
    });
    connect(m_discoverFyyd, &Fyyd::failed, this, [this](const QString &request, const QString &) {
        if (request.startsWith(QLatin1String("hot"))) {
            trendingPartReady(request.mid(4).trimmed(), true, QVariantList());
        }
    });
    // Nextcloud login in the browser (44)
    m_browserLogin = new NextcloudLogin(this);
    connect(m_browserLogin, &NextcloudLogin::activeChanged, this, &AppCore::browserLoginActiveChanged);
    connect(m_browserLogin, &NextcloudLogin::loginUrl, this, &AppCore::browserLoginUrl);
    connect(m_browserLogin, &NextcloudLogin::finished, this, &AppCore::browserLoginFinished);
    connect(m_browserLogin, &NextcloudLogin::failed, this, [this](int error, const QString &detail) {
        QString text;
        switch (error) {
        case NextcloudLogin::InvalidAddress: text = tr("Invalid server address"); break;
        case NextcloudLogin::NoNextcloud: text = tr("No Nextcloud found at this address"); break;
        case NextcloudLogin::NotStarted: text = tr("The server did not start the login - is it a Nextcloud?"); break;
        case NextcloudLogin::TimedOut: text = tr("The login in the browser was not completed in time"); break;
        case NextcloudLogin::Incomplete: text = tr("The server sent an incomplete answer"); break;
        default: text = tr("Login failed: %1").arg(detail); break;
        }
        emit browserLoginFailed(text);
    });
    // Database backup into the Nextcloud (83)
    m_webdav = new WebDavBackup(this);
    connect(m_webdav, &WebDavBackup::listed, this,
            [this](bool ok, const QList<WebDavBackup::Entry> &entries, const QString &error) {
        QVariantList list;
        for (int i = 0; i < entries.size(); ++i) {
            QVariantMap map;
            map.insert(QStringLiteral("name"), entries.at(i).name);
            map.insert(QStringLiteral("modified"), double(entries.at(i).modified));
            map.insert(QStringLiteral("size"), double(entries.at(i).size));
            list.append(map);
        }
        emit nextcloudBackupsListed(ok, list, UrlMask::maskText(error));
    });
    connect(m_webdav, &WebDavBackup::fetched, this, [this](bool ok, const QString &path, const QString &error) {
        emit nextcloudBackupFetched(ok, path, UrlMask::maskText(error));
    });
    connect(m_webdav, &WebDavBackup::finished, this, [this](bool ok, const QString &error) {
        if (ok) {
            m_database->setSetting(QStringLiteral("backup.lastNextcloud"),
                                   QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
            emit settingsChanged();
            if (m_webdavManual) {
                emitMessage(tr("Backup saved in the Nextcloud"));
            }
        } else {
            emitMessage(tr("Backup to the Nextcloud failed: %1").arg(error));
        }
        m_webdavManual = false;
    });
    // gpodder.net devices (51)
    m_devices = new GpodderDevices(this);
    connect(m_devices, &GpodderDevices::listed, this, [this](const QList<GpodderDevice> &devices) {
        QVariantList list;
        for (int i = 0; i < devices.size(); ++i) {
            QVariantMap map;
            map.insert(QStringLiteral("id"), devices.at(i).id);
            map.insert(QStringLiteral("caption"), devices.at(i).caption);
            map.insert(QStringLiteral("type"), devices.at(i).type);
            map.insert(QStringLiteral("subscriptions"), devices.at(i).subscriptions);
            map.insert(QStringLiteral("group"), devices.at(i).group);
            list.append(map);
        }
        emit devicesListed(list);
    });
    connect(m_devices, &GpodderDevices::failed, this, [this](const QString &error) {
        emit devicesFailed(tr("Devices could not be loaded: %1").arg(error));
    });
    connect(m_devices, &GpodderDevices::synchronized, this, [this]() {
        // The Sailcast device exists on the server now
        m_database->setSetting(QStringLiteral("sync.deviceExisting"), QStringLiteral("1"));
        emit settingsChanged();
        emitMessage(tr("The devices now share their subscriptions - synchronize to get them"));
        emit devicesSynchronized();
    });
    connect(m_devices, &GpodderDevices::synchronizeFailed, this, [this](const QString &error) {
        emitMessage(tr("The devices could not be grouped: %1").arg(error));
    });
    connect(m_index, &PodcastIndex::searchFailed, this, [this](const QString &term, const QString &error) {
        // Only the reason: the search page shows "Search failed" as the title
        // above it (59: the text said it twice)
        emit searchFailed(term, UrlMask::maskText(error));
    });
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    m_networkConfig = new QNetworkConfigurationManager(this);
#endif
    m_metadataTimer = new QTimer(this);
    m_metadataTimer->setSingleShot(true);
    m_metadataTimer->setInterval(MetadataDelayMs);
    connect(m_metadataTimer, &QTimer::timeout, this, &AppCore::writeArchiveMetadata);

    // --- Feed updates ---
    connect(m_updater, &FeedUpdater::progress, this, [this](int done, int total) {
        m_updateDone = done;
        m_updateTotal = total;
        emit updatingChanged();
    });
    connect(m_updater, &FeedUpdater::podcastUpdated, this, [this](qint64 id, int newEpisodes) {
        emit podcastRefreshed(int(id), true);
        // Loaded for "Anhören ohne Abo" (147): not counted as new episodes
        if (m_previews.contains(id)) {
            const QString guid = m_previews.take(id);
            const qint64 episodeId = guid.isEmpty() ? -1 : m_database->findEpisodeByGuid(id, guid);
            emit dataChanged();
            emit previewReady(int(id), int(episodeId));
            return;
        }
        if (m_database->podcast(id).provisional) {
            return;
        }
        m_updateRegular = true;
        m_updateNew += newEpisodes;
        if (newEpisodes > 0) {
            autoDownload(id, newEpisodes);
            // "New since you last looked" marker on the podcast (50)
            m_database->setSetting(QStringLiteral("podcast.%1.newAt").arg(id),
                                   QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
        }
    });
    connect(m_updater, &FeedUpdater::podcastFailed, this, [this](qint64 id, const QString &error) {
        emit podcastRefreshed(int(id), false);
        if (m_previews.contains(id)) {
            m_previews.remove(id);
            // Nothing stored yet: no trace left
            if (m_database->podcast(id).episodeCount == 0) {
                removeProvisional(id);
            }
            emit previewFailed(UrlMask::maskText(error));
            return;
        }
        if (m_database->podcast(id).provisional) {
            return;
        }
        m_updateRegular = true;
        ++m_updateFailed;
    });
    connect(m_updater, &FeedUpdater::finished, this, [this]() {
        m_database->applyPendingActions();
        // (168) Imported with "only the newest unplayed": now that the
        // episodes are there (and the sync's states applied)
        markImportedOlder();
        afterPlayed();
        m_updateDone = 0;
        m_updateTotal = 0;
        emit updatingChanged();
        emit dataChanged();
        if (!m_sync->isRunning() && m_updateRegular) {
            emitMessage(m_updateFailed > 0
                         ? tr("%n new episodes, %1 feeds failed", "", m_updateNew).arg(m_updateFailed)
                         : tr("%n new episodes", "", m_updateNew));
        }
        m_updateNew = 0;
        m_updateFailed = 0;
        m_updateRegular = false;
    });

    // --- Sync ---
    connect(m_sync, &SyncEngine::finished, this, [this](const SyncResult &r) {
        // Episodes played on another device leave the playlist here too
        afterPlayed();
        emit syncingChanged();
        emit dataChanged();
        emit queueChanged();
        emitMessage(tr("Synchronized: %1 listening states, %2 subscription changes")
                     .arg(r.applied.applied + r.actionsUploaded)
                     .arg(r.podcastsAdded + r.podcastsRemoved + r.subscriptionsUploaded));
        // The sync password is known now: a Nextcloud backup that is due
        backupIfDue();
    });
    connect(m_sync, &SyncEngine::failed, this, [this](const QString &error) {
        emit syncingChanged();
        emitMessage(tr("Sync failed: %1").arg(error));
    });

    // --- Downloads ---
    connect(m_downloads, &DownloadManager::progress, this, [this](qint64 id, qint64 received, qint64 total) {
        const qreal progress = total > 0 ? qreal(received) / qreal(total) : 0.0;
        m_downloadProgress.insert(int(id), progress);
        emit downloadProgressChanged(int(id), progress);
    });
    connect(m_downloads, &DownloadManager::downloaded, this, [this](qint64 id, const QString &) {
        m_downloadProgress.remove(int(id));
        emit downloadFinished(int(id), true);
        emit episodeChanged(int(id));
        emit downloadsChanged();
        // Download counts and the downloads list
        emit dataChanged();
    });
    connect(m_downloads, &DownloadManager::failed, this, [this](qint64 id, const QString &error) {
        m_downloadProgress.remove(int(id));
        emit downloadFinished(int(id), false);
        emit episodeChanged(int(id));
        emit downloadsChanged();
        const bool cancelledHere = m_cancelled.remove(int(id));
        if (!cancelledHere && error != DownloadManager::cancelledError()) {
            emitMessage(tr("Download failed: %1").arg(error));
        }
    });

    // --- Archive ---
    connect(m_archiver, &Archiver::progress, this, [this](const QString &) {
        // The archive queues its downloads right after this signal
        QTimer::singleShot(0, this, [this]() {
            if (m_archiveCancelled) {
                cancelArchive();
            }
            emit downloadsChanged();
        });
    });
    connect(m_archiver, &Archiver::finished, this, &AppCore::onArchiveFinished);
    connect(m_archiver, &Archiver::failed, this, [this](const QString &error) {
        // Also emitted when an archive could not start (no podcast id set)
        if (!m_archiver->isRunning()) {
            m_archivingId = 0;
            m_archiveCancelled = false;
            emit archivingChanged();
        }
        emitMessage(tr("Archiving failed: %1").arg(error));
    });

    // --- Chapters ---
    connect(m_chapters, &ChaptersLoader::loaded, this, [this](qint64 id, int) {
        emit episodeChanged(int(id));
    });

    reloadQueue();
    cleanupOnStart();
    // Not during the start itself: the backup copies the whole database
    QTimer::singleShot(20000, this, &AppCore::backupIfDue);
}

AppCore::~AppCore()
{
    // Pending episodes.json updates of archived podcasts
    writeArchiveMetadata();
    // The workers use the database - delete them first
    delete m_chapters;
    delete m_archiver;
    delete m_sync;
    delete m_downloads;
    delete m_updater;
    delete m_database;
    s_instance = nullptr;
}

AppCore *AppCore::instance()
{
    return s_instance;
}

QString AppCore::databasePath() const
{
    // Sailjail: ~/.local/share/harbour-sailcast/harbour-sailcast
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    QDir().mkpath(dir);
    return dir + QStringLiteral("/sailcast.db");
}

void AppCore::openDatabase()
{
    m_databaseError.clear();
    if (!m_database->open(databasePath())) {
        m_databaseError = m_database->lastError();
    }
}

bool AppCore::isKept(const EpisodeRow &e) const
{
    // (209) In the archive folder and named there (kept by "Save in Music"
    // or archived) - a file of "Downloads in Music" is a normal download
    return e.downloaded && isInside(e.downloadPath, archiveRoot()) && !e.archiveFile.isEmpty();
}

QString AppCore::archiveRoot() const
{
    const QString dir = m_database->setting(QStringLiteral("archive.dir"));
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                           + QStringLiteral("/Podcasts")
                         : dir;
}

QString AppCore::version() const
{
    return QStringLiteral("1.2 (build 240)");
}

void AppCore::reloadQueue()
{
    m_queue.clear();
    const QList<qint64> queue = m_database->queueIds();
    for (int i = 0; i < queue.size(); ++i) {
        m_queue.insert(int(queue.at(i)));
    }
}

bool AppCore::updating() const
{
    return m_updater->isBusy();
}

qreal AppCore::updateProgress() const
{
    return m_updateTotal > 0 ? qreal(m_updateDone) / qreal(m_updateTotal) : 0.0;
}

bool AppCore::syncing() const
{
    return m_sync->isRunning();
}

bool AppCore::syncConfigured() const
{
    return m_database->isSyncConfigured();
}

QString AppCore::syncAccount() const
{
    if (!m_database->isSyncConfigured()) {
        return QString();
    }
    return m_database->setting(QStringLiteral("sync.user")) + QStringLiteral(" @ ")
            + QUrl(m_database->setting(QStringLiteral("sync.server"))).host();
}

int AppCore::podcastCount() const
{
    return m_database->stats().podcasts;
}

int AppCore::queueCount() const
{
    return m_queue.size();
}

int AppCore::downloadCount() const
{
    return m_downloads->pendingIds().size();
}

// --- Feeds -------------------------------------------------------------------

void AppCore::updateAll()
{
    m_updater->updateAll();
    emit updatingChanged();
}

void AppCore::updatePodcast(int podcastId)
{
    m_updater->updatePodcast(podcastId);
    emit updatingChanged();
}

void AppCore::subscribe(const QString &url, int folderId)
{
    const qint64 id = m_updater->subscribe(url.trimmed());
    if (id < 0) {
        emitMessage(tr("Cannot subscribe: %1").arg(m_database->lastError()));
        return;
    }
    if (folderId > 0) {
        m_database->setPodcastFolder(id, folderId);
    }
    emit dataChanged();
    emit updatingChanged();
}

QVariantMap AppCore::podcastStats(int podcastId) const
{
    QVariantMap map;
    const PodcastRow p = m_database->podcast(podcastId);
    map.insert(QStringLiteral("listened"), double(p.listenedSeconds));
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    int started = 0;
    int downloads = 0;
    qint64 bytes = 0;
    QList<qint64> dates;
    for (const EpisodeRow &e : episodes) {
        if (e.played || e.position > 0) {
            ++started;
        }
        if (e.downloaded) {
            ++downloads;
            bytes += QFileInfo(e.downloadPath).size();
        }
        if (e.published > 0) {
            dates.append(e.published);
        }
    }
    map.insert(QStringLiteral("started"), started);
    map.insert(QStringLiteral("downloads"), downloads);
    map.insert(QStringLiteral("bytes"), double(bytes));

    const QVariantMap rhythm = rhythmOf(dates);
    map.insert(QStringLiteral("rhythm"), rhythm.value(QStringLiteral("rhythm")));
    map.insert(QStringLiteral("next"), rhythm.value(QStringLiteral("next")));
    return map;
}

QVariantMap AppCore::rhythmOf(QList<qint64> dates)
{
    QVariantMap map;
    // Rhythm from the last up to 10 intervals: the median, and whether most
    // intervals are near it (within 30 %)
    std::sort(dates.begin(), dates.end(), std::greater<qint64>());
    QList<double> days;
    for (int i = 0; i + 1 < dates.size() && days.size() < 10; ++i) {
        const double d = double(dates.at(i) - dates.at(i + 1)) / 86400.0;
        if (d > 0.1) {
            days.append(d);
        }
    }
    QString rhythm;
    qint64 next = 0;
    if (days.size() >= 3) {
        QList<double> sorted = days;
        std::sort(sorted.begin(), sorted.end());
        const double median = sorted.at(sorted.size() / 2);
        int near = 0;
        for (int i = 0; i < days.size(); ++i) {
            near += qAbs(days.at(i) - median) <= median * 0.3 + 0.5 ? 1 : 0;
        }
        const bool regular = near * 10 >= days.size() * 6;
        // The usual weekday (local time), when at least 70 % of the last
        // episodes share it
        QHash<int, int> weekdays;
        const int look = qMin(dates.size(), 8);
        for (int i = 0; i < look; ++i) {
            weekdays[QDateTime::fromMSecsSinceEpoch(dates.at(i) * 1000).date().dayOfWeek()] += 1;
        }
        int weekday = 0;
        for (QHash<int, int>::const_iterator it = weekdays.constBegin(); it != weekdays.constEnd(); ++it) {
            if (it.value() * 10 >= look * 7) {
                weekday = it.key();
            }
        }
        const QString day = weekday > 0 ? QLocale().dayName(weekday, QLocale::LongFormat) : QString();
        if (!regular) {
            rhythm = tr("irregular");
        } else if (median <= 1.5) {
            rhythm = tr("daily");
        } else if (median >= 5.5 && median <= 8.5) {
            rhythm = day.isEmpty() ? tr("weekly") : tr("weekly, %1").arg(day);
        } else if (median >= 12 && median <= 16) {
            rhythm = day.isEmpty() ? tr("every 2 weeks") : tr("every 2 weeks, %1").arg(day);
        } else if (median >= 26 && median <= 35) {
            rhythm = tr("monthly");
        } else if (median > 16 && qAbs(median / 7.0 - qRound(median / 7.0)) <= 0.15) {
            rhythm = tr("every %n weeks", "", qRound(median / 7.0));
        } else {
            rhythm = tr("every %n days", "", qRound(median));
        }
        if (regular) {
            const qint64 estimate = dates.first() + qint64(median * 86400.0);
            // Not when it is overdue already (a pause, or the end)
            if (estimate * 1000 >= QDateTime::currentMSecsSinceEpoch() - 86400000LL) {
                next = estimate * 1000;
            }
        }
    }
    map.insert(QStringLiteral("rhythm"), rhythm);
    map.insert(QStringLiteral("next"), double(next));
    return map;
}

QStringList AppCore::categoryLabels(const QStringList &categories) const
{
    const QList<FyydCategory> tree = m_discoverFyyd->cachedCategories();
    const bool german = QLocale::system().language() == QLocale::German;
    auto label = [&tree, german](const QString &name, bool sub) {
        for (int i = 0; i < tree.size(); ++i) {
            if ((tree.at(i).parentId != 0) == sub && Fyyd::normalized(tree.at(i).name) == Fyyd::normalized(name)) {
                return german && !tree.at(i).nameDe.isEmpty() ? tree.at(i).nameDe : tree.at(i).name;
            }
        }
        return name;
    };
    QStringList tops;
    QStringList result;
    for (int i = 0; i < categories.size(); ++i) {
        const QString c = categories.at(i).trimmed();
        if (c.contains(QLatin1Char('/'))) {
            tops.append(c.section(QLatin1Char('/'), 0, 0));
        }
    }
    for (int i = 0; i < categories.size(); ++i) {
        const QString c = categories.at(i).trimmed();
        if (c.isEmpty()) {
            continue;
        }
        QString text;
        if (c.contains(QLatin1Char('/'))) {
            text = label(c.section(QLatin1Char('/'), 0, 0), false) + QStringLiteral(" / ")
                    + label(c.section(QLatin1Char('/'), 1), true);
        } else if (!tops.contains(c)) {
            text = label(c, false);
        }
        if (!text.isEmpty() && !result.contains(text)) {
            result.append(text);
        }
    }
    return result;
}

QString AppCore::languageName(const QString &code) const
{
    const QString c = code.trimmed().toLower().section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0);
    const QVariantMap names = interests().value(QStringLiteral("languageNames")).toMap();
    return names.value(c, code).toString();
}

void AppCore::subscribePodcast(int podcastId)
{
    const PodcastRow p = m_database->podcast(podcastId);
    if (p.id > 0 && p.provisional) {
        subscribe(p.url);
    }
}

void AppCore::listenWithoutSubscribing(const QString &url, const QString &title, const QString &guid)
{
    const QString address = FeedProbe::normalize(url);
    qint64 id = m_database->findPodcast(address);
    if (id >= 0 && m_database->podcast(id).episodeCount > 0) {
        // Subscribed, or listened to before: there already
        const qint64 episodeId = guid.isEmpty() ? -1 : m_database->findEpisodeByGuid(id, guid);
        if (!guid.isEmpty() && episodeId < 0 && m_database->podcast(id).provisional) {
            // A newer episode than the stored ones: load the feed again
            m_previews.insert(id, guid);
            m_updater->updatePodcast(id);
            return;
        }
        emit previewReady(int(id), int(episodeId));
        return;
    }
    if (id < 0) {
        id = m_database->addPodcast(address, title, false, true);
    }
    if (id < 0) {
        emit previewFailed(m_database->lastError());
        return;
    }
    m_previews.insert(id, guid);
    m_updater->updatePodcast(id);
}

// Removes a podcast listened to without subscribing (147): its downloads
// (not files in the archive), settings and the row - no message, no sync
void AppCore::removeProvisional(qint64 podcastId)
{
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        if (!isKept(downloads.at(i))) {
            QFile::remove(downloads.at(i).downloadPath);
        }
    }
    m_database->removePodcast(podcastId, false);
    m_database->removeSettings(QStringLiteral("podcast.%1.").arg(podcastId));
}

void AppCore::unsubscribe(int podcastId)
{
    // Running downloads of the podcast end first
    const QList<qint64> pending = m_downloads->pendingIds();
    for (int i = 0; i < pending.size(); ++i) {
        if (m_database->episode(pending.at(i)).podcastId == podcastId) {
            cancelDownload(int(pending.at(i)));
        }
    }
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        // Files in the archive stay, normal downloads are deleted (209: also
        // those in Music by "Downloads in Music")
        if (!isKept(downloads.at(i))) {
            QFile::remove(downloads.at(i).downloadPath);
        }
    }
    const QString title = m_database->podcast(podcastId).title;
    // Never subscribed (147): nothing to tell the sync server
    if (!m_database->removePodcast(podcastId, !m_database->podcast(podcastId).provisional)) {
        qWarning("[AppCore] Unsubscribe %d failed: %s", podcastId, qPrintable(m_database->lastError()));
        emitMessage(tr("Cannot unsubscribe: %1").arg(m_database->lastError()));
        return;
    }
    m_updater->setPassword(podcastId, QString());
    m_downloads->setPassword(podcastId, QString());
    emit bookmarksChanged(0);   // its bookmarks went with its episodes
    m_database->removeSettings(QStringLiteral("podcast.%1.").arg(podcastId));
    m_metadataDirty.remove(podcastId);
    reloadQueue();
    emit dataChanged();
    emit queueChanged();
    emitMessage(tr("Unsubscribed: %1").arg(title));
}

void AppCore::checkFeed(const QString &url)
{
    m_probe->check(url);
}

bool AppCore::searchAvailable() const
{
    return m_index->isAvailable();
}

void AppCore::searchPodcasts(const QString &term)
{
    m_index->search(term);
}

QVariantMap AppCore::podcastMap(const PodcastSearchResult &r) const
{
    QVariantMap map;
    map.insert(QStringLiteral("title"), r.title);
    map.insert(QStringLiteral("author"), r.author);
    map.insert(QStringLiteral("url"), r.url);
    map.insert(QStringLiteral("imageUrl"), r.imageUrl);
    map.insert(QStringLiteral("description"), r.description);
    map.insert(QStringLiteral("language"), r.language);
    map.insert(QStringLiteral("episodes"), r.episodeCount);
    map.insert(QStringLiteral("link"), r.link);
    map.insert(QStringLiteral("ownerName"), r.ownerName);
    map.insert(QStringLiteral("categories"), r.categories);
    map.insert(QStringLiteral("lastUpdate"), double(r.lastUpdate > 0 ? r.lastUpdate : r.newestItemTime));
    map.insert(QStringLiteral("explicit"), r.explicitContent);
    map.insert(QStringLiteral("fundingUrl"), r.fundingUrl);
    map.insert(QStringLiteral("fundingMessage"), r.fundingMessage);
    map.insert(QStringLiteral("subscribed"), m_database->findSubscribed(r.url) >= 0);
    return map;
}

QVariantMap AppCore::podcastMap(const FyydPodcast &p) const
{
    QVariantMap map;
    map.insert(QStringLiteral("title"), p.title);
    map.insert(QStringLiteral("author"), p.author);
    map.insert(QStringLiteral("url"), p.url);
    map.insert(QStringLiteral("imageUrl"), p.imageUrl);
    map.insert(QStringLiteral("description"), p.description);
    map.insert(QStringLiteral("language"), p.language);
    map.insert(QStringLiteral("episodes"), p.episodeCount);
    map.insert(QStringLiteral("link"), p.link);
    map.insert(QStringLiteral("lastUpdate"), double(p.lastPub));
    map.insert(QStringLiteral("subscribed"), m_database->findSubscribed(p.url) >= 0);
    return map;
}

QVariantMap AppCore::suggestionMap(const ForYouItem &item) const
{
    QVariantMap map;
    map.insert(QStringLiteral("title"), item.title);
    map.insert(QStringLiteral("author"), item.author);
    map.insert(QStringLiteral("url"), item.url);
    map.insert(QStringLiteral("imageUrl"), item.imageUrl);
    map.insert(QStringLiteral("description"), item.description);
    map.insert(QStringLiteral("language"), item.language);
    map.insert(QStringLiteral("episodes"), item.episodeCount);
    map.insert(QStringLiteral("categories"), item.categories);
    map.insert(QStringLiteral("lastUpdate"), double(item.newest));
    map.insert(QStringLiteral("subscribed"), false);
    // The reason, worded here (the core gives the parts)
    QString reason;
    switch (item.reason) {
    case ForYouItem::ReasonSimilar:
        reason = tr("Similar to %1").arg(item.reasonPodcast);
        break;
    case ForYouItem::ReasonKeyword:
        reason = tr("Keyword \"%1\"").arg(item.reasonKeyword);
        break;
    case ForYouItem::ReasonCollections:
        reason = tr("In %n collections with %1", "", item.reasonCount).arg(item.reasonPodcast);
        break;
    case ForYouItem::ReasonCategory: {
        const QString name = QLocale::system().language() == QLocale::German && !item.reasonCategoryDe.isEmpty()
                ? item.reasonCategoryDe : item.reasonCategory.section(QLatin1Char('/'), -1);
        reason = tr("Because you listen to %1").arg(name);
        break;
    }
    case ForYouItem::ReasonTrending:
        reason = tr("Trending");
        break;
    }
    map.insert(QStringLiteral("reason"), reason);
    return map;
}

void AppCore::discover()
{
    refreshForYou();
    const ForYouProfile profile = m_forYou->profile();
    discoverTrending(profile.languages.isEmpty() ? QString() : profile.languages.first());
}

void AppCore::refreshForYou()
{
    m_forYou->setUseSubscriptions(m_database->setting(QStringLiteral("discover.fromSubscriptions"),
                                                      QStringLiteral("1")) == QLatin1String("1"));
    emit forYouStarted();
    if (m_forYou->isRunning()) {
        m_forYouAgain = true;
        return;
    }
    m_forYou->start();
}

void AppCore::discoverTrending(const QString &language)
{
    const QString lang = language.trimmed().toLower();
    m_trendIndex.remove(lang);
    m_trendFyyd.remove(lang);
    m_trendIndexDone.remove(lang);
    m_trendFyydDone.remove(lang);
    m_discoverIndex->trending(lang, QString(), 100);
    m_discoverFyyd->hot(lang, 100);
}

QString AppCore::indexCategory(const QString &category)
{
    QString name = category.contains(QLatin1Char('/')) ? category.section(QLatin1Char('/'), 1) : category;
    name.replace(QStringLiteral(" & "), QStringLiteral(","));
    return name;
}

void AppCore::discoverCategory(const QString &category)
{
    m_catName = category.trimmed();
    m_catResults.clear();
    m_catParts.clear();
    QStringList languages = m_forYou->profile().languages;
    if (languages.isEmpty()) {
        languages << QLocale::system().name().section(QLatin1Char('_'), 0, 0);
    }
    for (int i = 0; i < languages.size(); ++i) {
        m_catParts << QStringLiteral("pi:") + languages.at(i) << QStringLiteral("fy:") + languages.at(i);
    }
    for (int i = 0; i < languages.size(); ++i) {
        m_catIndex->trending(languages.at(i), indexCategory(m_catName), 100);
        m_catFyyd->hot(languages.at(i), 200);
    }
}

// Not hidden, an episode in the last 24 months (when known)
bool AppCore::categoryWanted(const QVariantMap &map, qint64 newest) const
{
    if (newest > 0 && newest < QDateTime::currentMSecsSinceEpoch() / 1000 - qint64(730) * 86400) {
        return false;
    }
    return !m_forYou->isHidden(map.value(QStringLiteral("url")).toString(), map.value(QStringLiteral("title")).toString(),
                               map.value(QStringLiteral("author")).toString());
}

void AppCore::categoryPartReady(const QString &part, const QVariantList &items)
{
    if (!m_catParts.contains(part) || m_catResults.contains(part)) {
        return;
    }
    m_catResults.insert(part, items);
    if (m_catResults.size() < m_catParts.size()) {
        return;
    }
    // Taking turns between the sources, the same podcast once
    QVariantList merged;
    QSet<QString> seen;
    QStringList urls;
    int longest = 0;
    for (int p = 0; p < m_catParts.size(); ++p) {
        longest = qMax(longest, m_catResults.value(m_catParts.at(p)).size());
    }
    for (int i = 0; i < longest; ++i) {
        for (int p = 0; p < m_catParts.size(); ++p) {
            const QVariantList list = m_catResults.value(m_catParts.at(p));
            if (i >= list.size()) {
                continue;
            }
            const QVariantMap map = list.at(i).toMap();
            const QString key = Fyyd::normalized(map.value(QStringLiteral("title")).toString()) + QLatin1Char('|')
                    + Fyyd::normalized(map.value(QStringLiteral("author")).toString());
            const QString url = map.value(QStringLiteral("url")).toString();
            bool known = seen.contains(key);
            for (int u = 0; u < urls.size() && !known; ++u) {
                known = Fyyd::sameFeed(urls.at(u), url);
            }
            if (known) {
                continue;
            }
            seen.insert(key);
            urls.append(url);
            merged.append(map);
        }
    }

    // Order (152, Thomas: "Brandschutz To Go" first under Technik): named by
    // both sources first, then what fits the user (a "Mehr davon" keyword,
    // a strongly listened sub-category - with the reason), then the rest,
    // subscribed ones last; otherwise the sources keep taking turns
    QHash<QString, int> sources;
    for (int p = 0; p < m_catParts.size(); ++p) {
        const QVariantList list = m_catResults.value(m_catParts.at(p));
        QSet<QString> inThisPart;
        for (int i = 0; i < list.size(); ++i) {
            const QVariantMap map = list.at(i).toMap();
            inThisPart.insert(Fyyd::normalized(map.value(QStringLiteral("title")).toString()) + QLatin1Char('|')
                              + Fyyd::normalized(map.value(QStringLiteral("author")).toString()));
        }
        for (QSet<QString>::const_iterator it = inThisPart.constBegin(); it != inThisPart.constEnd(); ++it) {
            sources[*it] += 1;
        }
    }
    const ForYouProfile profile = m_forYou->profile();
    double strongest = 0;
    for (int i = 0; i < profile.categories.size(); ++i) {
        if (!profile.categories.at(i).first.contains(QLatin1Char('/'))) {
            strongest = qMax(strongest, profile.categories.at(i).second);
        }
    }
    // Strong sub-categories of this category: the last word of the name
    // as Podcast Index lists it ("Leisure/Automotive" -> "automotive")
    QHash<QString, QString> strongSubs;
    const QList<FyydCategory> tree = m_discoverFyyd->cachedCategories();
    const bool german = QLocale::system().language() == QLocale::German;
    for (int i = 0; i < profile.categories.size(); ++i) {
        const QString name = profile.categories.at(i).first;
        if (!name.contains(QLatin1Char('/')) || name == QLatin1String("Leisure/Hobbies")
                || strongest <= 0 || profile.categories.at(i).second < strongest / 3) {
            continue;
        }
        QString label = name.section(QLatin1Char('/'), 1);
        for (int t = 0; t < tree.size(); ++t) {
            if (tree.at(t).parentId != 0 && Fyyd::normalized(tree.at(t).name) == Fyyd::normalized(label)) {
                label = german && !tree.at(t).nameDe.isEmpty() ? tree.at(t).nameDe : tree.at(t).name;
            }
        }
        strongSubs.insert(Fyyd::normalized(name.section(QLatin1Char('/'), 1)), label);
    }
    QList<QVariantList> groups;
    for (int g = 0; g < 4; ++g) {
        groups.append(QVariantList());
    }
    for (int i = 0; i < merged.size(); ++i) {
        QVariantMap map = merged.at(i).toMap();
        const QString key = Fyyd::normalized(map.value(QStringLiteral("title")).toString()) + QLatin1Char('|')
                + Fyyd::normalized(map.value(QStringLiteral("author")).toString());
        // The reason: a keyword, else a strong sub-category
        const QStringList tokens = TopicWords::tokens(map.value(QStringLiteral("title")).toString() + QLatin1Char(' ')
                                                      + map.value(QStringLiteral("description")).toString());
        QString reason;
        for (int k = 0; k < profile.moreKeywords.size() && reason.isEmpty(); ++k) {
            for (int f = 0; f < profile.moreKeywords.at(k).size() && reason.isEmpty(); ++f) {
                if (TopicWords::containsPhrase(tokens, profile.moreKeywords.at(k).at(f))) {
                    reason = tr("Keyword \"%1\"").arg(profile.moreKeywords.at(k).first());
                }
            }
        }
        const QStringList categories = map.value(QStringLiteral("categories")).toStringList();
        for (int c = 0; c < categories.size() && reason.isEmpty(); ++c) {
            const QString label = strongSubs.value(Fyyd::normalized(categories.at(c)));
            if (!label.isEmpty()) {
                reason = tr("Because you listen to %1").arg(label);
            }
        }
        if (!reason.isEmpty()) {
            map.insert(QStringLiteral("reason"), reason);
        }
        const int group = map.value(QStringLiteral("subscribed")).toBool() ? 3
                        : sources.value(key) >= 2 ? 0 : !reason.isEmpty() ? 1 : 2;
        groups[group].append(map);
    }
    QVariantList ordered;
    for (int g = 0; g < groups.size(); ++g) {
        ordered += groups.at(g);
    }
    emit categoryReady(m_catName, ordered);
}

void AppCore::trendingPartReady(const QString &language, bool fromFyyd, const QVariantList &items)
{
    if (fromFyyd) {
        m_trendFyyd.insert(language, items);
        m_trendFyydDone.insert(language);
    } else {
        m_trendIndex.insert(language, items);
        m_trendIndexDone.insert(language);
    }
    if (!m_trendFyydDone.contains(language) || !m_trendIndexDone.contains(language)) {
        return;
    }
    // Taking turns (fyyd first - better stocked for German), the same
    // podcast once (title + author, or the same address)
    const QVariantList a = m_trendFyyd.value(language);
    const QVariantList b = m_trendIndex.value(language);
    QVariantList merged;
    QSet<QString> seen;
    QStringList urls;
    for (int i = 0; i < qMax(a.size(), b.size()); ++i) {
        for (int k = 0; k < 2; ++k) {
            const QVariantList &list = k == 0 ? a : b;
            if (i >= list.size()) {
                continue;
            }
            const QVariantMap map = list.at(i).toMap();
            const QString key = Fyyd::normalized(map.value(QStringLiteral("title")).toString()) + QLatin1Char('|')
                    + Fyyd::normalized(map.value(QStringLiteral("author")).toString());
            const QString url = map.value(QStringLiteral("url")).toString();
            bool known = seen.contains(key);
            for (int u = 0; u < urls.size() && !known; ++u) {
                known = Fyyd::sameFeed(urls.at(u), url);
            }
            if (known) {
                continue;
            }
            seen.insert(key);
            urls.append(url);
            merged.append(map);
        }
    }
    emit trendingReady(language, merged);
}

QVariantMap AppCore::interests() const
{
    QVariantMap result;
    const ForYouProfile profile = m_forYou->profile();
    // Languages
    result.insert(QStringLiteral("languages"), profile.languages);
    result.insert(QStringLiteral("languagesChosen"), profile.languagesChosen);
    QVariantMap names;
    QVariantList available;
    // Own names (140): QLocale gives the main country's variant ("American
    // English", "Español de España")
    struct Language { const char *code; const char *name; };
    static const Language codes[] = {
        { "de", "Deutsch" }, { "en", "English" }, { "fr", "Français" }, { "es", "Español" },
        { "it", "Italiano" }, { "nl", "Nederlands" }, { "pt", "Português" }, { "pl", "Polski" },
        { "sv", "Svenska" }, { "da", "Dansk" }, { "no", "Norsk" }, { "fi", "Suomi" },
        { "cs", "Čeština" }, { "hu", "Magyar" }, { "hr", "Hrvatski" }, { "sl", "Slovenščina" },
        { "sk", "Slovenčina" }, { "ro", "Română" }, { "tr", "Türkçe" }, { "el", "Ελληνικά" },
        { "ru", "Русский" }, { "uk", "Українська" }, { "ja", "日本語" }, { "zh", "中文" } };
    for (size_t i = 0; i < sizeof(codes) / sizeof(codes[0]); ++i) {
        const QString code = QString::fromLatin1(codes[i].code);
        const QString name = QString::fromUtf8(codes[i].name);
        names.insert(code, name.isEmpty() ? code : name);
        QVariantMap entry;
        entry.insert(QStringLiteral("code"), code);
        entry.insert(QStringLiteral("name"), name.isEmpty() ? code : name);
        available.append(entry);
    }
    for (int i = 0; i < profile.languages.size(); ++i) {
        if (!names.contains(profile.languages.at(i))) {
            names.insert(profile.languages.at(i), profile.languages.at(i));
        }
    }
    result.insert(QStringLiteral("languageNames"), names);
    result.insert(QStringLiteral("available"), available);

    // Categories: fyyd's tree (German names), shares from the profile
    QHash<QString, double> share;
    double strongest = 0;
    for (int i = 0; i < profile.categories.size(); ++i) {
        share.insert(profile.categories.at(i).first, profile.categories.at(i).second);
        if (!profile.categories.at(i).first.contains(QLatin1Char('/'))) {
            strongest = qMax(strongest, profile.categories.at(i).second);
        }
    }
    const bool german = QLocale::system().language() == QLocale::German;
    const QList<FyydCategory> tree = m_discoverFyyd->cachedCategories();
    if (tree.isEmpty()) {
        // Loaded once (cached 24 h); interestsChanged() when it is there
        const_cast<Fyyd *>(m_discoverFyyd)->categories();
    }
    // The 12 categories most listeners in Germany want (146, Bitkom survey
    // "beliebteste Podcast-Themen 2025", mapped to Apple's categories):
    // order of the tiles while nothing has been listened to, and the
    // choice on the first start ("Was interessiert dich?")
    static const char *const Popular[] = {
        "News", "Business", "Music", "History", "Technology", "Health & Fitness",
        "Sports", "Society & Culture", "Science", "Education", "Comedy", "True Crime" };
    QHash<int, QString> topName;
    QVariantList categories;
    for (int i = 0; i < tree.size(); ++i) {
        const FyydCategory &c = tree.at(i);
        if (c.parentId == 0) {
            topName.insert(c.id, c.name);
        }
        // Apple's name as the profile uses it ("Leisure/Automotive")
        const QString name = c.parentId == 0 ? c.name : topName.value(c.parentId) + QLatin1Char('/') + c.name;
        QVariantMap entry;
        entry.insert(QStringLiteral("name"), name);
        entry.insert(QStringLiteral("label"), german && !c.nameDe.isEmpty() ? c.nameDe : c.name);
        entry.insert(QStringLiteral("parent"), c.parentId == 0 ? QString() : topName.value(c.parentId));
        double s = 0;
        double l = 0;
        // The profile may spell it slightly differently ("Non-Profit" / "Non-profit")
        for (QHash<QString, double>::const_iterator it = share.constBegin(); it != share.constEnd(); ++it) {
            if (Fyyd::normalized(it.key()) == Fyyd::normalized(name)) {
                s = it.value();
                l = profile.listened.value(it.key());
            }
        }
        entry.insert(QStringLiteral("share"), strongest > 0 ? qMin(1.0, s / strongest) : 0.0);
        entry.insert(QStringLiteral("listened"), l);
        int level = profile.interests.value(name, 0);
        for (QHash<QString, int>::const_iterator it = profile.interests.constBegin();
             it != profile.interests.constEnd(); ++it) {
            if (Fyyd::normalized(it.key()) == Fyyd::normalized(name)) {
                level = it.value();
            }
        }
        entry.insert(QStringLiteral("level"), level);
        int popular = -1;
        for (int p = 0; c.parentId == 0 && p < int(sizeof(Popular) / sizeof(Popular[0])); ++p) {
            if (Fyyd::normalized(name) == Fyyd::normalized(QString::fromLatin1(Popular[p]))) {
                popular = p;
            }
        }
        entry.insert(QStringLiteral("popular"), popular);
        categories.append(entry);
    }
    result.insert(QStringLiteral("categories"), categories);

    // Keywords
    for (int k = 0; k < 2; ++k) {
        const QList<QStringList> &list = k == 0 ? profile.moreKeywords : profile.neverKeywords;
        QVariantList words;
        for (int i = 0; i < list.size(); ++i) {
            QVariantMap entry;
            entry.insert(QStringLiteral("word"), list.at(i).first());
            entry.insert(QStringLiteral("forms"), QStringList(list.at(i).mid(1)));
            words.append(entry);
        }
        result.insert(k == 0 ? QStringLiteral("more") : QStringLiteral("never"), words);
    }
    // (146) Anything listened to or subscribed - else Discover asks "Was
    // interessiert dich?" until a level or keyword is set
    bool listened = false;
    for (QHash<QString, double>::const_iterator it = profile.listened.constBegin();
         it != profile.listened.constEnd() && !listened; ++it) {
        listened = it.value() > 0;
    }
    result.insert(QStringLiteral("hasListening"), listened);
    result.insert(QStringLiteral("customized"), !profile.interests.isEmpty() || !profile.moreKeywords.isEmpty()
                  || !profile.neverKeywords.isEmpty());
    result.insert(QStringLiteral("fromSubscriptions"),
                  m_database->setting(QStringLiteral("discover.fromSubscriptions"), QStringLiteral("1"))
                  == QLatin1String("1"));
    return result;
}

void AppCore::setInterest(const QString &category, int level)
{
    m_forYou->setInterest(category, ForYou::Interest(qBound(0, level, 3)));
    interestsEdited();
}

void AppCore::setDiscoverLanguages(const QStringList &languages)
{
    m_forYou->setLanguages(languages);
    interestsEdited();
}

QStringList AppCore::translateKeyword(const QString &word) const
{
    return m_forYou->translate(word);
}

void AppCore::setKeyword(int kind, const QString &word, const QStringList &forms, const QString &oldWord)
{
    const ForYou::Interest k = kind == ForYou::Never ? ForYou::Never : ForYou::More;
    if (!oldWord.trimmed().isEmpty()) {
        m_forYou->removeKeyword(k, oldWord);
    }
    if (word.trimmed().isEmpty()) {
        interestsEdited();
        return;
    }
    QStringList clean;
    for (int i = 0; i < forms.size(); ++i) {
        if (!forms.at(i).trimmed().isEmpty()) {
            clean.append(forms.at(i).trimmed());
        }
    }
    // No other forms wanted: the word itself (addKeyword() would translate
    // on its own with an empty list, and drops a form equal to the word)
    m_forYou->addKeyword(k, word, clean.isEmpty() ? QStringList() << word.trimmed() : clean);
    interestsEdited();
}

void AppCore::removeKeyword(int kind, const QString &word)
{
    m_forYou->removeKeyword(kind == ForYou::Never ? ForYou::Never : ForYou::More, word);
    interestsEdited();
}

void AppCore::setRecommendFromSubscriptions(bool on)
{
    m_database->setSetting(QStringLiteral("discover.fromSubscriptions"), on ? QStringLiteral("1") : QStringLiteral("0"));
    interestsEdited();
}

// Hörsuppe's curation "Nullnummern" on fyyd: the first episode of every
// German podcast new on fyyd (149)
static const int NewCurationId = 609;

void AppCore::discoverNew(const QString &language)
{
    m_newProfile.reset();
    if (language == QLatin1String("en")) {
        m_podnews->trailers();
    } else {
        m_newFyyd->curation(NewCurationId);
    }
}

void AppCore::newPodcastInfo(const QString &key)
{
    if (m_newTrailers.contains(key)) {
        m_newIndex->byFeedUrl(key);
        return;
    }
    const int fyydId = key.toInt();
    if (fyydId > 0) {
        m_newInfoFyyd->podcast(fyydId);
    }
}

QVariantMap AppCore::trailerAndFirst(int podcastId, int trailerSeconds) const
{
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::AllEpisodes, true);
    qint64 trailer = -1;
    // 1. marked as trailer in the feed (the newest such one)
    for (int i = 0; i < episodes.size(); ++i) {
        if (episodes.at(i).type == QLatin1String("trailer")) {
            trailer = episodes.at(i).id;
        }
    }
    // 2. "Trailer" in the title, 3. the length Podnews names (a few
    // seconds' leeway), short episodes only
    for (int i = 0; i < episodes.size() && trailer < 0; ++i) {
        if (episodes.at(i).title.contains(QLatin1String("trailer"), Qt::CaseInsensitive)
                && (episodes.at(i).duration <= 0 || episodes.at(i).duration < 15 * 60)) {
            trailer = episodes.at(i).id;
        }
    }
    for (int i = 0; i < episodes.size() && trailer < 0 && trailerSeconds > 0; ++i) {
        const int d = episodes.at(i).duration;
        if (d > 0 && qAbs(d - trailerSeconds) <= 3) {
            trailer = episodes.at(i).id;
        }
    }
    // The first full episode: the oldest one that is neither the trailer
    // nor a bonus
    qint64 first = -1;
    for (int i = 0; i < episodes.size() && first < 0; ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (e.id != trailer && e.type.isEmpty()) {
            first = e.id;
        }
    }
    QVariantMap map;
    map.insert(QStringLiteral("trailer"), int(trailer));
    map.insert(QStringLiteral("first"), int(first));
    return map;
}

QVariantMap AppCore::newPodcastMap(const FyydPodcast &p) const
{
    QVariantMap map;
    if (p.id <= 0) {
        return map;
    }
    map.insert(QStringLiteral("title"), p.title);
    map.insert(QStringLiteral("author"), p.author);
    map.insert(QStringLiteral("url"), p.url);
    // The original cover (151: fyyd's small copies are often not made yet
    // for podcasts new on fyyd - the rows showed only initials)
    map.insert(QStringLiteral("imageUrl"), p.imageUrl);
    map.insert(QStringLiteral("description"), cleanDescription(p.description).left(600));
    map.insert(QStringLiteral("episodes"), p.episodeCount);

    // Its categories as Apple names ("History", "Leisure/Automotive") and
    // labels, from fyyd's tree
    QStringList names;
    QStringList labels;
    categoriesOf(p.categories, &names, &labels);
    // (197) The main category for the row
    map.insert(QStringLiteral("category"), labels.isEmpty() ? QString() : labels.first());
    markNew(map, names, labels, p.title, p.description, p.language);
    return map;
}

// (193) A show of Podnews: its podcast as Podcast Index knows it (result
// may be empty - then Podnews' own data), its categories as Apple names
// from Podcast Index's words ("Society", "Culture") and Podnews' genre
QVariantMap AppCore::newPodcastMap(const PodnewsTrailer &t, const PodcastSearchResult &r) const
{
    const bool known = !r.url.isEmpty();
    QVariantMap map;
    map.insert(QStringLiteral("title"), known && !r.title.isEmpty() ? r.title : t.title);
    map.insert(QStringLiteral("author"), known && !r.author.isEmpty() ? r.author : t.author);
    // The address Podnews names: the one played and subscribed
    map.insert(QStringLiteral("url"), t.feedUrl);
    map.insert(QStringLiteral("imageUrl"), !t.imageUrl.isEmpty() ? t.imageUrl : r.imageUrl);
    map.insert(QStringLiteral("description"),
               cleanDescription(known && !r.description.isEmpty() ? r.description : t.description).left(600));
    map.insert(QStringLiteral("episodes"), r.episodeCount);

    // Podnews' genre first: it is the show's main category
    QStringList words;
    if (!t.genre.isEmpty()) {
        words.append(t.genre);
    }
    words += r.categories;
    QStringList names;
    QStringList labels;
    categoriesOf(words, &names, &labels);
    if (names.isEmpty() && !t.genre.isEmpty()) {
        // fyyd's tree not loaded yet: the genre alone, for "Nie"
        names.append(t.genre);
        labels.append(t.genre);
    }
    map.insert(QStringLiteral("category"), labels.isEmpty() ? QString() : labels.first());
    markNew(map, names, labels, map.value(QStringLiteral("title")).toString(), t.description,
            known ? r.language : QString());
    return map;
}

// "Neu" (149, shared since 193): subscribed, wanted (not hidden, no "Nie"
// category or keyword, no spam) and the interest it fits
void AppCore::markNew(QVariantMap &map, const QStringList &names, const QStringList &labels, const QString &title,
                      const QString &description, const QString &language) const
{
    const QString url = map.value(QStringLiteral("url")).toString();
    const QString author = map.value(QStringLiteral("author")).toString();
    map.insert(QStringLiteral("subscribed"), m_database->findSubscribed(url) >= 0);

    if (!m_newProfile) {
        m_newProfile.reset(new ForYouProfile(m_forYou->profile()));
    }
    const ForYouProfile &profile = *m_newProfile;
    const bool never = neverCategory(names, profile.never);
    // (195) Not in one of the user's languages (Podnews' list mixes them:
    // a Spanish show among the English ones); unknown = kept
    const QString primary = language.trimmed().toLower().section(QLatin1Char('-'), 0, 0).section(QLatin1Char('_'), 0, 0);
    const bool otherLanguage = !primary.isEmpty() && !profile.languages.isEmpty()
            && !profile.languages.contains(primary);
    const bool wanted = !never && !otherLanguage && !ForYou::isSpam(title, language)
            && !m_forYou->isUnwanted(title, description, labels)
            && !m_forYou->isHidden(url, title, author);
    map.insert(QStringLiteral("wanted"), wanted);

    // Fits the interests - specific only, as Für dich (133): a category set
    // to "mehr", a strong sub-category (not "Hobbies"), or a strong one of
    // History, Technology, True Crime, Government (151: "Gesellschaft &
    // Kultur" and "Musik" matched almost every new podcast)
    double strongest = 0;
    for (int i = 0; i < profile.categories.size(); ++i) {
        if (!profile.categories.at(i).first.contains(QLatin1Char('/'))) {
            strongest = qMax(strongest, profile.categories.at(i).second);
        }
    }
    QString match;
    for (int i = 0; i < names.size() && match.isEmpty(); ++i) {
        bool more = false;
        for (QHash<QString, int>::const_iterator it = profile.interests.constBegin();
             it != profile.interests.constEnd(); ++it) {
            if (it.value() == ForYou::More && Fyyd::normalized(it.key()) == Fyyd::normalized(names.at(i))) {
                more = true;
            }
        }
        const QString name = names.at(i);
        const bool strong = strongest > 0 && profile.share(name) >= strongest / 3;
        const bool sub = name.contains(QLatin1Char('/'));
        static const char *const SpecificTops[] = { "History", "Technology", "True Crime", "Government" };
        bool specificTop = false;
        for (size_t t = 0; t < sizeof(SpecificTops) / sizeof(SpecificTops[0]); ++t) {
            specificTop = specificTop || name == QLatin1String(SpecificTops[t]);
        }
        if (more || (strong && ((sub && name != QLatin1String("Leisure/Hobbies")) || specificTop))) {
            match = labels.at(i);
        }
    }
    map.insert(QStringLiteral("match"), match);
}

void AppCore::loadCategoryTree() const
{
    if (m_tree.isEmpty()) {
        m_tree = m_discoverFyyd->cachedCategories();
        m_treeById.clear();
        for (int i = 0; i < m_tree.size(); ++i) {
            m_treeById.insert(m_tree.at(i).id, m_tree.at(i));
        }
    }
}

void AppCore::categoriesOf(const QList<int> &fyydIds, QStringList *names, QStringList *labels) const
{
    loadCategoryTree();
    const QHash<int, FyydCategory> &byId = m_treeById;
    const bool german = QLocale::system().language() == QLocale::German;
    QStringList n;
    QStringList l;
    for (int i = 0; i < fyydIds.size(); ++i) {
        const FyydCategory c = byId.value(fyydIds.at(i));
        if (c.id <= 0) {
            continue;
        }
        const QString name = c.parentId == 0 ? c.name : byId.value(c.parentId).name + QLatin1Char('/') + c.name;
        if (n.contains(name)) {
            continue;
        }
        // (197) A top category first: the main category of the row
        const int at = c.parentId == 0 ? 0 : n.size();
        n.insert(at, name);
        l.insert(at, german && !c.nameDe.isEmpty() ? c.nameDe : c.name);
    }
    *names = n;
    *labels = l;
}

void AppCore::categoriesOf(const QStringList &words, QStringList *names, QStringList *labels) const
{
    QStringList n;
    QStringList l;
    if (!words.isEmpty()) {
        loadCategoryTree();
        const QList<FyydCategory> &tree = m_tree;
        const QHash<int, FyydCategory> &byId = m_treeById;
        const bool german = QLocale::system().language() == QLocale::German;
        // The top category holding the first word is the main one
        int main = -1;
        for (int i = 0; i < tree.size(); ++i) {
            const FyydCategory &c = tree.at(i);
            const QString name = c.parentId == 0 ? c.name : byId.value(c.parentId).name + QLatin1Char('/') + c.name;
            if (n.contains(name) || !ForYou::categoryIn(name, words)) {
                continue;
            }
            const QString label = german && !c.nameDe.isEmpty() ? c.nameDe : c.name;
            if (main < 0 && c.parentId == 0 && ForYou::categoryIn(c.name, QStringList() << words.first())) {
                main = 0;
                n.prepend(name);
                l.prepend(label);
            } else {
                n.append(name);
                l.append(label);
            }
        }
        // Otherwise the first top category found
        if (main < 0) {
            for (int i = 0; i < n.size(); ++i) {
                if (!n.at(i).contains(QLatin1Char('/'))) {
                    n.move(i, 0);
                    l.move(i, 0);
                    break;
                }
            }
        }
    }
    *names = n;
    *labels = l;
}

bool AppCore::neverCategory(const QStringList &names, const QStringList &never)
{
    for (int i = 0; i < names.size(); ++i) {
        const QString name = Fyyd::normalized(names.at(i));
        const QString top = Fyyd::normalized(names.at(i).section(QLatin1Char('/'), 0, 0));
        for (int n = 0; n < never.size(); ++n) {
            const QString no = Fyyd::normalized(never.at(n));
            if (no == name || no == top) {
                return true;
            }
        }
    }
    return false;
}

bool AppCore::trendingRow(QVariantMap &map, const QStringList &names, const QStringList &labels,
                          const QStringList &never) const
{
    if (neverCategory(names, never)) {
        return false;
    }
    map.insert(QStringLiteral("category"), labels.isEmpty() ? QString() : labels.first());
    return true;
}

void AppCore::similarPodcasts(const QString &feedUrl, const QString &title, const QString &author,
                              const QString &website)
{
    m_similarFyyd->similarForFeed(feedUrl, 30, title, author, website);
}

void AppCore::hideSuggestion(const QString &url, const QString &title, const QString &author)
{
    m_forYou->hide(url, title, author);
    // Not counted as a change of the interests: the row goes away on its own
    emit suggestionHidden(url);
}

int AppCore::podcastIdForUrl(const QString &url) const
{
    return int(m_database->findPodcast(url));
}

bool AppCore::isSubscribed(const QString &url) const
{
    const QString normalized = FeedProbe::normalize(url);
    return !normalized.isEmpty() && m_database->findSubscribed(normalized) >= 0;
}

void AppCore::onFeedChecked(const FeedProbeResult &r)
{
    QVariantMap map;
    map.insert(QStringLiteral("requested"), r.requestedUrl);
    map.insert(QStringLiteral("ok"), r.ok());
    map.insert(QStringLiteral("needsLogin"), r.status == FeedProbeResult::LoginRequired);
    map.insert(QStringLiteral("url"), r.url);
    map.insert(QStringLiteral("title"), r.title);
    map.insert(QStringLiteral("author"), r.author);
    map.insert(QStringLiteral("coverUrl"), r.coverUrl);
    map.insert(QStringLiteral("link"), r.link);                 // (157)
    map.insert(QStringLiteral("categories"), r.categories);     // (157)
    {
        // (163) How often it appears, and when next
        const QVariantMap rhythm = rhythmOf(r.dates);
        map.insert(QStringLiteral("rhythm"), rhythm.value(QStringLiteral("rhythm")));
        map.insert(QStringLiteral("next"), rhythm.value(QStringLiteral("next")));
    }
    map.insert(QStringLiteral("episodes"), r.episodes);
    map.insert(QStringLiteral("description"), r.description);
    QVariantList latest;
    for (int i = 0; i < r.latest.size(); ++i) {
        QVariantMap e;
        e.insert(QStringLiteral("title"), r.latest.at(i).title);
        e.insert(QStringLiteral("published"), double(r.latest.at(i).published));
        e.insert(QStringLiteral("duration"), r.latest.at(i).duration);
        e.insert(QStringLiteral("guid"), r.latest.at(i).guid);
        latest.append(e);
    }
    map.insert(QStringLiteral("latest"), latest);
    // A shared episode link (148): the episode of that page, and the time
    // in the link ("#t=1:02:03", "#t=754", "?t=12m30s")
    if (r.hasShared) {
        QVariantMap e;
        e.insert(QStringLiteral("title"), r.shared.title);
        e.insert(QStringLiteral("published"), double(r.shared.published));
        e.insert(QStringLiteral("duration"), r.shared.duration);
        e.insert(QStringLiteral("guid"), r.shared.guid);
        e.insert(QStringLiteral("start"), linkSeconds(r.requestedUrl));
        map.insert(QStringLiteral("shared"), e);
    }
    map.insert(QStringLiteral("discovered"), r.discovered);
    map.insert(QStringLiteral("pageTitle"), r.pageTitle);
    // A web page without a podcast feed (102): "no podcast on this page"
    map.insert(QStringLiteral("noPodcast"), r.status == FeedProbeResult::NotAFeed
               || (r.ok() && r.episodes == 0));
    map.insert(QStringLiteral("subscribed"), !r.url.isEmpty() && m_database->findSubscribed(r.url) >= 0);
    QString error;
    switch (r.status) {
    case FeedProbeResult::Ok:
        break;
    case FeedProbeResult::LoginRequired:
        error = tr("The feed needs a login (HTTP %1) - after subscribing, set it on the podcast page").arg(r.httpStatus);
        break;
    case FeedProbeResult::NotAFeed:
        error = r.discovered ? tr("None of the feeds this page names loads.")
                             : tr("This address is not a podcast feed");
        break;
    case FeedProbeResult::HttpError:
        error = r.httpStatus == 404 || r.httpStatus == 410 ? tr("Nothing found at this address (HTTP %1)").arg(r.httpStatus)
                                                           : tr("The server answered with an error (HTTP %1)").arg(r.httpStatus);
        break;
    case FeedProbeResult::NetworkError:
        error = tr("Not reachable: %1").arg(UrlMask::maskText(r.error));
        break;
    case FeedProbeResult::Timeout:
        error = tr("No answer from the server");
        break;
    case FeedProbeResult::InvalidUrl:
        error = tr("Not a web address");
        break;
    }
    map.insert(QStringLiteral("error"), error);
    emit feedChecked(map);
}

int AppCore::linkSeconds(const QString &url)
{
    QRegularExpression time(QStringLiteral("[#?&]t=([0-9:hms]+)"));
    const QRegularExpressionMatch m = time.match(url);
    if (!m.hasMatch()) {
        return 0;
    }
    const QString value = m.captured(1);
    int seconds = 0;
    if (value.contains(QLatin1Char(':'))) {
        const QStringList parts = value.split(QLatin1Char(':'));
        for (int i = 0; i < parts.size(); ++i) {
            seconds = seconds * 60 + parts.at(i).toInt();
        }
    } else if (value.contains(QRegularExpression(QStringLiteral("[hms]")))) {
        QRegularExpression unit(QStringLiteral("([0-9]+)([hms])"));
        QRegularExpressionMatchIterator it = unit.globalMatch(value);
        while (it.hasNext()) {
            const QRegularExpressionMatch u = it.next();
            const int n = u.captured(1).toInt();
            seconds += u.captured(2) == QLatin1String("h") ? n * 3600 : (u.captured(2) == QLatin1String("m") ? n * 60 : n);
        }
    } else {
        seconds = value.toInt();
    }
    return qMax(0, seconds);
}

void AppCore::setFeedLogin(int podcastId, const QString &user, const QString &password)
{
    const QString name = user.trimmed();
    m_database->setAuthUser(podcastId, name);
    setFeedPassword(podcastId, name.isEmpty() ? QString() : password);
    emit dataChanged();
}

void AppCore::setFeedPassword(int podcastId, const QString &password)
{
    m_updater->setPassword(podcastId, password);
    m_downloads->setPassword(podcastId, password);
}

QVariantList AppCore::feedsWithLogin() const
{
    QVariantList ids;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        if (!podcasts.at(i).authUser.isEmpty()) {
            ids.append(int(podcasts.at(i).id));
        }
    }
    return ids;
}

// --- Sync --------------------------------------------------------------------

void AppCore::sync(const QString &password)
{
    if (m_sync->isRunning()) {
        return;
    }
    m_sync->setPassword(password);
    m_sync->start();
    emit syncingChanged();
}

bool AppCore::setSyncAccount(const QString &backend, const QString &server, const QString &user)
{
    SyncEngine::Backend type;
    QString address = server.trimmed();
    const QString name = user.trimmed();
    if (!SyncEngine::backendFromName(backend, &type) || address.isEmpty() || name.isEmpty()) {
        return false;
    }
    while (address.endsWith(QLatin1Char('/'))) {
        address.chop(1);
    }
    if (!address.startsWith(QLatin1String("https://")) && !address.startsWith(QLatin1String("http://"))) {
        address.prepend(QStringLiteral("https://"));
    }
    if (!QUrl(address).isValid() || QUrl(address).host().isEmpty()) {
        return false;
    }
    // Another account or server: start over (the "since" timestamps and the
    // outbox belong to the old one). The device name only matters for the
    // gpodder.net API; an imported database keeps its own.
    const bool sameAccount = m_database->setting(QStringLiteral("sync.backend")) == SyncEngine::backendName(type)
            && m_database->setting(QStringLiteral("sync.server")) == address
            && m_database->setting(QStringLiteral("sync.user")) == name;
    if (!sameAccount) {
        m_database->resetSyncState();
    }
    m_database->setSetting(QStringLiteral("sync.backend"), SyncEngine::backendName(type));
    m_database->setSetting(QStringLiteral("sync.server"), address);
    m_database->setSetting(QStringLiteral("sync.user"), name);
    if (m_database->setting(QStringLiteral("sync.device")).isEmpty()) {
        m_database->setSetting(QStringLiteral("sync.device"), QStringLiteral("sailcast"));
    }
    emit settingsChanged();
    return true;
}

void AppCore::removeSyncAccount()
{
    if (m_sync->isRunning()) {
        emitMessage(tr("Please wait until the sync is finished"));
        return;
    }
    m_database->resetSyncState();
    m_database->setSetting(QStringLiteral("sync.backend"), QString());
    m_database->setSetting(QStringLiteral("sync.server"), QString());
    m_database->setSetting(QStringLiteral("sync.user"), QString());
    m_database->setSetting(QStringLiteral("sync.device"), QString());
    m_database->setSetting(QStringLiteral("sync.deviceExisting"), QString());
    emit settingsChanged();
}

// --- Episodes ----------------------------------------------------------------

QVariantMap AppCore::episode(int episodeId) const
{
    QVariantMap map;
    const EpisodeRow e = m_database->episode(episodeId);
    if (!e.id) {
        return map;
    }
    map.insert(QStringLiteral("episodeId"), int(e.id));
    map.insert(QStringLiteral("podcastId"), int(e.podcastId));
    map.insert(QStringLiteral("title"), e.title);
    map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
    // (147) Listened to without subscribing: the page offers "Abonnieren"
    map.insert(QStringLiteral("provisional"), m_database->podcast(e.podcastId).provisional);
    map.insert(QStringLiteral("podcastUrl"), m_database->podcast(e.podcastId).url);
    map.insert(QStringLiteral("imageUrl"), e.displayImageUrl);
    map.insert(QStringLiteral("episodeImageUrl"), e.imageUrl);
    map.insert(QStringLiteral("coverUrl"), m_database->podcastCoverUrl(e.podcastId));
    map.insert(QStringLiteral("published"), double(e.published) * 1000.0);
    map.insert(QStringLiteral("duration"), e.duration);
    map.insert(QStringLiteral("position"), e.position);
    map.insert(QStringLiteral("remaining"), e.remaining());
    map.insert(QStringLiteral("played"), e.played);
    // (189) Skipped with "next": where it was left, for playing it again
    map.insert(QStringLiteral("skippedAt"), e.played ? skippedAt(int(e.id)) : 0);
    map.insert(QStringLiteral("isNew"), e.isNew);
    map.insert(QStringLiteral("downloaded"), e.downloaded);
    map.insert(QStringLiteral("kept"), isKept(e));
    map.insert(QStringLiteral("link"), e.link);
    map.insert(QStringLiteral("chapterCount"), e.chapterCount);
    map.insert(QStringLiteral("hasChaptersUrl"), !e.chaptersUrl.isEmpty());
    map.insert(QStringLiteral("season"), e.season);
    map.insert(QStringLiteral("episodeNumber"), e.episodeNumber);
    QString description = m_database->episodeDescription(e.id, true);
    if (description.isEmpty()) {
        description = m_database->episodeDescription(e.id, false);
    }
    map.insert(QStringLiteral("description"), cleanDescription(description));

    QStringList sources;
    const QList<PlaybackSource> list = Archiver::playbackSources(m_database, e.id);
    for (int i = 0; i < list.size(); ++i) {
        sources.append(list.at(i).url.toString(QUrl::FullyEncoded));
    }
    map.insert(QStringLiteral("sources"), sources);

    QVariantList chapters;
    const QList<ParsedChapter> parsed = m_database->chapters(e.id);
    for (int i = 0; i < parsed.size(); ++i) {
        QVariantMap chapter;
        chapter.insert(QStringLiteral("start"), double(parsed.at(i).startMs));
        chapter.insert(QStringLiteral("title"), parsed.at(i).title);
        chapters.append(chapter);
    }
    map.insert(QStringLiteral("chapters"), chapters);
    return map;
}

QVariantMap AppCore::podcast(int podcastId) const
{
    QVariantMap map;
    const PodcastRow p = m_database->podcast(podcastId);
    if (!p.id) {
        return map;
    }
    map.insert(QStringLiteral("podcastId"), int(p.id));
    map.insert(QStringLiteral("title"), p.title);
    map.insert(QStringLiteral("feedTitle"), p.feedTitle);
    map.insert(QStringLiteral("customTitle"), p.customTitle);
    map.insert(QStringLiteral("language"), p.language);
    map.insert(QStringLiteral("categories"), p.categories);
    map.insert(QStringLiteral("author"), p.author);
    map.insert(QStringLiteral("description"), cleanDescription(p.description));
    map.insert(QStringLiteral("coverUrl"), p.coverUrl);
    map.insert(QStringLiteral("link"), p.link);
    map.insert(QStringLiteral("url"), p.url);
    map.insert(QStringLiteral("episodeCount"), p.episodeCount);
    map.insert(QStringLiteral("unplayed"), p.unplayedCount);
    map.insert(QStringLiteral("archived"), p.archived);
    map.insert(QStringLiteral("manualUpdate"), p.manualUpdate);
    map.insert(QStringLiteral("folderId"), int(p.folderId));
    map.insert(QStringLiteral("archiveFolder"), p.archiveFolder.isEmpty()
               ? QString() : QDir(archiveRoot()).absoluteFilePath(p.archiveFolder));
    map.insert(QStringLiteral("lastError"), p.lastError);
    map.insert(QStringLiteral("authUser"), p.authUser);
    map.insert(QStringLiteral("downloadedCount"), p.downloadedCount);
    map.insert(QStringLiteral("provisional"), p.provisional);   // (147)
    map.insert(QStringLiteral("newest"), double(p.newestPublished) * 1000.0);   // (158)
    // podcast:funding (83): [{url, title}]
    map.insert(QStringLiteral("funding"), p.funding.isEmpty()
               ? QVariantList() : QJsonDocument::fromJson(p.funding.toUtf8()).array().toVariantList());
    // For the archive dialog: size as declared in the feed (often missing)
    qint64 bytes = 0;
    int withMedia = 0;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    for (int i = 0; i < episodes.size(); ++i) {
        if (!episodes.at(i).url.isEmpty()) {
            ++withMedia;
            bytes += qMax<qint64>(0, episodes.at(i).fileSize);
        }
    }
    map.insert(QStringLiteral("mediaCount"), withMedia);
    map.insert(QStringLiteral("declaredBytes"), double(bytes));
    return map;
}

void AppCore::markPlayed(int episodeId)
{
    m_database->markPlayed(episodeId);
    archiveStateChanged(episodeId);
    afterPlayed();
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::markUnplayed(int episodeId)
{
    m_database->markUnplayed(episodeId);
    // (189) Skipped before: back where it was left (in progress again)
    const int at = skippedAt(episodeId);
    if (at > 0) {
        const EpisodeRow e = m_database->episode(episodeId);
        m_database->recordPlayback(episodeId, 0, at, e.duration);
        clearSkipped(episodeId);
    }
    archiveStateChanged(episodeId);
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::markAllPlayed(int podcastId)
{
    const int count = m_database->markAllPlayed(podcastId);
    afterPlayed();
    if (!m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

void AppCore::recordPlayback(int episodeId, int started, int position, int total)
{
    const bool wasPlayed = m_database->episode(episodeId).played;
    m_database->recordPlayback(episodeId, started, position, total);
    archiveStateChanged(episodeId);
    emit episodeChanged(episodeId);
    // Played state changed: counts in the lists change too
    const bool played = m_database->episode(episodeId).played;
    if (played && !wasPlayed) {
        afterPlayed();
    }
    if (played != wasPlayed) {
        emit dataChanged();
    }
}

void AppCore::refreshPodcast(int podcastId)
{
    // Full update: the feed may answer 304 although the links in it changed
    m_updater->updatePodcast(podcastId, true);
    emit updatingChanged();
}

void AppCore::loadChapters(int episodeId)
{
    m_chapters->load(QList<qint64>() << episodeId);
}

// --- Downloads ---------------------------------------------------------------

void AppCore::download(int episodeId)
{
    if (m_downloadProgress.contains(episodeId)) {
        return;
    }
    m_cancelled.remove(episodeId);
    m_downloadProgress.insert(episodeId, 0.0);
    emit downloadProgressChanged(episodeId, 0.0);
    // (209) Setting "Downloads in Music": into the podcast's folder there
    // (cleaned up as any download; "Save in Music" keeps a file)
    // (211) Per podcast: "podcast.<id>.inMusic" "1" Music, "0" the app's
    // folder, empty = the global setting
    QString target;
    const EpisodeRow episode = m_database->episode(episodeId);
    if (episode.id && podcastInMusic(episode.podcastId)) {
        target = m_archiver->musicTarget(episodeId);
    }
    m_downloads->download(episodeId, target);
    emit downloadsChanged();
}

void AppCore::cancelDownload(int episodeId)
{
    m_cancelled.insert(episodeId);
    if (!m_downloads->cancel(episodeId)) {
        m_cancelled.remove(episodeId);
        m_downloadProgress.remove(episodeId);
        emit downloadFinished(episodeId, false);
        emit downloadsChanged();
    }
}

void AppCore::cancelAllDownloads()
{
    const QList<qint64> ids = m_downloads->pendingIds();
    for (int i = ids.size() - 1; i >= 0; --i) {
        cancelDownload(int(ids.at(i)));
    }
}

void AppCore::deleteDownload(int episodeId)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (e.downloaded) {
        if (isKept(e)) {
            emitMessage(tr("The file is part of the archive and stays"));
            return;
        }
        QFile::remove(e.downloadPath);
        m_database->clearDownload(episodeId);
    }
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::keepEpisode(int episodeId)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (isKept(e)) {
        emitMessage(tr("Already saved in %1").arg(QFileInfo(e.downloadPath).absolutePath()));
        return;
    }
    // (209) Already in Music ("Downloads in Music"): only marked as kept
    if (e.downloaded && isInside(e.downloadPath, archiveRoot())) {
        m_database->setArchiveFile(e.id, QFileInfo(e.downloadPath).fileName());
        emit episodeChanged(episodeId);
        emit dataChanged();
        emitMessage(tr("Saved in %1").arg(QFileInfo(e.downloadPath).absolutePath()));
        return;
    }
    QString error;
    const QString path = m_archiver->keepEpisode(episodeId, &error);
    if (path.isEmpty()) {
        emitMessage(tr("Cannot save the episode: %1").arg(error));
        return;
    }
    emit episodeChanged(episodeId);
    emit dataChanged();
    emitMessage(tr("Saved in %1").arg(QFileInfo(path).absolutePath()));
}

QVariantMap AppCore::downloadAllInfo(int podcastId) const
{
    int all = 0, allUnknown = 0, unplayed = 0, unplayedUnknown = 0;
    qint64 allBytes = 0, unplayedBytes = 0;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    for (const EpisodeRow &e : episodes) {
        if (e.downloaded || e.url.isEmpty() || m_downloadProgress.contains(int(e.id))) {
            continue;
        }
        ++all;
        allBytes += qMax<qint64>(0, e.fileSize);
        allUnknown += e.fileSize > 0 ? 0 : 1;
        if (!e.played) {
            ++unplayed;
            unplayedBytes += qMax<qint64>(0, e.fileSize);
            unplayedUnknown += e.fileSize > 0 ? 0 : 1;
        }
    }
    QVariantMap map;
    map.insert(QStringLiteral("all"), all);
    map.insert(QStringLiteral("allBytes"), double(allBytes));
    map.insert(QStringLiteral("allUnknown"), allUnknown);
    map.insert(QStringLiteral("unplayed"), unplayed);
    map.insert(QStringLiteral("unplayedBytes"), double(unplayedBytes));
    map.insert(QStringLiteral("unplayedUnknown"), unplayedUnknown);
    return map;
}

int AppCore::downloadAll(int podcastId, bool unplayedOnly)
{
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    int started = 0;
    for (const EpisodeRow &e : episodes) {
        if (e.downloaded || e.url.isEmpty() || (unplayedOnly && e.played)
                || m_downloadProgress.contains(int(e.id))) {
            continue;
        }
        download(int(e.id));
        ++started;
    }
    if (started > 0) {
        emitMessage(tr("%n downloads started", "", started));
    }
    return started;
}

QVariantList AppCore::downloadCandidates(int podcastId) const
{
    const bool oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(podcastId),
                                                 QStringLiteral("0")) == QLatin1String("1");
    const bool bySeason = m_database->seasonCount(podcastId) >= 2;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::AllEpisodes, oldestFirst);
    QVariantList list;
    for (const EpisodeRow &e : episodes) {
        if (e.downloaded || e.url.isEmpty() || m_downloadProgress.contains(int(e.id))) {
            continue;
        }
        QVariantMap map;
        map.insert(QStringLiteral("id"), int(e.id));
        map.insert(QStringLiteral("title"), e.title);
        map.insert(QStringLiteral("published"), double(e.published) * 1000.0);
        map.insert(QStringLiteral("duration"), e.duration);
        map.insert(QStringLiteral("size"), double(qMax<qint64>(0, e.fileSize)));
        map.insert(QStringLiteral("played"), e.played);
        map.insert(QStringLiteral("section"), EpisodeListModel::sectionLabel(e, bySeason));
        list.append(map);
    }
    return list;
}

QVariantList AppCore::unplayedCandidates(int podcastId, bool played) const
{
    const bool oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(podcastId),
                                                 QStringLiteral("0")) == QLatin1String("1");
    const bool bySeason = m_database->seasonCount(podcastId) >= 2;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::AllEpisodes, oldestFirst);
    QVariantList list;
    for (const EpisodeRow &e : episodes) {
        if (e.played != played) {
            continue;
        }
        QVariantMap map;
        map.insert(QStringLiteral("id"), int(e.id));
        map.insert(QStringLiteral("title"), e.title);
        map.insert(QStringLiteral("published"), double(e.published) * 1000.0);
        map.insert(QStringLiteral("duration"), e.duration);
        map.insert(QStringLiteral("inProgress"), e.inProgress());
        map.insert(QStringLiteral("section"), EpisodeListModel::sectionLabel(e, bySeason));
        list.append(map);
    }
    return list;
}

int AppCore::markEpisodesPlayed(const QVariantList &episodeIds)
{
    int marked = 0;
    for (int i = 0; i < episodeIds.size(); ++i) {
        const int id = episodeIds.at(i).toInt();
        const EpisodeRow e = id > 0 ? m_database->episode(id) : EpisodeRow();
        if (!e.id || e.played) {
            continue;
        }
        m_database->markPlayed(e.id);
        archiveStateChanged(id);
        emit episodeChanged(id);
        ++marked;
    }
    if (marked > 0) {
        afterPlayed();
        emit dataChanged();
    }
    emitMessage(tr("%n episodes marked as played", "", marked));
    return marked;
}

int AppCore::markEpisodesUnplayed(const QVariantList &episodeIds)
{
    int marked = 0;
    for (int i = 0; i < episodeIds.size(); ++i) {
        const int id = episodeIds.at(i).toInt();
        const EpisodeRow e = id > 0 ? m_database->episode(id) : EpisodeRow();
        if (!e.id || !e.played) {
            continue;
        }
        m_database->markUnplayed(e.id);
        archiveStateChanged(id);
        emit episodeChanged(id);
        ++marked;
    }
    if (marked > 0) {
        emit dataChanged();
    }
    emitMessage(tr("%n episodes marked as unplayed", "", marked));
    return marked;
}

QList<EpisodeRow> AppCore::sectionEpisodes(int podcastId, const QString &section) const
{
    const bool oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(podcastId),
                                                 QStringLiteral("0")) == QLatin1String("1");
    const bool bySeason = m_database->seasonCount(podcastId) >= 2;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::AllEpisodes, oldestFirst);
    QList<EpisodeRow> result;
    for (const EpisodeRow &e : episodes) {
        if (EpisodeListModel::sectionLabel(e, bySeason) == section) {
            result.append(e);
        }
    }
    return result;
}

int AppCore::downloadSection(int podcastId, const QString &section)
{
    QVariantList ids;
    const QList<EpisodeRow> episodes = sectionEpisodes(podcastId, section);
    for (const EpisodeRow &e : episodes) {
        if (!e.downloaded && !e.url.isEmpty()) {
            ids.append(int(e.id));
        }
    }
    return downloadEpisodes(ids);
}

int AppCore::queueSection(int podcastId, const QString &section)
{
    int added = 0;
    QList<EpisodeRow> episodes = sectionEpisodes(podcastId, section);
    // (165) Always in the order they appeared (oldest first) - a series is
    // heard from its beginning
    std::stable_sort(episodes.begin(), episodes.end(), [](const EpisodeRow &a, const EpisodeRow &b) {
        return a.published < b.published;
    });
    for (const EpisodeRow &e : episodes) {
        if (!e.played && !m_queue.contains(int(e.id))) {
            queueAdd(int(e.id), false);
            ++added;
        }
    }
    if (added > 0) {
        emitMessage(tr("%n episodes added to the playlist", "", added));
    }
    return added;
}

int AppCore::queuePodcast(int podcastId)
{
    int added = 0;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::Unplayed, true);
    for (const EpisodeRow &e : episodes) {
        if (!m_queue.contains(int(e.id))) {
            queueAdd(int(e.id), false);
            ++added;
        }
    }
    emitMessage(added > 0 ? tr("%n episodes added to the playlist", "", added)
                          : tr("All unplayed episodes are in the playlist already"));
    return added;
}

int AppCore::markSectionPlayed(int podcastId, const QString &section)
{
    int marked = 0;
    const QList<EpisodeRow> episodes = sectionEpisodes(podcastId, section);
    for (const EpisodeRow &e : episodes) {
        if (!e.played) {
            m_database->markPlayed(e.id);
            archiveStateChanged(int(e.id));
            emit episodeChanged(int(e.id));
            ++marked;
        }
    }
    if (marked > 0) {
        afterPlayed();
        emit dataChanged();
    }
    return marked;
}

int AppCore::downloadEpisodes(const QVariantList &episodeIds)
{
    int started = 0;
    for (int i = 0; i < episodeIds.size(); ++i) {
        const int id = episodeIds.at(i).toInt();
        if (id > 0 && !m_downloadProgress.contains(id)) {
            download(id);
            ++started;
        }
    }
    if (started > 0) {
        emitMessage(tr("%n downloads started", "", started));
    }
    return started;
}

bool AppCore::podcastInMusic(qint64 podcastId) const
{
    QString inMusic = m_database->setting(QStringLiteral("podcast.%1.inMusic").arg(podcastId));
    if (inMusic.isEmpty()) {
        inMusic = m_database->setting(QStringLiteral("downloads.inMusic"), QStringLiteral("0"));
    }
    return inMusic == QLatin1String("1");
}

QList<EpisodeRow> AppCore::misplacedEpisodes(qint64 podcastId) const
{
    QList<EpisodeRow> result;
    const QString root = archiveRoot();
    const QList<PodcastRow> podcasts = m_database->podcasts();
    QHash<qint64, bool> inMusic;
    for (int i = 0; i < podcasts.size(); ++i) {
        // Archived podcasts keep everything in their archive folder
        if (!podcasts.at(i).archived && podcasts.at(i).id != m_archivingId) {
            inMusic.insert(podcasts.at(i).id, podcastInMusic(podcasts.at(i).id));
        }
    }
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId > 0 ? podcastId : -1, -1,
                                                             Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        const EpisodeRow &e = downloads.at(i);
        if (!inMusic.contains(e.podcastId) || isKept(e) || e.id == m_currentEpisode
                || m_downloads->isActive(e.id) || !QFile::exists(e.downloadPath)) {
            continue;
        }
        if (isInside(e.downloadPath, root) != inMusic.value(e.podcastId)) {
            result.append(e);
        }
    }
    return result;
}

QVariantList AppCore::misplacedDownloads(int podcastId) const
{
    QHash<qint64, QVariantMap> byPodcast;
    QList<qint64> order;
    const QString root = archiveRoot();
    const QList<EpisodeRow> episodes = misplacedEpisodes(podcastId);
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (!byPodcast.contains(e.podcastId)) {
            QVariantMap map;
            map.insert(QStringLiteral("podcastId"), int(e.podcastId));
            map.insert(QStringLiteral("title"), e.podcastTitle);
            map.insert(QStringLiteral("count"), 0);
            map.insert(QStringLiteral("bytes"), 0.0);
            map.insert(QStringLiteral("toMusic"), !isInside(e.downloadPath, root));
            byPodcast.insert(e.podcastId, map);
            order.append(e.podcastId);
        }
        QVariantMap &map = byPodcast[e.podcastId];
        map.insert(QStringLiteral("count"), map.value(QStringLiteral("count")).toInt() + 1);
        map.insert(QStringLiteral("bytes"), map.value(QStringLiteral("bytes")).toDouble()
                                            + double(QFileInfo(e.downloadPath).size()));
    }
    QVariantList list;
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(order.begin(), order.end(), [&](qint64 a, qint64 b) {
        return collator.compare(byPodcast.value(a).value(QStringLiteral("title")).toString(),
                                byPodcast.value(b).value(QStringLiteral("title")).toString()) < 0;
    });
    for (int i = 0; i < order.size(); ++i) {
        list.append(byPodcast.value(order.at(i)));
    }
    return list;
}

int AppCore::moveDownloads(const QVariantList &podcastIds)
{
    const QString root = archiveRoot();
    int moved = 0;
    int failed = 0;
    for (int p = 0; p < podcastIds.size(); ++p) {
        const QList<EpisodeRow> episodes = misplacedEpisodes(podcastIds.at(p).toLongLong());
        for (int i = 0; i < episodes.size(); ++i) {
            const EpisodeRow &e = episodes.at(i);
            const QString target = isInside(e.downloadPath, root) ? m_downloads->defaultTarget(e.id)
                                                                  : m_archiver->musicTarget(e.id);
            if (m_archiver->relocate(e.id, target)) {
                ++moved;
                emit episodeChanged(int(e.id));
            } else {
                ++failed;
            }
        }
    }
    if (moved > 0) {
        emit dataChanged();
        emit downloadsChanged();
    }
    if (failed > 0) {
        emitMessage(tr("%n downloads moved", "", moved) + QStringLiteral(" · ")
                    + tr("%n could not be moved", "", failed));
    } else if (moved > 0) {
        emitMessage(tr("%n downloads moved", "", moved));
    }
    return moved;
}

int AppCore::keepPodcastDownloads(int podcastId)
{
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId);
    int saved = 0;
    QString folder;
    QString error;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (!e.downloaded || isKept(e)) {
            continue;
        }
        if (isInside(e.downloadPath, archiveRoot())) {
            // (209) Already in Music: only marked as kept
            m_database->setArchiveFile(e.id, QFileInfo(e.downloadPath).fileName());
            folder = QFileInfo(e.downloadPath).absolutePath();
            ++saved;
            emit episodeChanged(int(e.id));
            continue;
        }
        const QString path = m_archiver->keepEpisode(e.id, &error);
        if (path.isEmpty()) {
            break;
        }
        folder = QFileInfo(path).absolutePath();
        ++saved;
        emit episodeChanged(int(e.id));
    }
    if (saved > 0) {
        emit dataChanged();
        emitMessage(tr("%n episodes saved in %1", "", saved).arg(folder));
    } else if (!error.isEmpty()) {
        emitMessage(tr("Cannot save the episode: %1").arg(error));
    } else {
        emitMessage(tr("All downloads are already in Music"));
    }
    return saved;
}

int AppCore::queueDownloaded()
{
    QList<EpisodeRow> episodes = m_database->episodes(-1, -1, Database::Downloaded);
    std::sort(episodes.begin(), episodes.end(), [](const EpisodeRow &a, const EpisodeRow &b) {
        return a.published < b.published;
    });
    int added = 0;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (e.played || m_queue.contains(int(e.id)) || e.id == m_currentEpisode) {
            continue;
        }
        m_database->queueAdd(e.id, false);
        m_queue.insert(int(e.id));
        emit episodeChanged(int(e.id));
        ++added;
    }
    if (added > 0) {
        emit queueChanged();
    }
    emitMessage(added > 0 ? tr("%n episodes added to the playlist", "", added)
                          : tr("No unplayed downloads outside the playlist"));
    return added;
}

QString AppCore::downloadPath(int episodeId) const
{
    const EpisodeRow e = m_database->episode(episodeId);
    return e.downloaded && QFile::exists(e.downloadPath) ? e.downloadPath : QString();
}

QString AppCore::mimeType(int episodeId) const
{
    const QString type = m_database->episode(episodeId).mimeType;
    return type.isEmpty() ? QStringLiteral("audio/mpeg") : type;
}

bool AppCore::isDownloading(int episodeId) const
{
    return m_downloadProgress.contains(episodeId) || m_downloads->pendingIds().contains(episodeId);
}

qreal AppCore::downloadProgress(int episodeId) const
{
    return m_downloadProgress.value(episodeId, 0.0);
}

QVariantList AppCore::activeDownloads() const
{
    QVariantList list;
    const QList<qint64> ids = m_downloads->pendingIds();
    for (int i = 0; i < ids.size(); ++i) {
        const EpisodeRow e = m_database->episode(ids.at(i));
        if (!e.id) {
            continue;
        }
        QVariantMap map;
        map.insert(QStringLiteral("episodeId"), int(e.id));
        map.insert(QStringLiteral("title"), e.title);
        map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
        map.insert(QStringLiteral("imageUrl"), e.displayImageUrl);
        map.insert(QStringLiteral("progress"), m_downloadProgress.value(int(e.id), 0.0));
        map.insert(QStringLiteral("running"), m_downloads->isActive(e.id));
        list.append(map);
    }
    return list;
}

int AppCore::downloadedCount() const
{
    return m_database->downloadedCount();
}

QVariantMap AppCore::downloadStats() const
{
    int count = 0;
    qint64 bytes = 0;
    int played = 0;
    qint64 playedBytes = 0;
    // (217) Of them in the Music folder (download location or kept)
    int music = 0;
    qint64 musicBytes = 0;
    const QString root = archiveRoot();
    const QList<EpisodeRow> downloads = m_database->episodes(-1, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        const QFileInfo info(downloads.at(i).downloadPath);
        if (info.exists()) {
            ++count;
            bytes += info.size();
            if (isInside(downloads.at(i).downloadPath, root)) {
                ++music;
                musicBytes += info.size();
            }
            // (190) Played ones: what "Delete played downloads" would free
            if (downloads.at(i).played) {
                ++played;
                playedBytes += info.size();
            }
        }
    }
    QVariantMap map;
    map.insert(QStringLiteral("count"), count);
    map.insert(QStringLiteral("bytes"), double(bytes));
    map.insert(QStringLiteral("played"), played);
    map.insert(QStringLiteral("playedBytes"), double(playedBytes));
    map.insert(QStringLiteral("music"), music);
    map.insert(QStringLiteral("musicBytes"), double(musicBytes));
    // (190) Free space where the downloads go
    QStorageInfo storage(m_downloads->downloadDirectory());
    map.insert(QStringLiteral("free"), storage.isValid() ? double(storage.bytesAvailable()) : -1.0);
    return map;
}

QVariantMap AppCore::queueStats() const
{
    // (190) The playlist page tiles: episodes, time left, downloaded
    const QList<EpisodeRow> rows = m_database->episodes(-1, -1, Database::Queue);
    int count = 0;
    qint64 seconds = 0;
    int downloaded = 0;
    for (const EpisodeRow &e : rows) {
        ++count;
        seconds += qMax(0, e.remaining());
        if (e.downloaded) {
            ++downloaded;
        }
    }
    QVariantMap map;
    map.insert(QStringLiteral("count"), count);
    map.insert(QStringLiteral("seconds"), double(seconds));
    map.insert(QStringLiteral("downloaded"), downloaded);
    return map;
}

QHash<qint64, int> AppCore::podcastCleanupDays() const
{
    QHash<qint64, int> result;
    const QHash<qint64, QString> values = m_database->podcastSettings(QStringLiteral("cleanupDays"));
    for (QHash<qint64, QString>::const_iterator it = values.constBegin(); it != values.constEnd(); ++it) {
        bool ok = false;
        const int days = it.value().toInt(&ok);
        if (ok) {
            result.insert(it.key(), days < 0 ? -1 : days);
        }
    }
    return result;
}

int AppCore::cleanupDownloads(int days)
{
    // days >= 0: asked for by the user ("Delete played downloads" = 0) -
    // every podcast except those set to "never" (117); -1: the settings
    const bool all = days >= 0;
    if (!all) {
        bool ok = false;
        days = m_database->setting(QStringLiteral("cleanup.days"), QStringLiteral("7")).toInt(&ok);
        if (!ok) {
            days = -1;
        }
    }
    const CleanupResult r = m_downloads->cleanup(days, archiveRoot(), 0, podcastCleanupDays(), all);
    emit dataChanged();
    emitMessage(tr("%n downloads deleted", "", r.files));
    return r.files;
}

void AppCore::afterPlayed()
{
    // Played episodes leave the playlist
    const QList<qint64> queue = m_database->queueIds();
    bool queueChangedHere = false;
    for (int i = 0; i < queue.size(); ++i) {
        if (m_database->episode(queue.at(i)).played) {
            m_database->queueRemove(queue.at(i));
            queueChangedHere = true;
        }
    }
    if (queueChangedHere) {
        reloadQueue();
        emit queueChanged();
    }
    cleanupRightAfterListening();
}

void AppCore::setCurrentEpisode(int episodeId)
{
    const int previous = m_currentEpisode;
    m_currentEpisode = episodeId;
    // (189) Played again: the skip position has been used (or not wanted)
    if (episodeId > 0 && episodeId != previous) {
        clearSkipped(episodeId);
    }
    // (186) The episode playing is the first of the playlist: what the list
    // shows below "Now playing" is exactly what comes next
    if (episodeId > 0 && m_queue.contains(episodeId)) {
        const QList<qint64> ids = m_database->queueIds();
        if (!ids.isEmpty() && ids.first() != episodeId) {
            m_database->queueMove(episodeId, 0);
            emit queueChanged();
        }
    }
    // (183) An episode heard to the end keeps its file while it is in the
    // player; when the next one is loaded, "right after listening" deletes
    // it now (with the message) instead of at the next start
    if (previous > 0 && previous != episodeId) {
        const EpisodeRow e = m_database->episode(previous);
        if (e.id && e.played && e.downloaded) {
            cleanupRightAfterListening();
        }
    }
}

void AppCore::noteSkipped(int episodeId, int seconds)
{
    if (episodeId > 0 && seconds > 0) {
        m_database->setSetting(QStringLiteral("episode.%1.skippedAt").arg(episodeId), QString::number(seconds));
    }
}

int AppCore::skippedAt(int episodeId) const
{
    return m_database->setting(QStringLiteral("episode.%1.skippedAt").arg(episodeId)).toInt();
}

void AppCore::clearSkipped(int episodeId)
{
    m_database->removeSettings(QStringLiteral("episode.%1.skippedAt").arg(episodeId));
}

void AppCore::noteHeard(int episodeId)
{
    if (episodeId <= 0) {
        return;
    }
    const QString key = QStringLiteral("player.recent");
    QStringList ids = m_database->setting(key).split(QLatin1Char(','), QString::SkipEmptyParts);
    const QString id = QString::number(episodeId);
    ids.removeAll(id);
    ids.prepend(id);
    while (ids.size() > 30) {
        // (189) Its skip position goes with it
        clearSkipped(ids.last().toInt());
        ids.removeLast();
    }
    m_database->setSetting(key, ids.join(QLatin1Char(',')));
    emit dataChanged();
}

void AppCore::cleanupRightAfterListening()
{
    // "Right after listening": delete their downloads now, not only at the
    // next start - never the file in the player
    // (also when only single podcasts are set to that, 117)
    const QHash<qint64, int> own = podcastCleanupDays();
    bool ok = false;
    int days = m_database->setting(QStringLiteral("cleanup.days"), QStringLiteral("7")).toInt(&ok);
    if (!ok) {
        days = -1;
    }
    if (days == 0 || own.values().contains(0)) {
        const CleanupResult r = m_downloads->cleanup(days, archiveRoot(), m_currentEpisode, own);
        if (r.files > 0) {
            emit dataChanged();
            // (183) Say so - marking as played can delete downloads
            emitMessage(tr("%n played downloads deleted", "", r.files));
        }
    }
}

void AppCore::cleanupOnStart()
{
    // "cleanup.days": -1 = never, 0 = right after listening, N = days
    bool ok = false;
    int days = m_database->setting(QStringLiteral("cleanup.days"), QStringLiteral("7")).toInt(&ok);
    if (!ok) {
        days = -1;
    }
    // Also with "never" globally: podcasts may have their own choice (117)
    m_downloads->cleanup(days, archiveRoot(), 0, podcastCleanupDays());
    // (209) Half downloads of failed attempts never tried again
    m_downloads->removeStaleParts(archiveRoot(), 7);
    // Listened to without subscribing (147): gone after 30 days without use
    // (not the player's episode, not with an episode in the playlist)
    const qint64 last = m_database->setting(QStringLiteral("player.last"), QStringLiteral("0")).toLongLong();
    const QList<qint64> stale = m_database->staleProvisional(30, last);
    for (int i = 0; i < stale.size(); ++i) {
        removeProvisional(stale.at(i));
    }
}

bool AppCore::onWifi() const
{
#if QT_VERSION < QT_VERSION_CHECK(5, 15, 0)
    // Sailfish OS (Qt 5.6): the active connections of the bearer plugin
    const QList<QNetworkConfiguration> active
            = m_networkConfig->allConfigurations(QNetworkConfiguration::Active);
    for (int i = 0; i < active.size(); ++i) {
        const QNetworkConfiguration::BearerType type = active.at(i).bearerType();
        if (type == QNetworkConfiguration::BearerWLAN || type == QNetworkConfiguration::BearerEthernet) {
            return true;
        }
    }
    return false;
#else
    // Bearer management is gone from Qt 5.15 on (desktop only)
    return true;
#endif
}

bool AppCore::downloadAllowed() const
{
    return m_database->setting(QStringLiteral("downloads.wifiOnly"), QStringLiteral("0")) != QLatin1String("1")
            || onWifi();
}

void AppCore::autoDownload(qint64 podcastId, int newEpisodes)
{
    // Per podcast (112): "1" always, "0" never, empty = the global setting
    QString autoSetting = m_database->setting(QStringLiteral("podcast.%1.autoDownload").arg(podcastId));
    if (autoSetting.isEmpty()) {
        autoSetting = m_database->setting(QStringLiteral("downloads.auto"), QStringLiteral("0"));
    }
    if (autoSetting != QLatin1String("1") || !downloadAllowed()) {
        return;
    }
    const PodcastRow podcast = m_database->podcast(podcastId);
    // The first update of a subscription brings the whole back catalog -
    // nothing is loaded automatically then
    // Being archived: the archive loads the new episodes into its folder
    // Not subscribed (147): nothing is loaded by itself
    if (!podcast.id || podcast.archived || podcast.provisional || newEpisodes >= podcast.episodeCount
            || podcast.id == m_archivingId) {
        return;
    }
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, qMin(newEpisodes, AutoDownloadLimit),
                                                            Database::NewEpisodes);
    for (int i = 0; i < episodes.size(); ++i) {
        if (!episodes.at(i).downloaded && !episodes.at(i).url.isEmpty()) {
            download(int(episodes.at(i).id));
        }
    }
}

// --- Playlist ----------------------------------------------------------------

void AppCore::queueAdd(int episodeId, bool next)
{
    m_database->queueAdd(episodeId, next);
    // (183) "Play next" = right after the episode playing now when that one
    // is in the playlist: since 169 the playlist continues after the
    // current episode's place, so the front would only come after the end
    if (next && m_currentEpisode > 0 && m_currentEpisode != episodeId) {
        QList<qint64> ids = m_database->queueIds();
        ids.removeAll(episodeId);
        const int current = ids.indexOf(m_currentEpisode);
        if (current >= 0) {
            m_database->queueMove(episodeId, current + 1);
        }
    }
    m_queue.insert(episodeId);
    emit queueChanged();
    emit episodeChanged(episodeId);
}

void AppCore::queueRemove(int episodeId)
{
    m_database->queueRemove(episodeId);
    m_queue.remove(episodeId);
    emit queueChanged();
    emit episodeChanged(episodeId);
}

void AppCore::queueMove(int episodeId, int newIndex)
{
    m_database->queueMove(episodeId, newIndex);
    emit queueChanged();
}

void AppCore::queueSetOrder(const QVariantList &episodeIds)
{
    QList<qint64> ids;
    // Queued episodes that are not in the list (the current one) first
    const QList<qint64> current = m_database->queueIds();
    QSet<qint64> shown;
    for (int i = 0; i < episodeIds.size(); ++i) {
        shown.insert(episodeIds.at(i).toLongLong());
    }
    for (int i = 0; i < current.size(); ++i) {
        if (!shown.contains(current.at(i))) {
            ids.append(current.at(i));
        }
    }
    for (int i = 0; i < episodeIds.size(); ++i) {
        ids.append(episodeIds.at(i).toLongLong());
    }
    m_database->queueSetOrder(ids);
    emit queueChanged();
}

void AppCore::queueClear()
{
    m_database->queueClear();
    reloadQueue();
    emit queueChanged();
    emit dataChanged();
}

bool AppCore::isQueued(int episodeId) const
{
    return m_queue.contains(episodeId);
}

// (169) The episode after the finished one in the playlist - not simply the
// first: an episode left unfinished above it (skipped with "next") would
// come again. At the end of the list the first remaining one.
int AppCore::queueNext(int finishedEpisodeId)
{
    const int index = m_database->queueIds().indexOf(finishedEpisodeId);
    if (m_queue.contains(finishedEpisodeId)) {
        queueRemove(finishedEpisodeId);
    }
    const QList<qint64> ids = m_database->queueIds();
    if (ids.isEmpty()) {
        return -1;
    }
    return index >= 0 && index < ids.size() ? int(ids.at(index)) : int(ids.first());
}

int AppCore::nextEpisode(int currentEpisodeId) const
{
    const QList<qint64> queue = m_database->queueIds();
    // (169) The one after the current episode, then from the start
    const int index = queue.indexOf(currentEpisodeId);
    if (index >= 0 && index + 1 < queue.size()) {
        return int(queue.at(index + 1));
    }
    for (int i = 0; i < queue.size(); ++i) {
        if (queue.at(i) != currentEpisodeId) {
            return int(queue.at(i));
        }
    }
    const QList<EpisodeRow> started = m_database->episodes(-1, 2, Database::InProgress);
    for (int i = 0; i < started.size(); ++i) {
        if (started.at(i).id != currentEpisodeId) {
            return int(started.at(i).id);
        }
    }
    return -1;
}

QVariantList AppCore::upNext(int currentEpisodeId, int count) const
{
    QVariantList list;
    const QList<qint64> queue = m_database->queueIds();
    // (169) In the order they will play: after the current one, then from
    // the start (as nextEpisode / queueNext)
    const int start = queue.indexOf(currentEpisodeId) + 1;
    for (int n = 0; n < queue.size() && list.size() < count; ++n) {
        const int i = (start + n) % queue.size();
        if (queue.at(i) != currentEpisodeId) {
            const QVariantMap map = episode(int(queue.at(i)));
            if (!map.isEmpty()) {
                list.append(map);
            }
        }
    }
    return list;
}

int AppCore::inProgressCount(int excludeEpisodeId) const
{
    int count = m_database->stats().inProgress;
    if (excludeEpisodeId > 0 && m_database->episode(excludeEpisodeId).inProgress()) {
        --count;
    }
    return qMax(0, count);
}

// --- Archive -----------------------------------------------------------------

void AppCore::archivePodcast(int podcastId)
{
    if (m_archiver->isRunning()) {
        emitMessage(tr("An archive is already running"));
        return;
    }
    if (!QDir().mkpath(archiveRoot())) {
        emitMessage(tr("Cannot create the archive folder %1").arg(archiveRoot()));
        return;
    }
    m_archiver->setArchiveRoot(archiveRoot());
    m_archivingId = podcastId;
    m_archiveCancelled = false;
    emit archivingChanged();
    m_archiver->archive(podcastId);
}

void AppCore::cancelArchive()
{
    if (!m_archivingId) {
        return;
    }
    m_archiveCancelled = true;
    const QList<qint64> pending = m_downloads->pendingIds();
    for (int i = 0; i < pending.size(); ++i) {
        if (m_database->episode(pending.at(i)).podcastId == m_archivingId) {
            cancelDownload(int(pending.at(i)));
        }
    }
}

void AppCore::onArchiveFinished(const ArchiveResult &result)
{
    const bool cancelled = m_archiveCancelled;
    m_archivingId = 0;
    m_archiveCancelled = false;
    emit archivingChanged();
    emit dataChanged();
    emit downloadsChanged();
    const int inArchive = result.present + result.moved + result.downloaded;
    if (cancelled) {
        emitMessage(tr("Archiving cancelled: %n episodes in the archive", "", inArchive));
    } else if (!result.failures.isEmpty()) {
        emitMessage(tr("Archived: %n episodes, %1 failed", "", inArchive).arg(result.failures.size()));
    } else {
        emitMessage(tr("Archived: %n episodes", "", inArchive));
    }
}

void AppCore::setManualUpdate(int podcastId, bool manual)
{
    m_database->setManualUpdate(podcastId, manual);
    emit dataChanged();
}

void AppCore::setPodcastTitle(int podcastId, const QString &title)
{
    QString name = title.simplified();
    // The feed's own title typed again is no own name
    if (name == m_database->podcast(podcastId).feedTitle.simplified()) {
        name.clear();
    }
    if (name == m_database->podcast(podcastId).customTitle) {
        return;
    }
    m_database->setCustomTitle(podcastId, name);
    emit dataChanged();
}

void AppCore::setArchived(int podcastId, bool archived)
{
    m_database->setArchived(podcastId, archived);
    if (!m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
}

QString AppCore::archiveDirectory() const
{
    return archiveRoot();
}

QString AppCore::defaultArchiveDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::MusicLocation) + QStringLiteral("/Podcasts");
}

bool AppCore::setArchiveDirectory(const QString &path)
{
    QString folder = path.trimmed();
    if (folder.startsWith(QLatin1String("~/"))) {
        folder = QDir::homePath() + folder.mid(1);
    }
    while (folder.length() > 1 && folder.endsWith(QLatin1Char('/'))) {
        folder.chop(1);
    }
    if (folder == defaultArchiveDirectory()) {
        folder.clear();
    }
    if (!folder.isEmpty()) {
        const QFileInfo info(folder);
        if (!info.isAbsolute() || !QDir().mkpath(folder) || !QFileInfo(folder).isWritable()) {
            emitMessage(tr("Cannot write to %1").arg(folder));
            return false;
        }
    }
    m_database->setSetting(QStringLiteral("archive.dir"), folder);
    m_archiver->setArchiveRoot(archiveRoot());
    emit settingsChanged();
    return true;
}

QVariantList AppCore::archiveFolders() const
{
    QVariantList list;
    const QFileInfoList dirs = QDir(archiveRoot()).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot,
                                                                 QDir::Name | QDir::IgnoreCase);
    for (int i = 0; i < dirs.size(); ++i) {
        QFile file(dirs.at(i).absoluteFilePath() + QStringLiteral("/episodes.json"));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QJsonObject root = QJsonDocument::fromJson(file.readAll()).object();
        const QJsonObject podcast = root.value(QStringLiteral("podcast")).toObject();
        const QString url = podcast.value(QStringLiteral("url")).toString();
        if (url.isEmpty()) {
            continue;
        }
        QVariantMap map;
        map.insert(QStringLiteral("path"), dirs.at(i).absoluteFilePath());
        map.insert(QStringLiteral("title"), podcast.value(QStringLiteral("title")).toString());
        map.insert(QStringLiteral("episodes"), root.value(QStringLiteral("episodes")).toArray().size());
        map.insert(QStringLiteral("subscribed"), m_database->findSubscribed(url) >= 0);
        list.append(map);
    }
    return list;
}

void AppCore::importArchiveFolder(const QString &path)
{
    const ArchiveImportResult r = m_archiver->importFolder(path);
    if (!r.ok) {
        emitMessage(tr("Import failed: %1").arg(r.error));
        return;
    }
    emit dataChanged();
    emitMessage(tr("Archive imported: %n episodes, %1 files", "", r.episodesAdded).arg(r.filesFound));
}

void AppCore::archiveStateChanged(int episodeId)
{
    const qint64 podcastId = m_database->episode(episodeId).podcastId;
    if (podcastId && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
}

void AppCore::writeArchiveMetadata()
{
    m_metadataTimer->stop();
    const QList<qint64> ids = m_metadataDirty.values();
    m_metadataDirty.clear();
    for (int i = 0; i < ids.size(); ++i) {
        // Not while the archive itself writes it
        if (m_archiver->isRunning() && ids.at(i) == m_archivingId) {
            continue;
        }
        QString error;
        if (!m_archiver->writeMetadata(ids.at(i), &error)) {
            qWarning("[AppCore] episodes.json: %s", qPrintable(error));
        }
    }
}

// --- Settings ----------------------------------------------------------------

QString AppCore::setting(const QString &key, const QString &defaultValue) const
{
    return m_database->setting(key, defaultValue);
}

void AppCore::setSetting(const QString &key, const QString &value)
{
    m_database->setSetting(key, value);
    emit settingsChanged();
}

// --- Import and export -------------------------------------------------------

void AppCore::importOpml(const QString &path, int folderId, bool markOlder)
{
    QString error;
    const QList<OpmlEntry> entries = Opml::read(path, &error);
    if (entries.isEmpty()) {
        emitMessage(error.isEmpty() ? tr("No feeds in %1").arg(QFileInfo(path).fileName())
                                     : tr("Import failed: %1").arg(error));
        return;
    }
    int added = 0;
    QStringList waiting = m_database->setting(QStringLiteral("import.markOlder")).split(QLatin1Char(','), QString::SkipEmptyParts);
    for (int i = 0; i < entries.size(); ++i) {
        qint64 id = m_database->findSubscribed(entries.at(i).url);
        const bool existed = id >= 0;
        if (!existed) {
            id = m_database->addPodcast(entries.at(i).url, entries.at(i).title);
            if (id >= 0) {
                ++added;
                if (markOlder) {
                    waiting.append(QString::number(id));
                }
            }
        }
        // Folders from the file (45): new podcasts, and existing ones that
        // are in no folder yet
        if (id >= 0 && !entries.at(i).folder.isEmpty()
                && (!existed || m_database->podcast(id).folderId == 0)) {
            m_database->setPodcastFolder(id, m_database->createFolder(entries.at(i).folder));
        } else if (id >= 0 && !existed && folderId > 0) {
            // No folder in the file: the folder chosen on the import page
            m_database->setPodcastFolder(id, folderId);
        }
    }
    m_database->setSetting(QStringLiteral("import.markOlder"), waiting.join(QLatin1Char(',')));
    emit dataChanged();
    emitMessage(added > 0 ? tr("%n podcasts added - loading the episodes", "", added)
                           : tr("All podcasts of the file are already subscribed"));
    if (added > 0) {
        updateAll();
    }
}

void AppCore::markImportedOlder()
{
    const QString key = QStringLiteral("import.markOlder");
    const QStringList waiting = m_database->setting(key).split(QLatin1Char(','), QString::SkipEmptyParts);
    if (waiting.isEmpty()) {
        return;
    }
    QStringList still;
    int marked = 0;
    for (const QString &value : waiting) {
        const qint64 id = value.toLongLong();
        const PodcastRow p = m_database->podcast(id);
        if (!p.id) {
            continue;                       // unsubscribed meanwhile
        }
        if (p.episodeCount == 0) {
            still.append(value);            // feed not loaded yet
            continue;
        }
        marked += m_database->markAllButNewestPlayed(id);
    }
    m_database->setSetting(key, still.join(QLatin1Char(',')));
    if (marked > 0) {
        emitMessage(tr("%n older episodes marked as played", "", marked));
    }
}

void AppCore::importGpodder(const QString &path)
{
    if (m_updater->isBusy() || m_sync->isRunning()) {
        emitMessage(tr("Please wait until updates and the sync are finished"));
        return;
    }
    const GpodderImportResult r = GpodderImport::run(m_database, path);
    if (!r.ok) {
        emitMessage(tr("Import failed: %1").arg(r.error));
        return;
    }
    reloadQueue();
    emit dataChanged();
    emitMessage(tr("gPodder: %n podcasts added, %1 episodes, %2 played, %3 in progress", "", r.podcastsAdded)
                 .arg(r.episodesAdded + r.episodesMerged).arg(r.played).arg(r.inProgress));
    updateAll();
}

void AppCore::importDatabase(const QString &path, bool keepFolders)
{
    if (m_updater->isBusy() || m_sync->isRunning() || m_downloads->isBusy() || m_archiver->isRunning()) {
        emitMessage(tr("Please wait until updates and downloads are finished"));
        return;
    }
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(databasePath()).absoluteFilePath()) {
        return;
    }
    writeArchiveMetadata();
    const QString target = databasePath();
    m_database->close();
    QFile::remove(target);
    QFile::remove(target + QStringLiteral("-wal"));
    QFile::remove(target + QStringLiteral("-shm"));
    const bool copied = QFile::copy(path, target);
    if (copied) {
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    }
    openDatabase();
    // Folders of the other device (e.g. /tmp on the desktop) do not exist
    // here: back to the defaults - not for a backup of this phone (93)
    if (!keepFolders) {
        m_database->setSetting(QStringLiteral("archive.dir"), QString());
        m_database->setSetting(QStringLiteral("downloads.dir"), QString());
    }
    m_archiver->setArchiveRoot(archiveRoot());
    reloadQueue();
    emit settingsChanged();
    emit dataChanged();
    emit queueChanged();
    emitMessage(copied && m_databaseError.isEmpty()
                 ? tr("Database taken over: %n podcasts", "", m_database->stats().podcasts)
                 : tr("Import failed: %1").arg(m_databaseError.isEmpty() ? path : m_databaseError));
}

QString AppCore::exportOpml(const QVariantList &folderIds)
{
    QList<OpmlEntry> entries;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    // Folders (45): written as enclosing outlines, in the folder order
    QSet<int> wanted;
    for (int i = 0; i < folderIds.size(); ++i) {
        wanted.insert(folderIds.at(i).toInt());
    }
    const QList<FolderRow> folders = m_database->folders();
    QHash<qint64, QString> folderNames;
    for (int f = 0; f < folders.size(); ++f) {
        folderNames.insert(folders.at(f).id, folders.at(f).name);
    }
    for (int f = -1; f < folders.size(); ++f) {
        const qint64 folderId = f < 0 ? 0 : folders.at(f).id;
        for (int i = 0; i < podcasts.size(); ++i) {
            if (podcasts.at(i).folderId != folderId
                    && !(f < 0 && !folderNames.contains(podcasts.at(i).folderId))) {
                continue;
            }
            OpmlEntry entry;
            entry.title = podcasts.at(i).title;
            entry.url = podcasts.at(i).url;
            entry.htmlUrl = podcasts.at(i).link;
            entry.folder = folderNames.value(podcasts.at(i).folderId);
            // Only the chosen folders (45); a podcast in an unknown folder
            // counts as "Other podcasts" (0)
            const int key = folderNames.contains(podcasts.at(i).folderId) ? int(podcasts.at(i).folderId) : 0;
            if (!wanted.isEmpty() && !wanted.contains(key)) {
                continue;
            }
            entries.append(entry);
        }
    }
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + QStringLiteral("/sailcast-subscriptions-%1.opml").arg(today());
    QString error;
    if (!Opml::write(path, entries, &error)) {
        emitMessage(tr("Export failed: %1").arg(error));
        return QString();
    }
    return path;
}

QString AppCore::exportDatabase()
{
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + QStringLiteral("/sailcast-%1.db").arg(today());
    if (!copyDatabaseTo(path)) {
        emitMessage(tr("Export failed: %1").arg(path));
        return QString();
    }
    return path;
}

bool AppCore::copyDatabaseTo(const QString &path)
{
    // Everything from the write-ahead log into the database file first
    QSqlQuery q(m_database->handle());
    q.exec(QStringLiteral("PRAGMA wal_checkpoint(TRUNCATE)"));
    const QString part = path + QStringLiteral(".part");
    QFile::remove(part);
    if (!QFile::copy(databasePath(), part)) {
        QFile::remove(part);
        return false;
    }
    QFile::remove(path);
    return QFile::rename(part, path);
}

QString AppCore::backupDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + QStringLiteral("/Sailcast-Backups");
}

double AppCore::lastBackup() const
{
    return m_database->setting(QStringLiteral("backup.last"), QStringLiteral("0")).toDouble() * 1000.0;
}

double AppCore::lastNextcloudBackup() const
{
    return m_database->setting(QStringLiteral("backup.lastNextcloud"), QStringLiteral("0")).toDouble() * 1000.0;
}

bool AppCore::nextcloudBackupPossible() const
{
    return syncConfigured()
            && m_database->setting(QStringLiteral("sync.backend")) == QLatin1String("nextcloud")
            && m_database->setting(QStringLiteral("backup.nextcloud"), QStringLiteral("1")) == QLatin1String("1");
}

QString AppCore::makeBackup()
{
    const QString dir = backupDirectory();
    if (!QDir().mkpath(dir)) {
        emitMessage(tr("Backup failed: %1").arg(dir));
        return QString();
    }
    const QString path = dir + QStringLiteral("/sailcast-%1.db").arg(today());
    if (!copyDatabaseTo(path)) {
        emitMessage(tr("Backup failed: %1").arg(path));
        return QString();
    }
    // The newest 5 stay (names sort by date)
    QStringList files = QDir(dir).entryList(QStringList() << QStringLiteral("sailcast-*.db"),
                                            QDir::Files, QDir::Name);
    while (files.size() > 5) {
        QFile::remove(dir + QLatin1Char('/') + files.takeFirst());
    }
    m_database->setSetting(QStringLiteral("backup.last"),
                           QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
    emit settingsChanged();
    return path;
}

void AppCore::uploadBackup(const QString &path, const QString &password, bool manual)
{
    if (path.isEmpty() || !nextcloudBackupPossible()) {
        return;
    }
    if (m_webdav->isRunning()) {
        if (manual) {
            emitMessage(tr("A Nextcloud backup is already running"));
        }
        return;
    }
    // A password from the settings is kept for the session (97): without a
    // sync since the start (no update on start) the core had none, and the
    // automatic Nextcloud copy waited for one
    if (!password.isEmpty() && m_sync->password().isEmpty()) {
        m_sync->setPassword(password);
    }
    const QString pw = password.isEmpty() ? m_sync->password() : password;
    if (pw.isEmpty()) {
        if (manual) {
            emitMessage(tr("Nextcloud backup: the password is missing"));
        }
        return;
    }
    m_webdavManual = manual;
    m_webdav->upload(m_database->setting(QStringLiteral("sync.server")),
                     m_database->setting(QStringLiteral("sync.user")), pw,
                     path, QStringLiteral("Sailcast-Backups"), 5);
}

QString AppCore::backupNow(const QString &password)
{
    const QString path = makeBackup();
    // Reported here, before the Nextcloud part (97): the page's message
    // came last and hid "the password is missing"
    if (!path.isEmpty()) {
        emitMessage(nextcloudBackupPossible() ? tr("Backup saved - copying it to the Nextcloud …")
                                              : tr("Backup saved"));
    }
    uploadBackup(path, password, true);
    return path;
}

QVariantList AppCore::localBackups() const
{
    QVariantList list;
    const QDir dir(backupDirectory());
    const QFileInfoList files = dir.entryInfoList(QStringList() << QStringLiteral("sailcast-*.db")
                                                  << QStringLiteral("before-restore.db"),
                                                  QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo &f = files.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("path"), f.absoluteFilePath());
        map.insert(QStringLiteral("name"), f.fileName());
        map.insert(QStringLiteral("modified"), double(f.lastModified().toMSecsSinceEpoch()));
        map.insert(QStringLiteral("size"), double(f.size()));
        map.insert(QStringLiteral("undo"), f.fileName() == QLatin1String("before-restore.db"));
        list.append(map);
    }
    return list;
}

void AppCore::listNextcloudBackups(const QString &password)
{
    if (!password.isEmpty() && m_sync->password().isEmpty()) {
        m_sync->setPassword(password);
    }
    const QString pw = password.isEmpty() ? m_sync->password() : password;
    if (pw.isEmpty()) {
        emit nextcloudBackupsListed(false, QVariantList(), tr("the password is missing"));
        return;
    }
    m_webdav->list(m_database->setting(QStringLiteral("sync.server")),
                   m_database->setting(QStringLiteral("sync.user")), pw, QStringLiteral("Sailcast-Backups"));
}

void AppCore::fetchNextcloudBackup(const QString &name, const QString &password)
{
    const QString pw = password.isEmpty() ? m_sync->password() : password;
    if (pw.isEmpty()) {
        emit nextcloudBackupFetched(false, QString(), tr("the password is missing"));
        return;
    }
    QDir().mkpath(backupDirectory());
    // Its own name, so it does not count among the local backups
    const QString path = backupDirectory() + QStringLiteral("/nextcloud-download.db");
    QFile::remove(path);
    m_webdav->fetch(m_database->setting(QStringLiteral("sync.server")),
                    m_database->setting(QStringLiteral("sync.user")), pw,
                    QStringLiteral("Sailcast-Backups"), name, path);
}

bool AppCore::restoreBackup(const QString &path)
{
    if (!QFileInfo(path).isFile()) {
        emitMessage(tr("Backup not found: %1").arg(path));
        return false;
    }
    if (m_updater->isBusy() || m_sync->isRunning() || m_downloads->isBusy() || m_archiver->isRunning()) {
        emitMessage(tr("Please wait until updates and downloads are finished"));
        return false;
    }
    QDir().mkpath(backupDirectory());
    const QString undo = backupDirectory() + QStringLiteral("/before-restore.db");
    QString source = path;
    // Restoring the undo copy itself: keep it apart first
    if (QFileInfo(path).absoluteFilePath() == QFileInfo(undo).absoluteFilePath()) {
        source = backupDirectory() + QStringLiteral("/restore-source.db");
        QFile::remove(source);
        QFile::copy(path, source);
    }
    if (!copyDatabaseTo(undo)) {
        emitMessage(tr("Backup failed: %1").arg(undo));
        return false;
    }
    importDatabase(source, true);
    if (source != path) {
        QFile::remove(source);
    }
    return true;
}

void AppCore::backupIfDue()
{
    bool ok = false;
    const int days = m_database->setting(QStringLiteral("backup.days"), QStringLiteral("7")).toInt(&ok);
    if (!ok || days <= 0 || podcastCount() == 0) {
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
    // An hour of slack: a daily backup at about the same time each day
    const qint64 interval = qint64(days) * 86400 - 3600;
    QString path;
    if (now - m_database->setting(QStringLiteral("backup.last"), QStringLiteral("0")).toLongLong() >= interval) {
        path = makeBackup();
    }
    // The Nextcloud copy: the newest local backup, once the sync password
    // is known (it may not be at the first check after the start)
    if (nextcloudBackupPossible() && !m_sync->password().isEmpty()
            && now - m_database->setting(QStringLiteral("backup.lastNextcloud"), QStringLiteral("0")).toLongLong() >= interval) {
        if (path.isEmpty()) {
            const QStringList files = QDir(backupDirectory()).entryList(
                        QStringList() << QStringLiteral("sailcast-*.db"), QDir::Files, QDir::Name);
            if (!files.isEmpty()) {
                path = backupDirectory() + QLatin1Char('/') + files.last();
            }
        }
        uploadBackup(path, QString(), false);
    }
}

// All messages to the user without tokens from feed/media addresses (40)
void AppCore::emitMessage(const QString &text)
{
    emit message(UrlMask::maskText(text));
}

QString AppCore::maskUrl(const QString &text) const
{
    return UrlMask::maskText(text);
}

QVariantMap AppCore::shareLink(int episodeId, int seconds) const
{
    QVariantMap map;
    const EpisodeRow e = m_database->episode(episodeId);
    if (!e.id) {
        return map;
    }
    // Never a token: a private feed's media address would give access to
    // the feed's content
    QString url = e.link.trimmed();
    if (url.isEmpty()) {
        url = m_database->podcast(e.podcastId).link.trimmed();
    }
    if (url.isEmpty() && UrlMask::maskText(e.url) == e.url) {
        url = e.url;
    }
    if (url.isEmpty() || UrlMask::maskText(url) != url) {
        return map;
    }
    QString title = e.podcastTitle.isEmpty() ? e.title
                                             : QStringLiteral("%1: %2").arg(e.podcastTitle, e.title);
    if (seconds > 0) {
        const QString clock = QStringLiteral("%1:%2:%3").arg(seconds / 3600)
                .arg((seconds / 60) % 60, 2, 10, QLatin1Char('0'))
                .arg(seconds % 60, 2, 10, QLatin1Char('0'));
        if (!url.contains(QLatin1Char('#'))) {
            url += QStringLiteral("#t=") + clock;
        }
        title = tr("%1 (from %2)").arg(title, clock);
    }
    map.insert(QStringLiteral("url"), url);
    map.insert(QStringLiteral("title"), title);
    return map;
}

void AppCore::startBrowserLogin(const QString &server)
{
    m_browserLogin->start(server);
}

void AppCore::cancelBrowserLogin()
{
    m_browserLogin->cancel();
}

bool AppCore::browserLoginActive() const
{
    return m_browserLogin->isActive();
}

// --- Folders (45) -------------------------------------------------------------

QVariantList AppCore::folders() const
{
    QVariantList list;
    const QList<FolderRow> rows = m_database->folders();
    for (int i = 0; i < rows.size(); ++i) {
        QVariantMap map;
        map.insert(QStringLiteral("folderId"), int(rows.at(i).id));
        map.insert(QStringLiteral("name"), rows.at(i).name);
        map.insert(QStringLiteral("manualUpdate"), rows.at(i).manualUpdate);
        map.insert(QStringLiteral("podcastCount"), rows.at(i).podcastCount);
        list.append(map);
    }
    return list;
}

int AppCore::createFolder(const QString &name)
{
    const qint64 id = m_database->createFolder(name);
    emit dataChanged();
    return int(id);
}

void AppCore::renameFolder(int folderId, const QString &name)
{
    m_database->renameFolder(folderId, name);
    emit dataChanged();
}

void AppCore::moveFolder(int folderId, int step)
{
    m_database->moveFolder(folderId, step);
    emit dataChanged();
}

void AppCore::setFolderManualUpdate(int folderId, bool manual)
{
    m_database->setFolderManualUpdate(folderId, manual);
    emit dataChanged();
}

bool AppCore::folderManualUpdate(int folderId) const
{
    const QList<FolderRow> rows = m_database->folders();
    for (int i = 0; i < rows.size(); ++i) {
        if (rows.at(i).id == folderId) {
            return rows.at(i).manualUpdate;
        }
    }
    return false;
}

void AppCore::deleteFolder(int folderId)
{
    m_database->deleteFolder(folderId);
    emit dataChanged();
}

QVariantList AppCore::podcastIdsInFolder(int folderId) const
{
    QVariantList list;
    const QList<qint64> ids = m_database->podcastIdsInFolder(folderId);
    for (int i = 0; i < ids.size(); ++i) {
        list.append(int(ids.at(i)));
    }
    return list;
}

QVariantList AppCore::podcastIdsWithChoice(const QString &name, const QString &onValue, bool globalOn) const
{
    const QHash<qint64, QString> own = m_database->podcastSettings(name);
    const QList<PodcastRow> podcasts = m_database->podcasts();
    QVariantList ids;
    for (int i = 0; i < podcasts.size(); ++i) {
        const QString value = own.value(podcasts.at(i).id);
        if (value == onValue || (value.isEmpty() && globalOn)) {
            ids.append(int(podcasts.at(i).id));
        }
    }
    return ids;
}

QVariantMap AppCore::podcastChoice(const QString &name, const QString &onValue, bool globalOn) const
{
    QVariantMap result;
    result.insert(QStringLiteral("on"), podcastIdsWithChoice(name, onValue, globalOn).size());
    result.insert(QStringLiteral("total"), m_database->podcasts().size());
    return result;
}

void AppCore::setPodcastFolder(int podcastId, int folderId)
{
    m_database->setPodcastFolder(podcastId, folderId);
    emit dataChanged();
}

void AppCore::markFolderPlayed(int folderId)
{
    const QList<qint64> ids = m_database->podcastIdsInFolder(folderId);
    int count = 0;
    for (int i = 0; i < ids.size(); ++i) {
        count += m_database->markAllPlayed(ids.at(i));
        if (!m_database->podcast(ids.at(i)).archiveFolder.isEmpty()) {
            m_metadataDirty.insert(ids.at(i));
            m_metadataTimer->start();
        }
    }
    afterPlayed();
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

QString AppCore::otherFolderName() const
{
    return tr("Other podcasts");
}

// --- Bookmarks (47) -------------------------------------------------------------

QVariantList AppCore::bookmarks(int episodeId) const
{
    QVariantList list;
    const QList<BookmarkRow> rows = m_database->bookmarks(episodeId);
    for (int i = 0; i < rows.size(); ++i) {
        const BookmarkRow &b = rows.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("bookmarkId"), int(b.id));
        map.insert(QStringLiteral("episodeId"), int(b.episodeId));
        map.insert(QStringLiteral("position"), b.position);
        map.insert(QStringLiteral("note"), b.note);
        map.insert(QStringLiteral("title"), b.title);
        map.insert(QStringLiteral("created"), double(b.created));
        map.insert(QStringLiteral("episodeTitle"), b.episodeTitle);
        map.insert(QStringLiteral("podcastTitle"), b.podcastTitle);
        map.insert(QStringLiteral("imageUrl"), b.imageUrl);
        list.append(map);
    }
    return list;
}

// (236) A short piece of the description around the first hit, cut at
// word boundaries, with "…" where it was cut
static QString excerptAround(const QString &text, const QString &needle)
{
    const QString plain = text.simplified();
    const int at = plain.indexOf(needle, 0, Qt::CaseInsensitive);
    if (at < 0) {
        return plain.left(120);
    }
    const int before = 50;
    const int after = 80;
    int start = qMax(0, at - before);
    int end = qMin(plain.length(), at + needle.length() + after);
    if (start > 0) {
        const int space = plain.indexOf(QLatin1Char(' '), start);
        if (space >= 0 && space < at) {
            start = space + 1;
        }
    }
    if (end < plain.length()) {
        const int space = plain.lastIndexOf(QLatin1Char(' '), end);
        if (space > at + needle.length()) {
            end = space;
        }
    }
    return (start > 0 ? QStringLiteral("… ") : QString()) + plain.mid(start, end - start)
            + (end < plain.length() ? QStringLiteral(" …") : QString());
}

QVariantMap AppCore::searchLocal(const QString &term, bool inDescriptions) const
{
    QVariantMap result;
    const QString needle = term.trimmed();
    if (needle.length() < 2) {
        return result;
    }
    // Podcasts: title or author (QString: umlauts compared correctly)
    QVariantList podcasts;
    const QList<PodcastRow> rows = m_database->podcasts();
    for (int i = 0; i < rows.size() && podcasts.size() < 5; ++i) {
        const PodcastRow &p = rows.at(i);
        if (p.title.contains(needle, Qt::CaseInsensitive) || p.author.contains(needle, Qt::CaseInsensitive)
                || p.feedTitle.contains(needle, Qt::CaseInsensitive)) {
            QVariantMap map;
            map.insert(QStringLiteral("podcastId"), int(p.id));
            map.insert(QStringLiteral("title"), p.title);
            map.insert(QStringLiteral("author"), p.author);
            map.insert(QStringLiteral("coverUrl"), p.coverUrl);
            podcasts.append(map);
        }
    }
    result.insert(QStringLiteral("podcasts"), podcasts);

    // Episodes: titles over all podcasts (the same search as on the podcast
    // page, 53 - SQLite LIKE, case-insensitive for ASCII)
    QVariantList episodes;
    const QList<EpisodeRow> found = m_database->episodes(-1, 20, Database::AllEpisodes, false, needle);
    for (int i = 0; i < found.size(); ++i) {
        const EpisodeRow &e = found.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("episodeId"), int(e.id));
        map.insert(QStringLiteral("title"), e.title);
        map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
        map.insert(QStringLiteral("imageUrl"), e.displayImageUrl);
        map.insert(QStringLiteral("published"), double(e.published) * 1000.0);
        map.insert(QStringLiteral("duration"), e.duration);
        map.insert(QStringLiteral("played"), e.played);
        map.insert(QStringLiteral("position"), e.position);
        episodes.append(map);
    }
    result.insert(QStringLiteral("episodes"), episodes);

    // (236) Episodes found only in their description, with an excerpt
    if (inDescriptions) {
        QVariantList described;
        const QList<QPair<EpisodeRow, QString>> hits = m_database->searchDescriptions(needle, 20);
        for (int i = 0; i < hits.size(); ++i) {
            const EpisodeRow &e = hits.at(i).first;
            QVariantMap map;
            map.insert(QStringLiteral("episodeId"), int(e.id));
            map.insert(QStringLiteral("title"), e.title);
            map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
            map.insert(QStringLiteral("imageUrl"), e.displayImageUrl);
            map.insert(QStringLiteral("published"), double(e.published) * 1000.0);
            map.insert(QStringLiteral("duration"), e.duration);
            map.insert(QStringLiteral("played"), e.played);
            map.insert(QStringLiteral("position"), e.position);
            map.insert(QStringLiteral("excerpt"), excerptAround(hits.at(i).second, needle));
            described.append(map);
        }
        result.insert(QStringLiteral("described"), described);
    }

    // Bookmarks: name, note or episode title
    QVariantList bookmarks;
    const QVariantList all = this->bookmarks(0);
    for (int i = 0; i < all.size() && bookmarks.size() < 5; ++i) {
        const QVariantMap b = all.at(i).toMap();
        if (b.value(QStringLiteral("title")).toString().contains(needle, Qt::CaseInsensitive)
                || b.value(QStringLiteral("note")).toString().contains(needle, Qt::CaseInsensitive)
                || b.value(QStringLiteral("episodeTitle")).toString().contains(needle, Qt::CaseInsensitive)) {
            bookmarks.append(b);
        }
    }
    result.insert(QStringLiteral("bookmarks"), bookmarks);
    return result;
}

int AppCore::addBookmark(int episodeId, int seconds)
{
    const qint64 id = m_database->addBookmark(episodeId, seconds);
    if (id > 0) {
        bookmarkArchiveChanged(episodeId);
        emit bookmarksChanged(episodeId);
        emit episodeChanged(episodeId);     // star in the lists
    }
    return int(id);
}

void AppCore::editBookmark(int bookmarkId, const QString &title, const QString &note)
{
    m_database->setBookmarkTitle(bookmarkId, title);
    setBookmarkNote(bookmarkId, note);
}

void AppCore::setBookmarkNote(int bookmarkId, const QString &note)
{
    m_database->setBookmarkNote(bookmarkId, note);
    const QList<BookmarkRow> all = m_database->bookmarks(0);
    for (int i = 0; i < all.size(); ++i) {
        if (all.at(i).id == bookmarkId) {
            bookmarkArchiveChanged(all.at(i).episodeId);
        }
    }
    emit bookmarksChanged(0);
}

void AppCore::removeBookmark(int bookmarkId)
{
    const qint64 episodeId = m_database->removeBookmark(bookmarkId);
    bookmarkArchiveChanged(episodeId);
    emit bookmarksChanged(int(episodeId));
    if (episodeId > 0) {
        emit episodeChanged(int(episodeId));
    }
}

int AppCore::bookmarkCount() const
{
    return m_database->bookmarkCount();
}

// episodes.json of an archived podcast holds its bookmarks (49): written
// again after a while, like after listening
void AppCore::bookmarkArchiveChanged(qint64 episodeId)
{
    const qint64 podcastId = m_database->episode(episodeId).podcastId;
    if (podcastId > 0 && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
}

void AppCore::markPodcastSeen(int podcastId)
{
    if (!m_database->podcast(podcastId).freshEpisodes) {
        return;
    }
    m_database->setSetting(QStringLiteral("podcast.%1.seenAt").arg(podcastId),
                           QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
    emit dataChanged();
}

// --- gpodder.net devices (51) ---------------------------------------------------

void AppCore::listDevices(const QString &server, const QString &user, const QString &password)
{
    m_devices->setAccount(server, user, password);
    m_devices->list();
}

void AppCore::chooseDevice(const QString &deviceId, bool existing)
{
    const QString id = deviceId.trimmed();
    if (id.isEmpty()) {
        return;
    }
    if (m_database->setting(QStringLiteral("sync.device")) != id) {
        // Another device has other subscriptions: start over with a first
        // sync (union, nothing is lost on either side)
        m_database->resetSyncState();
    }
    m_database->setSetting(QStringLiteral("sync.device"), id);
    m_database->setSetting(QStringLiteral("sync.deviceExisting"), existing ? QStringLiteral("1") : QStringLiteral("0"));
    emit settingsChanged();
}

void AppCore::synchronizeDevices(const QStringList &deviceIds, const QString &newDevice,
                                 const QStringList &stopIds)
{
    m_devices->synchronize(deviceIds, newDevice.trimmed(), stopIds);
}

QStringList AppCore::coverUrls() const
{
    QStringList urls;
    const QList<PodcastRow> rows = m_database->podcasts();
    for (int i = 0; i < rows.size(); ++i) {
        if (!rows.at(i).coverUrl.isEmpty()) {
            urls.append(rows.at(i).coverUrl);
        }
    }
    return urls;
}

void AppCore::resetHints()
{
    m_database->removeSettings(QStringLiteral("hint."));
}

void AppCore::markOlderPlayed(int episodeId)
{
    const qint64 podcastId = m_database->episode(episodeId).podcastId;
    const int count = m_database->markOlderPlayed(episodeId);
    afterPlayed();
    if (podcastId > 0 && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

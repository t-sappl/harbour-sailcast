// SPDX-License-Identifier: GPL-3.0-or-later
#include "appcore.h"
#include "applog.h"

#include <algorithm>
#include <cmath>
#include <functional>

#include <QCollator>
#include <QHash>
#include <QSet>
#include "urlmask.h"
#include "logscrub.h"
#include "filenames.h"
#include "nextcloudlogin.h"
#include "gpodderdevices.h"
#include "webdavbackup.h"

#include <QDate>
#include <QDateTime>
#include <QElapsedTimer>
#include <QPair>
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
#include <QDebug>
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
#include "htmlexport.h"
#include "imagecache.h"
#include <QBuffer>
#include <QImage>
#include <QSaveFile>
#include "podcastindex.h"
#include "syncengine.h"
#include "releasenotes.h"
#include <QCoreApplication>
#include <QDesktopServices>
#include <QProcess>

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
    m_archiver->setVideoRoot(videoRoot());
    m_archiver->setDownloadDirectory(m_downloads->downloadDirectory());
    m_downloads->setVideoRoot(videoRoot());
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
        case NextcloudLogin::NotStarted: text = tr("The server did not start the login – is it a Nextcloud?"); break;
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
        emitMessage(tr("The devices now share their subscriptions – synchronize to get them"));
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
    // (289) The list of connections is filled a moment after the start: an
    // update right at the start saw "no Wi-Fi" and skipped the automatic
    // downloads. They wait for the list now (see autoDownload).
    const auto networkReady = [this]() {
        if (m_networkReady) {
            return;
        }
        m_networkReady = true;
        const QList<QPair<qint64, int> > waiting = m_autoDeferred;
        m_autoDeferred.clear();
        for (int i = 0; i < waiting.size(); ++i) {
            autoDownload(waiting.at(i).first, waiting.at(i).second);
        }
    };
    connect(m_networkConfig, &QNetworkConfigurationManager::updateCompleted, this, networkReady);
    QTimer::singleShot(5000, this, networkReady);    // the list never completes: go on anyway
    m_networkConfig->updateConfigurations();
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
    // (248) The same podcast twice (a feed moved to another subscription's
    // address): merged at once, the update goes on with the remaining one
    connect(m_updater, &FeedUpdater::duplicateFound, this, &AppCore::mergeDuplicate, Qt::DirectConnection);
    // (248) Subscriptions differing only in the case of the address: merged
    // once after the start
    QTimer::singleShot(3000, this, &AppCore::mergeCaseDuplicates);
    // (289) After the connection list is complete (5 s): interrupted downloads go on
    QTimer::singleShot(6000, this, &AppCore::resumeDownloads);
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
        // (289) The first update of a subscription brings the whole back
        // catalog, but only the newest episode counts as new
        const int counted = newEpisodes >= m_database->podcast(id).episodeCount ? qMin(newEpisodes, 1) : newEpisodes;
        m_updateNew += counted;
        // (340, Thomas 17 Nov) "7 new episodes" with New episodes empty: the
        // part in podcasts hidden there is named in the message
        if (m_database->hiddenFromNew().contains(id)) {
            m_updateNewHidden += counted;
        }
        if (newEpisodes > 0) {
            // (289) Heard elsewhere: the states waiting for these episodes are
            // applied first, so such an episode is not loaded or added
            m_database->applyPendingActions();
            autoDownload(id, newEpisodes);
            autoAddToPlaylist(id, newEpisodes);
            // "New since you last looked" marker on the podcast (50)
            m_database->setSetting(QStringLiteral("podcast.%1.newAt").arg(id),
                                   QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
        }
    });
    connect(m_updater, &FeedUpdater::podcastFailed, this, [this](qint64 id, const QString &error) {
        emit podcastRefreshed(int(id), false);
        if (m_previews.contains(id)) {
            m_previews.remove(id);
            // Nothing stored yet: no trace left (320, code review: unless it
            // was subscribed in the meantime)
            if (m_database->podcast(id).episodeCount == 0 && m_database->podcast(id).provisional) {
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
        // (355, Thomas 18 Nov) Named in the message and in the log
        const PodcastRow p = m_database->podcast(id);
        // (review 355) title is already custom/feed title/URL: a URL masked
        const QString name = (p.customTitle.isEmpty() && p.feedTitle.isEmpty()) ? UrlMask::maskText(p.url) : p.title;
        m_updateFailedNames << name;
        // (review 355) Offline, every feed fails: ten lines, then one summary
        // line at the end (the Log page shows only the last part)
        if (m_updateFailedNames.size() <= 10) {
            qWarning("Update: %s failed: %s", qPrintable(name), qPrintable(UrlMask::maskText(error)));
        }
    });
    connect(m_updater, &FeedUpdater::finished, this, [this]() {
        m_database->applyPendingActions();
        // (309) Playlist episodes added without Wi-Fi: loaded now
        downloadPlaylistCatchUp();
        // (168) Imported with "only the newest unplayed": now that the
        // episodes are there (and the sync's states applied)
        markImportedOlder();
        afterMarkedPlayed();
        m_updateDone = 0;
        m_updateTotal = 0;
        emit updatingChanged();
        emit dataChanged();
        if (m_sync->isRunning()) {
            // (289) The sync is still running (at the start both run side by
            // side): the result is told when it is over, not lost
            m_updateSummaryPending = true;
            return;
        }
        emitUpdateSummary();
    });

    // --- Sync ---
    // (289) Another device unsubscribed a podcast: clean up like a local unsubscribe
    m_sync->setKeepPodcast([this](qint64 podcastId) {
        return m_currentEpisode > 0 && m_database->episode(m_currentEpisode).podcastId == podcastId;
    });
    connect(m_sync, &SyncEngine::podcastRemoving, this, [this](qint64 id) {
        flushArchiveMetadata(id);
        const QList<qint64> pending = m_downloads->pendingIds();
        for (int i = 0; i < pending.size(); ++i) {
            if (m_database->episode(pending.at(i)).podcastId == id) {
                cancelDownload(int(pending.at(i)));
            }
        }
        // (320, code review) An ended ("archived") podcast keeps its
        // downloads, as in the clean-up: another device must not take them
        if (m_database->podcast(id).archived) {
            return;
        }
        const QList<EpisodeRow> downloads = m_database->episodes(id, -1, Database::Downloaded);
        QSet<QString> folders;
        for (int i = 0; i < downloads.size(); ++i) {
            if (!isKept(downloads.at(i)) && downloads.at(i).id != m_currentEpisode) {
                QFile::remove(downloads.at(i).downloadPath);
                folders.insert(QFileInfo(downloads.at(i).downloadPath).absolutePath());
            }
        }
        removeEmptyFolders(folders);
    });
    connect(m_sync, &SyncEngine::podcastRemoved, this, [this](qint64 id, const QString &title) {
        m_updater->setPassword(id, QString());
        m_downloads->setPassword(id, QString());
        m_database->removeSettings(QStringLiteral("podcast.%1.").arg(id));
        m_metadataDirty.remove(id);
        emit podcastForgotten(int(id));     // (320) the page layer forgets the password
        emit bookmarksChanged(0);
        reloadQueue();
        emit queueChanged();
        emit dataChanged();
        emitMessage(tr("Removed by the sync: %1").arg(title));
    });
    connect(m_sync, &SyncEngine::finished, this, [this](const SyncResult &r) {
        // Episodes played on another device leave the playlist here too
        afterMarkedPlayed();
        emit syncingChanged();
        emit dataChanged();
        emit queueChanged();
        emitMessage(tr("Synchronized: %1 listening states, %2 subscription changes")
                     .arg(r.applied.applied + r.actionsUploaded)
                     .arg(r.podcastsAdded + r.podcastsRemoved + r.subscriptionsUploaded));
        if (m_updateSummaryPending && !m_updater->isBusy()) {
            emitUpdateSummary();
        }
        // The sync password is known now: a Nextcloud backup that is due
        backupIfDue();
    });
    connect(m_sync, &SyncEngine::failed, this, [this](const QString &error) {
        emit syncingChanged();
        emitMessage(tr("Sync failed: %1").arg(error));
        if (m_updateSummaryPending && !m_updater->isBusy()) {
            emitUpdateSummary();
        }
    });

    // --- Downloads ---
    connect(m_downloads, &DownloadManager::progress, this, [this](qint64 id, qint64 received, qint64 total) {
        const qreal progress = total > 0 ? qreal(received) / qreal(total) : 0.0;
        m_downloadProgress.insert(int(id), progress);
        emit downloadProgressChanged(int(id), progress);
    });
    connect(m_downloads, &DownloadManager::downloaded, this, [this](qint64 id, const QString &) {
        forgetResumable(int(id));
        m_downloadProgress.remove(int(id));
        // (334, code review Downloads 14) The "for the playlist" mark ends with
        // the download: a later own download must not stop with queueRemove
        m_downloadByPlaylist.remove(int(id));
        emit downloadFinished(int(id), true);
        emit episodeChanged(int(id));
        emit downloadsChanged();
        // Download counts and the downloads list
        emit dataChanged();
    });
    connect(m_downloads, &DownloadManager::failed, this, [this](qint64 id, const QString &error) {
        forgetResumable(int(id));
        m_downloadProgress.remove(int(id));
        m_downloadByPlaylist.remove(int(id));   // (334) as above
        emit downloadFinished(int(id), false);
        emit episodeChanged(int(id));
        emit downloadsChanged();
        const bool cancelledHere = m_cancelled.remove(int(id));
        if (error == DownloadManager::noSpaceError()) {
            // (289) Equal texts are shown once: a burst of failed downloads is one line
            emitMessage(tr("No space left – download stopped (%1 MB free)")
                         .arg(qint64(qMax<qint64>(0, m_downloads->freeBytes()) / 1048576)));
        } else if (!cancelledHere && error != DownloadManager::cancelledError()) {
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
    setupAppUpdate();
    // Not during the start itself: the backup copies the whole database
    QTimer::singleShot(20000, this, &AppCore::backupIfDue);
}

AppCore::~AppCore()
{
    m_closing = true;   // (289) aborting downloads now must not empty the list to resume
    // Pending episodes.json updates of archived podcasts
    writeArchiveMetadata();
    // The workers use the database - delete them first
    delete m_chapters;
    deleteHtmlJob();
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
        return;
    }
    // (304) Once with 1.4: before 302 hardly anything took the "new" flag
    // away, so months of old episodes would fill the view "New". They stay
    // unplayed (view "All unplayed"); only the flag goes.
    if (m_database->setting(QStringLiteral("migrate.newCleared14")).isEmpty()) {
        m_database->clearAllNew();
        m_database->setSetting(QStringLiteral("migrate.newCleared14"), QStringLiteral("1"));
    }
}

bool AppCore::isKept(const EpisodeRow &e) const
{
    // (209) In the archive folder and named there (kept by "Save in Music"
    // or archived) - a file of "Downloads in Music" is a normal download;
    // (245) video files in the Videos folder likewise
    if (!e.downloaded || e.archiveFile.isEmpty()) {
        return false;
    }
    if (isInside(e.downloadPath, archiveRoot()) || isInside(e.downloadPath, videoRoot())) {
        return true;
    }
    // (289) Files kept under an archive folder that was changed later
    const QStringList old = oldArchiveRoots();
    for (int i = 0; i < old.size(); ++i) {
        if (isInside(e.downloadPath, old.at(i))) {
            return true;
        }
    }
    return false;
}

QStringList AppCore::oldArchiveRoots() const
{
    const QString raw = m_database->setting(QStringLiteral("archive.oldRoots"));
    return raw.isEmpty() ? QStringList() : raw.split(QLatin1Char('\n'), QString::SkipEmptyParts);
}

QString AppCore::videoRoot() const
{
    return QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + QStringLiteral("/Podcasts");
}

QString AppCore::mediaRootFor(const EpisodeRow &e) const
{
    return e.mimeType.startsWith(QLatin1String("video/")) ? videoRoot() : archiveRoot();
}

QString AppCore::archiveRoot() const
{
    const QString dir = m_database->setting(QStringLiteral("archive.dir"));
    return dir.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::MusicLocation)
                           + QStringLiteral("/Podcasts")
                         : dir;
}

QString AppCore::versionText()
{
    return QStringLiteral("1.5 (build 373)");
}

QString AppCore::version() const
{
    return versionText();
}

QString AppCore::logHeader() const
{
    // The settings the player's decisions depend on - so a log tells the
    // whole story (no addresses, no account data)
    const QStringList keys {
        QStringLiteral("player.afterSingle"), QStringLiteral("player.queueMode"),
        QStringLiteral("player.remoteButtons"), QStringLiteral("player.countPlayedBelow"),
        QStringLiteral("player.skipBack"), QStringLiteral("video.mobile"),
        QStringLiteral("downloads.wifiOnly"), QStringLiteral("player.last")
    };
    QString header = QStringLiteral("Sailcast ") + version() + QStringLiteral(" - log of ")
                     + QDateTime::currentDateTime().toString(Qt::ISODate) + QStringLiteral("\n");
    for (const QString &key : keys) {
        const QString value = m_database->setting(key);
        header += key + QStringLiteral(" = ") + (value.isEmpty() ? QStringLiteral("(default)") : value)
                  + QStringLiteral("\n");
    }
    return header;
}

QString AppCore::logText() const
{
    // (354) The last lines only: a TextArea with the whole 512 KB is sluggish
    // (review 354: 600, a TextEdit lays out everything at once)
    return logHeader() + QStringLiteral("\n") + AppLog::tail(600);
}

QVariantList AppCore::failedPodcasts() const
{
    QVariantList result;
    const QList<Database::FailedPodcast> list = m_database->failedPodcasts();
    for (const Database::FailedPodcast &p : list) {
        QVariantMap m;
        m.insert(QStringLiteral("podcastId"), int(p.id));
        m.insert(QStringLiteral("title"), p.name.startsWith(QStringLiteral("http")) ? UrlMask::maskText(p.name) : p.name);
        m.insert(QStringLiteral("error"), UrlMask::maskText(p.error));
        result << m;
    }
    return result;
}

QString AppCore::saveLogText(const QString &text) const
{
    return AppLog::writeText(QStandardPaths::writableLocation(QStandardPaths::DownloadLocation), text);
}

bool AppCore::logEnabled() const
{
    return AppLog::isEnabled();
}

void AppCore::setLogEnabled(bool on)
{
    if (on == AppLog::isEnabled()) {
        return;
    }
    AppLog::setEnabled(on);
    // the switch shows what really happened (marker not written: still on)
    emit logEnabledChanged();
}

QVariantMap AppCore::anonymizedLog(const QString &text) const
{
    QList<LogScrub::Name> names;
    auto add = [&names](const QString &value, const QString &replacement, bool address) {
        if (value.trimmed().isEmpty()) {
            return;
        }
        LogScrub::Name n;
        n.text = value;
        n.replacement = replacement;
        n.address = address;
        names << n;
        if (address) {
            // (review 371) as the log writes it: long path parts and keys
            // masked (AppLog::clean)
            const QString cleaned = AppLog::clean(value);
            if (cleaned != value) {
                n.text = cleaned;
                names << n;
            }
        } else if (value.contains(QLatin1Char('"'))) {
            // qDebug() writes a title in quotes with \"
            n.text = QString(value).replace(QLatin1Char('"'), QStringLiteral("\\\""));
            names << n;
        }
    };
    // (review 371) A host that belongs to one podcast only names it
    // ("freakshow.fm", "feeds.fokus-europa.de"); shared hosts (libsyn,
    // podigee) are left to the shortening of addresses
    QHash<QString, QSet<qint64> > hostOwners;
    // (review 371) Hosting platforms, CDNs and big sites are shared with
    // podcasts that are not subscribed: never one podcast's name
    static const QStringList shared {
        QStringLiteral("libsyn.com"), QStringLiteral("podigee.io"), QStringLiteral("cloudfront.net"),
        QStringLiteral("amazonaws.com"), QStringLiteral("youtube.com"), QStringLiteral("patreon.com"),
        QStringLiteral("apple.com"), QStringLiteral("mzstatic.com"), QStringLiteral("spotify.com"),
        QStringLiteral("scdn.co"), QStringLiteral("soundcloud.com"), QStringLiteral("sndcdn.com"),
        QStringLiteral("blubrry.com"), QStringLiteral("anchor.fm"), QStringLiteral("megaphone.fm"),
        QStringLiteral("simplecast.com"), QStringLiteral("podbean.com"), QStringLiteral("buzzsprout.com"),
        QStringLiteral("transistor.fm"), QStringLiteral("captivate.fm"), QStringLiteral("acast.com"),
        QStringLiteral("art19.com"), QStringLiteral("omny.fm"), QStringLiteral("omnycontent.com"),
        QStringLiteral("audioboom.com"), QStringLiteral("spreaker.com"), QStringLiteral("podtrac.com"),
        QStringLiteral("chtbl.com"), QStringLiteral("pdst.fm"), QStringLiteral("podcastics.com"),
        QStringLiteral("letscast.fm"), QStringLiteral("podcaster.de"), QStringLiteral("podhost.de"),
        QStringLiteral("podseed.org"), QStringLiteral("podeo.co"), QStringLiteral("ausha.co"),
        QStringLiteral("feedburner.com"), QStringLiteral("google.com"), QStringLiteral("googleusercontent.com"),
        QStringLiteral("github.com"), QStringLiteral("githubusercontent.com"), QStringLiteral("wordpress.com"),
        QStringLiteral("wp.com"), QStringLiteral("squarespace.com"), QStringLiteral("substack.com"),
        QStringLiteral("akamaized.net"), QStringLiteral("fastly.net"), QStringLiteral("cdn77.org"),
        QStringLiteral("ard.de"), QStringLiteral("ardmediathek.de"), QStringLiteral("zdf.de"),
        QStringLiteral("orf.at"), QStringLiteral("br.de"), QStringLiteral("wdr.de"), QStringLiteral("ndr.de"),
        QStringLiteral("swr.de"), QStringLiteral("deutschlandfunk.de"), QStringLiteral("srf.ch"),
        QStringLiteral("bbc.co.uk"), QStringLiteral("bbci.co.uk"), QStringLiteral("npr.org"),
        QStringLiteral("heise.de"), QStringLiteral("radiofrance.fr")
    };
    auto isShared = [](const QString &host) {
        for (const QString &d : shared) {
            if (host == d || host.endsWith(QLatin1Char('.') + d)) {
                return true;
            }
        }
        return false;
    };
    // the host itself; the domain of a podcast's own website too
    auto hostsOf = [&isShared](const QString &address, bool website) {
        QStringList result;
        const QString host = QUrl(address).host().toLower();
        if (host.isEmpty() || !host.contains(QLatin1Char('.')) || isShared(host)) {
            return result;
        }
        result << host;
        const QStringList labels = host.split(QLatin1Char('.'));
        if (website && labels.size() > 2) {
            result << QStringList(labels.mid(labels.size() - 2)).join(QLatin1Char('.'));
        }
        return result;
    };
    // Podcasts: every name they go by, their folders and addresses
    const QHash<qint64, QString> musicFolders = m_database->podcastSettings(QStringLiteral("musicFolder"));
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (const PodcastRow &p : podcasts) {
        for (int i = 0; i < 3; ++i) {
            const QString address = i == 0 ? p.link : (i == 1 ? p.url : p.coverUrl);
            for (const QString &host : hostsOf(address, i == 0)) {
                hostOwners[host].insert(p.id);
            }
        }
    }
    for (auto it = hostOwners.constBegin(); it != hostOwners.constEnd(); ++it) {
        // "www.x.de" and "x.de" alike; at least "abc.de" (scrub: addresses
        // of six characters and more) - never a bare "fm"
        if (it.value().size() == 1 && it.key().length() >= 6) {
            add(it.key(), QStringLiteral("‹host of Podcast #%1›").arg(*it.value().constBegin()), true);
        }
    }
    for (const PodcastRow &p : podcasts) {
        const QString name = QStringLiteral("Podcast #%1").arg(p.id);
        add(p.title, name, false);
        add(p.feedTitle, name, false);
        add(p.customTitle, name, false);
        if (!p.title.trimmed().isEmpty()) {
            add(FileNames::safe(p.title), name, false);
        }
        add(p.archiveFolder, name, false);
        add(musicFolders.value(p.id), name, false);
        add(p.url, QStringLiteral("‹feed of %1›").arg(name), true);
        add(UrlMask::maskText(p.url), QStringLiteral("‹feed of %1›").arg(name), true);
        add(p.coverUrl, QStringLiteral("‹cover of %1›").arg(name), true);
        add(p.link, QStringLiteral("‹website of %1›").arg(name), true);
    }
    // Episodes: titles, addresses, file names (the paths go anyway)
    QSqlQuery q(m_database->handle());
    q.setForwardOnly(true);
    if (q.exec(QStringLiteral("SELECT id, title, url, image_url, link, download_path FROM episodes"))) {
        while (q.next()) {
            const QString name = QStringLiteral("Episode #%1").arg(q.value(0).toLongLong());
            add(q.value(1).toString(), name, false);
            add(q.value(2).toString(), QStringLiteral("‹%1›").arg(name), true);
            add(q.value(3).toString(), QStringLiteral("‹image of %1›").arg(name), true);
            add(q.value(4).toString(), QStringLiteral("‹website of %1›").arg(name), true);
            const QString path = q.value(5).toString();
            if (!path.isEmpty()) {
                add(QFileInfo(path).completeBaseName(), name, false);
            }
        }
    }
    QString result = LogScrub::scrub(text, names);
    // What the text shows now (review 371: not what was replaced and then
    // went with a path)
    QSet<QString> podcastIds;
    QSet<QString> episodeIds;
    static const QRegularExpression id(QStringLiteral("(Podcast|Episode) #(\\d+)"));
    QRegularExpressionMatchIterator ids = id.globalMatch(result);
    while (ids.hasNext()) {
        const QRegularExpressionMatch m = ids.next();
        (m.captured(1) == QLatin1String("Podcast") ? podcastIds : episodeIds).insert(m.captured(2));
    }
    // A note for whoever reads it: the numbers are the app's own ids
    const int firstLine = result.indexOf(QLatin1Char('\n'));
    const QString note = QStringLiteral("names replaced: \"Podcast #n\" / \"Episode #n\" are ids in the app,"
                                        " web addresses shortened to the domain\n");
    result.insert(firstLine >= 0 ? firstLine + 1 : 0, note);
    QVariantMap map;
    map.insert(QStringLiteral("text"), result);
    map.insert(QStringLiteral("podcasts"), podcastIds.size());
    map.insert(QStringLiteral("episodes"), episodeIds.size());
    return map;
}

void AppCore::reloadQueue()
{
    m_queue.clear();
    const QList<qint64> queue = m_database->queueIds();
    for (int i = 0; i < queue.size(); ++i) {
        m_queue.insert(int(queue.at(i)));
    }
    m_pinned.clear();
    const QList<qint64> pinned = m_database->queuePinnedIds();
    for (int i = 0; i < pinned.size(); ++i) {
        m_pinned.insert(int(pinned.at(i)));
    }
    m_autoAdded.clear();
    const QList<qint64> autoAdded = m_database->queueAutoIds();
    for (int i = 0; i < autoAdded.size(); ++i) {
        m_autoAdded.insert(int(autoAdded.at(i)));
    }
}

// (289) "n new episodes" of the update just finished
void AppCore::emitUpdateSummary()
{
    m_updateSummaryPending = false;
    if (m_updateRegular) {
        // (280) "No new episodes" instead of "0 new episodes"
        QString found = m_updateNew > 0 ? tr("%n new episodes", "", m_updateNew) : tr("No new episodes");
        // (340) The part New episodes does not show
        if (m_updateNew > 0 && m_updateNewHidden >= m_updateNew) {
            found = tr("%n new episodes, all in podcasts not shown in New", "", m_updateNew);
        } else if (m_updateNewHidden > 0) {
            found += QStringLiteral(" – ") + tr("%n of them in podcasts not shown in New", "", m_updateNewHidden);
        }
        // (355, Thomas 18 Nov) Which ones: up to three names, then "and n more"
        QString failed;
        if (m_updateFailed > 0) {
            const int shown = m_updateFailedNames.size() > 3 ? 3 : m_updateFailedNames.size();
            failed = tr("%n feeds failed", "", m_updateFailed);
            if (shown > 0) {
                failed += QStringLiteral(": ") + QStringList(m_updateFailedNames.mid(0, shown)).join(QStringLiteral(", "));
                if (m_updateFailedNames.size() > shown) {
                    failed += QStringLiteral(" ") + tr("and %n more", "", m_updateFailedNames.size() - shown);
                }
            }
        }
        emitMessage(m_updateFailed > 0 ? found + QStringLiteral(", ") + failed : found);
        if (m_updateFailedNames.size() > 10) {
            qWarning("Update: %d feeds failed in all (%d more not listed)", m_updateFailedNames.size(),
                     m_updateFailedNames.size() - 10);
        }
        // (355) The hint line on the Podcasts page shows again after an update
        emit failedPodcastsChanged();
    }
    m_updateNew = 0;
    m_updateNewHidden = 0;
    m_updateFailed = 0;
    m_updateFailedNames.clear();
    m_updateRegular = false;
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

void AppCore::subscribe(const QString &url, int folderId, bool hiddenFromNew)
{
    const qint64 id = m_updater->subscribe(url.trimmed());
    if (id < 0) {
        emitMessage(tr("Cannot subscribe: %1").arg(m_database->lastError()));
        return;
    }
    if (folderId > 0) {
        m_database->setPodcastFolder(id, folderId);
    }
    // (343) "Show in New episodes" switched off in the subscribe dialog
    if (hiddenFromNew) {
        QList<qint64> ids = m_database->hiddenFromNew();
        if (!ids.contains(id)) {
            ids.append(id);
            m_database->setHiddenFromNew(ids);
        }
    }
    emit dataChanged();
    emit updatingChanged();
}

// (297) Everything the statistics page shows for one period. The heat map is
// always the last 53 weeks (Monday first), whatever the period.
// (329) Few queries, no ranking/categories (the full statistics take longer):
// read every time the Podcasts page becomes active
QVariantMap AppCore::listeningOverview() const
{
    QVariantMap map;
    const QDate today = QDate::currentDate();
    const QString dayFormat = QStringLiteral("yyyyMMdd");
    auto dayOf = [&dayFormat](const QDate &d) { return d.toString(dayFormat).toInt(); };
    const QList<QPair<int, qint64>> everyDay = m_database->listeningPerDay(0);
    QHash<int, qint64> allDays;
    qint64 total = 0;
    for (int i = 0; i < everyDay.size(); ++i) {
        allDays.insert(everyDay.at(i).first, everyDay.at(i).second);
        total += everyDay.at(i).second;
    }
    // Same period as listeningStatistics(7): today and the 6 days before
    qint64 week = 0;
    for (int i = 0; i < 7; ++i) {
        week += allDays.value(dayOf(today.addDays(-i)));
    }
    // Current series as in listeningStatistics: ends today or yesterday
    int current = 0;
    QDate cursor = allDays.value(dayOf(today)) > 0 ? today : today.addDays(-1);
    while (allDays.value(dayOf(cursor)) > 0) {
        ++current;
        cursor = cursor.addDays(-1);
    }
    map.insert(QStringLiteral("empty"), total <= 0);
    map.insert(QStringLiteral("weekSeconds"), double(week));
    map.insert(QStringLiteral("totalSeconds"), double(total));
    map.insert(QStringLiteral("streak"), current);
    return map;
}

QVariantMap AppCore::listeningStatistics(int days) const
{
    QVariantMap map;
    const QDate today = QDate::currentDate();
    const QString dayFormat = QStringLiteral("yyyyMMdd");
    const int fromDay = days > 0 ? today.addDays(-(days - 1)).toString(dayFormat).toInt() : 0;
    const ListeningTotals totals = m_database->listeningTotals(fromDay);
    map.insert(QStringLiteral("seconds"), double(totals.seconds));
    map.insert(QStringLiteral("saved"), double(totals.savedSeconds));
    map.insert(QStringLiteral("episodes"), totals.episodes);
    map.insert(QStringLiteral("noTime"), m_database->listenedWithoutTime());
    const ListeningSummary all = m_database->listeningSummary();
    map.insert(QStringLiteral("firstDay"), all.firstDay);
    map.insert(QStringLiteral("empty"), all.days == 0);

    // Seconds per day of the period and of the last year
    const QList<QPair<int, qint64>> perDay = m_database->listeningPerDay(fromDay);
    QHash<int, qint64> secondsOf;
    for (int i = 0; i < perDay.size(); ++i) {
        secondsOf.insert(perDay.at(i).first, perDay.at(i).second);
    }
    auto dayOf = [&dayFormat](const QDate &d) { return d.toString(dayFormat).toInt(); };

    // Course: one bar per day (7 and 30 days); "all": see below
    QVariantList bars;
    QString unit = QStringLiteral("day");
    if (days > 0) {
        for (int i = days - 1; i >= 0; --i) {
            const QDate d = today.addDays(-i);
            QVariantMap bar;
            bar.insert(QStringLiteral("seconds"), double(secondsOf.value(dayOf(d))));
            bar.insert(QStringLiteral("label"), days <= 7 ? QLocale().dayName(d.dayOfWeek(), QLocale::ShortFormat)
                                                             : QString::number(d.day()));
            bars.append(bar);
        }
    } else if (all.firstDay > 0) {
        // (314) The unit after the length of the log: days up to two months,
        // weeks up to half a year, else months (a log of two weeks showed
        // one single bar "Nov - Nov")
        const QDate first = QDate::fromString(QString::number(all.firstDay), dayFormat);
        const qint64 span = first.daysTo(today) + 1;
        auto shortDate = [](const QDate &d) { return QLocale().toString(d, QStringLiteral("d MMM")); };
        if (span <= 62) {
            unit = QStringLiteral("day");
            for (QDate d = first; d <= today; d = d.addDays(1)) {
                QVariantMap bar;
                bar.insert(QStringLiteral("seconds"), double(secondsOf.value(dayOf(d))));
                bar.insert(QStringLiteral("label"), shortDate(d));
                bars.append(bar);
            }
        } else if (span <= 26 * 7) {
            unit = QStringLiteral("week");
            QDate week = first.addDays(1 - first.dayOfWeek());
            for (; week <= today; week = week.addDays(7)) {
                qint64 sum = 0;
                for (int i = 0; i < 7; ++i) {
                    const QDate d = week.addDays(i);
                    if (d >= first && d <= today) {
                        sum += secondsOf.value(dayOf(d));
                    }
                }
                QVariantMap bar;
                bar.insert(QStringLiteral("seconds"), double(sum));
                bar.insert(QStringLiteral("label"), shortDate(week < first ? first : week));
                bars.append(bar);
            }
        } else {
            unit = QStringLiteral("month");
            QDate month(first.year(), first.month(), 1);
            const QDate lastMonth(today.year(), today.month(), 1);
            while (month.addMonths(24) <= lastMonth) {
                month = month.addMonths(1);
            }
            const QDate firstMonth = month;
            for (; month <= lastMonth; month = month.addMonths(1)) {
                qint64 sum = 0;
                for (QDate d = month; d.month() == month.month() && d <= today; d = d.addDays(1)) {
                    sum += secondsOf.value(dayOf(d));
                }
                QVariantMap bar;
                bar.insert(QStringLiteral("seconds"), double(sum));
                // (323, blind test) First and last bar and every January with
                // the year: "Nov ... Nov" over 13 months did not say which
                QString label = QLocale().monthName(month.month(), QLocale::ShortFormat);
                if (month == firstMonth || month == lastMonth || month.month() == 1) {
                    label += QLatin1Char(' ') + QString::number(month.year());
                }
                bar.insert(QStringLiteral("label"), label);
                bars.append(bar);
            }
        }
    }
    map.insert(QStringLiteral("bars"), bars);
    map.insert(QStringLiteral("barUnit"), unit);

    // Heat map: 53 weeks, Monday first, cells after today are -1
    const QDate gridStart = today.addDays(-(52 * 7 + today.dayOfWeek() - 1));
    const QList<QPair<int, qint64>> yearDays = m_database->listeningPerDay(dayOf(gridStart));
    QHash<int, qint64> yearSeconds;
    for (int i = 0; i < yearDays.size(); ++i) {
        yearSeconds.insert(yearDays.at(i).first, yearDays.at(i).second);
    }
    QVariantList heat;
    for (int w = 0; w < 53; ++w) {
        for (int d = 0; d < 7; ++d) {
            const QDate date = gridStart.addDays(w * 7 + d);
            heat.append(date > today ? -1.0 : double(yearSeconds.value(dayOf(date))));
        }
    }
    map.insert(QStringLiteral("heat"), heat);
    // Month names under the heat map: the week in which a month starts
    QVariantList heatMonths;
    for (int w = 0; w < 53; ++w) {
        const QDate monday = gridStart.addDays(w * 7);
        if (w == 0 || monday.month() != monday.addDays(-7).month()) {
            QVariantMap m;
            m.insert(QStringLiteral("week"), w);
            m.insert(QStringLiteral("label"), QLocale().monthName(monday.month(), QLocale::ShortFormat));
            heatMonths.append(m);
        }
    }
    map.insert(QStringLiteral("heatMonths"), heatMonths);

    // Series (from all days of the period, in the order of the days)
    int longest = 0;
    int run = 0;
    QDate previous;
    qint64 weekdays[7] = {0, 0, 0, 0, 0, 0, 0};
    qint64 bestDaySeconds = 0;
    int bestDay = 0;
    qint64 sum = 0;
    for (int i = 0; i < perDay.size(); ++i) {
        const QDate d = QDate::fromString(QString::number(perDay.at(i).first), dayFormat);
        if (!d.isValid()) {
            continue;
        }
        run = (previous.isValid() && previous.daysTo(d) == 1) ? run + 1 : 1;
        longest = qMax(longest, run);
        previous = d;
        weekdays[d.dayOfWeek() - 1] += perDay.at(i).second;
        sum += perDay.at(i).second;
        if (perDay.at(i).second > bestDaySeconds) {
            bestDaySeconds = perDay.at(i).second;
            bestDay = perDay.at(i).first;
        }
    }
    // Current series: ends today, or yesterday (the day is not over yet)
    // (322) Not limited to the chosen period: "current" is a fact about today,
    // "record" the longest series of the whole log (the other tiles follow the period)
    int current = 0;
    {
        QHash<int, qint64> allDays;
        const QList<QPair<int, qint64>> everyDay = m_database->listeningPerDay(0);
        for (int i = 0; i < everyDay.size(); ++i) {
            allDays.insert(everyDay.at(i).first, everyDay.at(i).second);
        }
        QDate cursor = allDays.contains(dayOf(today)) ? today : today.addDays(-1);
        while (allDays.value(dayOf(cursor)) > 0) {
            ++current;
            cursor = cursor.addDays(-1);
        }
    }
    longest = qMax(longest, all.longestStreak);
    int periodDays = days;
    if (days == 0 && all.firstDay > 0) {
        periodDays = int(QDate::fromString(QString::number(all.firstDay), dayFormat).daysTo(today)) + 1;
    }
    map.insert(QStringLiteral("longestStreak"), longest);
    map.insert(QStringLiteral("currentStreak"), current);
    map.insert(QStringLiteral("activeDays"), perDay.size());
    map.insert(QStringLiteral("activePercent"), periodDays > 0 ? qRound(100.0 * perDay.size() / periodDays) : 0);
    map.insert(QStringLiteral("perActiveDay"), perDay.isEmpty() ? 0.0 : double(sum) / perDay.size());
    QVariantList weekdayList;
    int favourite = -1;
    qint64 favouriteSeconds = 0;
    for (int i = 0; i < 7; ++i) {
        QVariantMap w;
        w.insert(QStringLiteral("seconds"), double(weekdays[i]));
        w.insert(QStringLiteral("label"), QLocale().dayName(i + 1, QLocale::ShortFormat));
        weekdayList.append(w);
        if (weekdays[i] > favouriteSeconds) {
            favouriteSeconds = weekdays[i];
            favourite = i;
        }
    }
    map.insert(QStringLiteral("weekdays"), weekdayList);
    map.insert(QStringLiteral("favouriteWeekday"), favourite >= 0 ? QLocale().dayName(favourite + 1, QLocale::LongFormat) : QString());

    // Records
    map.insert(QStringLiteral("bestDaySeconds"), double(bestDaySeconds));
    map.insert(QStringLiteral("bestDay"), bestDay > 0
               ? QLocale().toString(QDate::fromString(QString::number(bestDay), dayFormat), tr("d MMM")) : QString());
    // (324, code review) "Most listened episode" is no longer shown (315):
    // its query is gone

    // Per podcast, folder and category
    // (324, code review) The category names from the cache file once per
    // calculation (it read and parsed the file for every podcast and category)
    const QList<FyydCategory> categoryTree = m_discoverFyyd->cachedCategories();
    QHash<QString, QString> topLabels;
    auto topLabel = [this, &categoryTree, &topLabels](const QString &top) {
        if (!topLabels.contains(top)) {
            topLabels.insert(top, categoryLabelsIn(QStringList(top), categoryTree).value(0, top));
        }
        return topLabels.value(top);
    };
    const QList<ListeningRank> ranking = m_database->listeningRanking(fromDay);
    QHash<qint64, PodcastRow> podcasts;
    const QList<PodcastRow> rows = m_database->podcasts();
    for (int i = 0; i < rows.size(); ++i) {
        podcasts.insert(rows.at(i).id, rows.at(i));
    }
    QHash<qint64, QString> folderNames;
    const QList<FolderRow> folders = m_database->folders();
    for (int i = 0; i < folders.size(); ++i) {
        folderNames.insert(folders.at(i).id, folders.at(i).name);
    }
    QVariantList rankList;
    QHash<QString, qint64> byFolder;
    QHash<QString, qint64> byMain;
    QHash<QString, double> byAll;     // (297) shared equally between the categories of a podcast
    const QString none = tr("No category");
    for (int i = 0; i < ranking.size(); ++i) {
        const ListeningRank &r = ranking.at(i);
        const bool exists = podcasts.contains(r.podcastId);
        const PodcastRow p = podcasts.value(r.podcastId);
        QVariantMap item;
        item.insert(QStringLiteral("podcastId"), r.podcastId);
        item.insert(QStringLiteral("title"), exists ? p.title : r.title);
        item.insert(QStringLiteral("coverUrl"), exists ? p.coverUrl : QString());
        item.insert(QStringLiteral("seconds"), double(r.seconds));
        item.insert(QStringLiteral("exists"), exists && !p.provisional);
        rankList.append(item);
    }
    // The folder and category sums use all podcasts of the log, not only the first 50
    const QList<ListeningRank> everyone = m_database->listeningRanking(fromDay, 100000);
    for (int i = 0; i < everyone.size(); ++i) {
        const ListeningRank &r = everyone.at(i);
        if (!podcasts.contains(r.podcastId)) {
            byFolder[tr("Gone podcasts")] += r.seconds;
            byMain[none] += r.seconds;
            byAll[none] += double(r.seconds);
            continue;
        }
        const PodcastRow &p = podcasts[r.podcastId];
        byFolder[p.folderId > 0 && folderNames.contains(p.folderId) ? folderNames.value(p.folderId) : tr("Other podcasts")] += r.seconds;
        QStringList tops;
        for (int c = 0; c < p.categories.size(); ++c) {
            const QString top = p.categories.at(c).trimmed().section(QLatin1Char('/'), 0, 0);
            if (!top.isEmpty() && !tops.contains(top)) {
                tops.append(top);
            }
        }
        if (tops.isEmpty()) {
            byMain[none] += r.seconds;
            byAll[none] += r.seconds;
            continue;
        }
        byMain[topLabel(tops.first())] += r.seconds;
        for (int t = 0; t < tops.size(); ++t) {
            byAll[topLabel(tops.at(t))] += double(r.seconds) / tops.size();
        }
    }
    auto toList = [](const QHash<QString, double> &hash) {
        QList<QPair<double, QString>> sorted;
        for (QHash<QString, double>::const_iterator it = hash.constBegin(); it != hash.constEnd(); ++it) {
            sorted.append(qMakePair(it.value(), it.key()));
        }
        std::sort(sorted.begin(), sorted.end(), [](const QPair<double, QString> &a, const QPair<double, QString> &b) {
            return a.first != b.first ? a.first > b.first : a.second < b.second;
        });
        QVariantList list;
        for (int i = 0; i < sorted.size(); ++i) {
            QVariantMap item;
            item.insert(QStringLiteral("label"), sorted.at(i).second);
            item.insert(QStringLiteral("seconds"), sorted.at(i).first);
            list.append(item);
        }
        return list;
    };
    map.insert(QStringLiteral("ranking"), rankList);
    QHash<QString, double> folderSeconds;
    for (QHash<QString, qint64>::const_iterator it = byFolder.constBegin(); it != byFolder.constEnd(); ++it) {
        folderSeconds.insert(it.key(), double(it.value()));
    }
    QHash<QString, double> mainSeconds;
    for (QHash<QString, qint64>::const_iterator it = byMain.constBegin(); it != byMain.constEnd(); ++it) {
        mainSeconds.insert(it.key(), double(it.value()));
    }
    map.insert(QStringLiteral("folders"), toList(folderSeconds));
    map.insert(QStringLiteral("categoriesMain"), toList(mainSeconds));
    map.insert(QStringLiteral("categoriesAll"), toList(byAll));
    return map;
}

void AppCore::resetListeningStatistics()
{
    if (m_database->replaceListeningLog(QList<ListeningEntry>())) {
        emit dataChanged();
    }
}

void AppCore::resetListeningOf(int podcastId)
{
    if (m_database->deleteListeningOfPodcast(podcastId)) {
        emit dataChanged();
    }
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
    return categoryLabelsIn(categories, m_discoverFyyd->cachedCategories());
}

QStringList AppCore::categoryLabelsIn(const QStringList &categories, const QList<FyydCategory> &tree) const
{
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
    // (320, code review) Subscribed since: nothing to remove
    if (!m_database->podcast(podcastId).provisional) {
        return;
    }
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId, -1, Database::Downloaded);
    QSet<QString> folders;
    for (int i = 0; i < downloads.size(); ++i) {
        if (!isKept(downloads.at(i))) {
            QFile::remove(downloads.at(i).downloadPath);
            folders.insert(QFileInfo(downloads.at(i).downloadPath).absolutePath());
        }
    }
    removeEmptyFolders(folders);
    m_database->removePodcast(podcastId, false);
    m_database->removeSettings(QStringLiteral("podcast.%1.").arg(podcastId));
}

void AppCore::unsubscribe(int podcastId)
{
    // (320, code review) The episode in the player: its file would go and the
    // player would hold an episode that no longer exists (same rule as for
    // deleting a download, 289)
    if (m_currentEpisode > 0 && m_database->episode(m_currentEpisode).podcastId == podcastId) {
        emitMessage(tr("Cannot unsubscribe while an episode of this podcast is loaded in the player"));
        return;
    }
    flushArchiveMetadata(podcastId);
    // Running downloads of the podcast end first
    const QList<qint64> pending = m_downloads->pendingIds();
    for (int i = 0; i < pending.size(); ++i) {
        if (m_database->episode(pending.at(i)).podcastId == podcastId) {
            cancelDownload(int(pending.at(i)));
        }
    }
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId, -1, Database::Downloaded);
    QSet<QString> folders;
    for (int i = 0; i < downloads.size(); ++i) {
        // Files in the archive stay, normal downloads are deleted (209: also
        // those in Music by "Downloads in Music")
        if (!isKept(downloads.at(i))) {
            QFile::remove(downloads.at(i).downloadPath);
            folders.insert(QFileInfo(downloads.at(i).downloadPath).absolutePath());
        }
    }
    removeEmptyFolders(folders);
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
    // (348) Opt-in (forum 17 Nov): sends the 10 most listened feeds to fyyd
    m_forYou->setUseSubscriptions(m_database->setting(QStringLiteral("discover.fromSubscriptions"),
                                                      QStringLiteral("0")) == QLatin1String("1"));
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
                  m_database->setting(QStringLiteral("discover.fromSubscriptions"), QStringLiteral("0"))
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
    map.insert(QStringLiteral("video"), r.video);
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
        error = tr("The feed needs a login (HTTP %1) – after subscribing, set it on the podcast page").arg(r.httpStatus);
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

QVariantMap AppCore::episodeStatus(int episodeId) const
{
    QVariantMap map;
    const EpisodeRow e = m_database->episode(episodeId);
    if (!e.id) {
        return map;
    }
    map.insert(QStringLiteral("episodeId"), int(e.id));
    map.insert(QStringLiteral("podcastId"), int(e.podcastId));
    map.insert(QStringLiteral("podcastTitle"), e.podcastTitle);
    map.insert(QStringLiteral("played"), e.played);
    map.insert(QStringLiteral("inProgress"), e.inProgress());
    map.insert(QStringLiteral("isNew"), e.isNew);
    map.insert(QStringLiteral("downloaded"), e.downloaded);
    map.insert(QStringLiteral("downloading"), isDownloading(episodeId));
    map.insert(QStringLiteral("queued"), isQueued(episodeId));
    map.insert(QStringLiteral("pinned"), isPinned(episodeId));
    map.insert(QStringLiteral("provisional"), m_database->podcast(e.podcastId).provisional);
    return map;
}

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
    // (291) When the position was stored last (Unix time): the length of the
    // pause after a restart decides whether smart resume rewinds
    map.insert(QStringLiteral("positionUpdated"), double(e.positionUpdated));
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
    // (244) Video version preferred for the podcast: streamed first, the
    // audio (download, original, archive) as the fallback
    const ParsedAlternate video = videoAlternate(e);
    if (!video.url.isEmpty()) {
        sources.prepend(QUrl(video.url).toString(QUrl::FullyEncoded));
    }
    map.insert(QStringLiteral("sources"), sources);
    map.insert(QStringLiteral("videoVersion"), !video.url.isEmpty());
    const QList<ParsedAlternate> alternates = m_database->alternates(e.id);
    map.insert(QStringLiteral("hasAlternates"), !alternates.isEmpty());
    // (245) A video or HLS version in the feed (camera symbol, switch)
    bool videoAlternate = false;
    for (int i = 0; i < alternates.size(); ++i) {
        const QString type = alternates.at(i).mimeType.toLower();
        if (type.startsWith(QLatin1String("video/")) || type.contains(QLatin1String("mpegurl"))) {
            videoAlternate = true;
        }
    }
    map.insert(QStringLiteral("hasVideoAlternate"), videoAlternate);
    // (274) What the camera stands for: "stream" (HLS, in the enclosure or as
    // a version), "file" (a plain video file), "" (audio)
    QString videoKind;
    if (e.mimeType.toLower().contains(QLatin1String("mpegurl"))) {
        videoKind = QStringLiteral("stream");
    } else if (e.mimeType.startsWith(QLatin1String("video/"))) {
        videoKind = QStringLiteral("file");
    } else {
        for (int i = 0; i < alternates.size(); ++i) {
            const QString type = alternates.at(i).mimeType.toLower();
            if (type.contains(QLatin1String("mpegurl"))) {
                videoKind = QStringLiteral("stream");
                break;
            }
            if (type.startsWith(QLatin1String("video/"))) {
                videoKind = QStringLiteral("file");
            }
        }
    }
    map.insert(QStringLiteral("videoKind"), videoKind);
    map.insert(QStringLiteral("mimeType"), e.mimeType);
    map.insert(QStringLiteral("fileSize"), double(e.fileSize));
    // (245) Size of the video that would be streamed (the version's length,
    // else bitrate x duration; the file's for a video file), 0 = unknown
    double videoBytes = 0;
    if (!video.url.isEmpty()) {
        videoBytes = video.length > 0 ? double(video.length)
                   : (video.bitrate > 0 && e.duration > 0 ? double(video.bitrate) / 8.0 * e.duration : 0);
    } else if (e.mimeType.startsWith(QLatin1String("video/"))) {
        videoBytes = double(e.fileSize);
    }
    map.insert(QStringLiteral("videoBytes"), videoBytes);

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
    // (245) "Video podcast" on the podcast page
    map.insert(QStringLiteral("video"), m_database->isVideoPodcast(p.id));
    map.insert(QStringLiteral("language"), p.language);
    map.insert(QStringLiteral("categories"), p.categories);
    map.insert(QStringLiteral("author"), p.author);
    map.insert(QStringLiteral("description"), cleanDescription(p.description));
    map.insert(QStringLiteral("coverUrl"), p.coverUrl);
    map.insert(QStringLiteral("link"), p.link);
    map.insert(QStringLiteral("url"), p.url);
    map.insert(QStringLiteral("episodeCount"), p.episodeCount);
    map.insert(QStringLiteral("unplayed"), p.unplayedCount);
    map.insert(QStringLiteral("started"), p.startedCount);
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
    afterMarkedPlayed();
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

void AppCore::removeFromNew(int episodeId)
{
    m_database->clearNew(episodeId);
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::restoreNew(int episodeId)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (e.id && !e.played && m_database->setNew(episodeId)) {
        emit episodeChanged(episodeId);
        emit dataChanged();
    }
}

int AppCore::homeNewCount() const
{
    return m_database->homeNewCount();
}

// (335) Counts of the page New episodes for the download filter
int AppCore::homeCount(bool onlyNew, bool filtered) const
{
    return m_database->homeCount(onlyNew, filtered);
}

// (335) "Clear %n shown": with a download filter only what the page shows
void AppCore::removeShownFromNew(int upToEpisodeId, const QString &filter)
{
    // (336, review 16 Nov) The filter of the tap, not the one after the countdown
    m_database->clearShownNew(upToEpisodeId, filter);
    // (337) The undo marks of ➕ stay: those episodes are no longer "new",
    // so "Clear shown" did not touch them
    emit dataChanged();
}

int AppCore::newEpisodesSnapshot() const
{
    return int(m_database->maxNewEpisodeId());
}

void AppCore::removeAllFromNew(int upToEpisodeId)
{
    // (302) All of them; nothing left to undo. (340) Not those of hidden
    // podcasts: they keep their flags until shown again (as "Clear %n shown")
    // (322) Of those that were there at the tap (not what the update brought during the countdown)
    m_database->clearAllNew(upToEpisodeId);
    m_triageByPlaylist.clear();
    emit dataChanged();
}

// (302) The user took an episode up: the "new" flag goes (the page keeps the
// row until it is left - see EpisodeListModel::keepVanished). Not for
// Auto-Add and Auto-Download, they do not call this.
void AppCore::triageByUser(int episodeId, bool viaPlaylist)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (!e.id || !e.isNew || e.played) {
        return;
    }
    m_database->clearNew(episodeId);
    if (viaPlaylist) {
        m_triageByPlaylist.insert(episodeId);
    }
    emit episodeChanged(episodeId);
    emit dataChanged();
}

void AppCore::noteDownloadByUser(int episodeId)
{
    triageByUser(episodeId, false);
}

// (334) Hide a podcast from the statistics (instead of deleting its listening
// history): its time stays in the log and counts again when shown
bool AppCore::hiddenFromStats(int podcastId) const
{
    return m_database->statsHidden().contains(podcastId);
}

void AppCore::setHiddenFromStats(int podcastId, bool hidden)
{
    QList<qint64> ids = m_database->statsHidden();
    if (hidden == ids.contains(podcastId)) {
        return;
    }
    if (hidden) {
        ids.append(podcastId);
    } else {
        ids.removeAll(podcastId);
    }
    m_database->setStatsHidden(ids);
    emit dataChanged();
}

// The hidden podcasts for the list "n podcasts hidden": title (also of a
// podcast no longer subscribed, from the log) and its whole listening time
QVariantList AppCore::statsHiddenPodcasts() const
{
    QVariantList list;
    const QList<qint64> ids = m_database->statsHidden();
    for (qint64 id : ids) {
        const PodcastRow p = m_database->podcast(id);
        QString title = p.id ? p.title : QString();
        if (title.isEmpty()) {
            title = m_database->listeningTitle(id);
        }
        QVariantMap item;
        item.insert(QStringLiteral("podcastId"), double(id));
        item.insert(QStringLiteral("title"), title.isEmpty() ? tr("Podcast no longer there") : title);
        item.insert(QStringLiteral("seconds"), double(m_database->listenedSeconds(id)));
        item.insert(QStringLiteral("subscribed"), p.id > 0 && !p.provisional);
        list.append(item);
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return a.toMap().value(QStringLiteral("title")).toString().localeAwareCompare(
                   b.toMap().value(QStringLiteral("title")).toString()) < 0;
    });
    return list;
}

bool AppCore::hiddenFromNew(int podcastId) const
{
    return m_database->hiddenFromNew().contains(podcastId);
}

void AppCore::setHiddenFromNew(int podcastId, bool hidden)
{
    QList<qint64> ids = m_database->hiddenFromNew();
    const bool now = ids.contains(podcastId);
    if (hidden == now) {
        return;
    }
    if (hidden) {
        ids.append(podcastId);
    } else {
        // (340, review) Shown again with its flags: the line and the choice
        // page show beforehand how many new episodes come back (302 cleared
        // them here, which emptied exactly what the line had promised)
        ids.removeAll(podcastId);
    }
    m_database->setHiddenFromNew(ids);
    emit dataChanged();
}

// (340) Hidden podcasts that are still subscribed (the list can hold ids
// of podcasts removed meanwhile)
int AppCore::hiddenFromNewCount() const
{
    return m_database->hiddenFromNewCount();
}

int AppCore::homeHiddenCount(bool onlyNew) const
{
    return m_database->homeHiddenCount(onlyNew);
}

// (340) All subscribed podcasts for the choice page "Show in New episodes":
// title, hidden or not, and its unplayed (onlyNew: new) episodes
QVariantList AppCore::newChoicePodcasts(bool onlyNew) const
{
    QVariantList list;
    const QList<qint64> hidden = m_database->hiddenFromNew();
    const QHash<qint64, int> counts = m_database->homeCountsByPodcast(onlyNew);
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (const PodcastRow &p : podcasts) {
        if (p.provisional) {
            continue;
        }
        QVariantMap item;
        item.insert(QStringLiteral("podcastId"), double(p.id));
        item.insert(QStringLiteral("title"), p.title.isEmpty() ? p.url : p.title);
        item.insert(QStringLiteral("hidden"), hidden.contains(p.id));
        item.insert(QStringLiteral("count"), counts.value(p.id));
        list.append(item);
    }
    std::sort(list.begin(), list.end(), [](const QVariant &a, const QVariant &b) {
        return Database::comparePodcastTitles(a.toMap().value(QStringLiteral("title")).toString(),
                                              b.toMap().value(QStringLiteral("title")).toString()) < 0;
    });
    return list;
}

// (340) "All" / "None" on the choice page: one change, one reload
void AppCore::setAllHiddenFromNew(bool hidden)
{
    const QList<qint64> before = m_database->hiddenFromNew();
    QList<qint64> ids;
    if (hidden) {
        const QList<PodcastRow> podcasts = m_database->podcasts();
        for (const PodcastRow &p : podcasts) {
            if (!p.provisional) {
                ids.append(p.id);
            }
        }
    }
    if (ids == before) {
        return;
    }
    m_database->setHiddenFromNew(ids);
    emit dataChanged();
}

bool AppCore::ensureMergeBackup()
{
    // Before each merge a fresh copy, as "before-merge.db" (restorable on
    // Settings → Backup; 320, code review: before only the first merge got one)
    QDir().mkpath(backupDirectory());
    const QString path = backupDirectory() + QStringLiteral("/before-merge.db");
    return copyDatabaseTo(path);
}

void AppCore::mergeDuplicate(qint64 from, qint64 into)
{
    const EpisodeRow current = m_database->episode(m_currentEpisode);
    // Not while an episode of it plays or downloads run - next update again
    if ((current.id && current.podcastId == from) || m_downloads->isBusy() || m_archiver->isRunning()) {
        qWarning() << "Duplicate subscription" << from << "of" << into << "left for later (busy)";
        return;
    }
    if (!ensureMergeBackup()) {
        qWarning() << "Duplicate subscription not merged: no backup copy";
        return;
    }
    const QString title = m_database->podcast(into).title;
    // (320, code review) Files of the source that the merge does not take over
    // (the same episode is downloaded in both) would stay without an entry
    QStringList sourceFiles;
    {
        const QList<EpisodeRow> src = m_database->episodes(from, -1, Database::Downloaded);
        for (int i = 0; i < src.size(); ++i) {
            if (!isKept(src.at(i))) {
                sourceFiles.append(src.at(i).downloadPath);
            }
        }
    }
    if (m_database->mergePodcasts(from, into)) {
        // (337, review 16 Nov) "Hidden" (statistics, New episodes) goes with
        // the episodes and the listening log to the remaining podcast
        {
            QList<qint64> stats = m_database->statsHidden();
            if (stats.removeAll(from) > 0) {
                if (!stats.contains(into)) {
                    stats.append(into);
                }
                m_database->setStatsHidden(stats);
            }
            QList<qint64> fresh = m_database->hiddenFromNew();
            if (fresh.removeAll(from) > 0) {
                if (!fresh.contains(into)) {
                    fresh.append(into);
                }
                m_database->setHiddenFromNew(fresh);
            }
        }
        QSet<QString> used;
        const QList<EpisodeRow> now = m_database->episodes(into, -1, Database::Downloaded);
        for (int i = 0; i < now.size(); ++i) {
            used.insert(now.at(i).downloadPath);
        }
        for (int i = 0; i < sourceFiles.size(); ++i) {
            if (!used.contains(sourceFiles.at(i))) {
                QFile::remove(sourceFiles.at(i));
            }
        }
        qWarning() << "Duplicate subscription" << from << "merged into" << into << title;
        emit dataChanged();
        emitMessage(tr("Duplicate subscription merged: %1").arg(title));
    }
}

void AppCore::mergeCaseDuplicates()
{
    const QList<QPair<qint64, qint64>> pairs = m_database->caseDuplicates();
    for (int i = 0; i < pairs.size(); ++i) {
        mergeDuplicate(pairs.at(i).first, pairs.at(i).second);
    }
}

QVariantList AppCore::markablePodcasts(bool keepNewest) const
{
    QVariantList list;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        const int count = m_database->markAllPlayedCount(podcasts.at(i).id, keepNewest);
        if (count > 0) {
            QVariantMap map;
            map.insert(QStringLiteral("podcastId"), int(podcasts.at(i).id));
            map.insert(QStringLiteral("title"), podcasts.at(i).title);
            map.insert(QStringLiteral("count"), count);
            map.insert(QStringLiteral("archived"), podcasts.at(i).archived);
            list.append(map);
        }
    }
    return list;
}

QVariantList AppCore::unplayedToDownload(int newest) const
{
    QVariantList list;
    const QList<PodcastRow> podcasts = m_database->podcasts();
    for (int i = 0; i < podcasts.size(); ++i) {
        const PodcastRow &p = podcasts.at(i);
        if (p.provisional || p.archived) {
            continue;
        }
        // Newest first
        const QList<EpisodeRow> episodes = m_database->episodes(p.id, -1, Database::Unplayed);
        int count = 0;
        qint64 bytes = 0;
        QVariantList ids;
        for (const EpisodeRow &e : episodes) {
            if (e.downloaded || e.url.isEmpty() || m_downloadProgress.contains(int(e.id))) {
                continue;
            }
            if (newest > 0 && count >= newest) {
                break;
            }
            ++count;
            bytes += qMax<qint64>(0, e.fileSize);
            ids.append(int(e.id));
        }
        if (count > 0) {
            QVariantMap map;
            map.insert(QStringLiteral("podcastId"), int(p.id));
            map.insert(QStringLiteral("title"), p.customTitle.isEmpty() ? p.title : p.customTitle);
            map.insert(QStringLiteral("count"), count);
            map.insert(QStringLiteral("bytes"), double(bytes));
            map.insert(QStringLiteral("ids"), ids);
            list.append(map);
        }
    }
    return list;
}

void AppCore::markEverythingPlayed(bool keepNewest, const QVariantList &podcastIds)
{
    // (248) Only the chosen podcasts (empty = all)
    QSet<qint64> chosen;
    for (int i = 0; i < podcastIds.size(); ++i) {
        chosen.insert(podcastIds.at(i).toLongLong());
    }
    const QList<PodcastRow> podcasts = m_database->podcasts();
    int count = 0;
    for (int i = 0; i < podcasts.size(); ++i) {
        if (podcasts.at(i).provisional || (!chosen.isEmpty() && !chosen.contains(podcasts.at(i).id))) {
            continue;
        }
        count += m_database->markAllPlayed(podcasts.at(i).id, keepNewest);
        if (!podcasts.at(i).archiveFolder.isEmpty()) {
            m_metadataDirty.insert(podcasts.at(i).id);
        }
    }
    if (!m_metadataDirty.isEmpty()) {
        m_metadataTimer->start();
    }
    afterMarkedPlayed();
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

void AppCore::markAllPlayed(int podcastId)
{
    const int count = m_database->markAllPlayed(podcastId);
    afterMarkedPlayed();
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
    // (254) The new position replaces the skip position (189)
    clearSkipped(episodeId);
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

// (289) Storage protection: false (with a message) when `bytes` would leave
// less than the reserve free
bool AppCore::storageAllows(qint64 bytes, bool quiet)
{
    if (m_downloads->fitsInStorage(bytes)) {
        return true;
    }
    if (!quiet) {
        const qint64 free = m_downloads->freeBytes();
        const int reserve = int(DownloadManager::reserveBytes() / 1048576);
        emitMessage(bytes > 0
                     ? tr("Not enough space: about %1 MB needed, %2 MB free (%3 MB stay free)")
                       .arg(qint64(bytes / 1048576)).arg(qint64(qMax<qint64>(0, free) / 1048576)).arg(reserve)
                     : tr("Not enough space: %1 MB free (%2 MB stay free)")
                       .arg(qint64(qMax<qint64>(0, free) / 1048576)).arg(reserve));
    }
    return false;
}

void AppCore::download(int episodeId)
{
    if (m_downloadProgress.contains(episodeId)) {
        return;
    }
    {
        const EpisodeRow check = m_database->episode(episodeId);
        // Several at once: the burst gets one message (equal texts are shown once)
        if (check.id && !storageAllows(qMax<qint64>(0, check.fileSize), false)) {
            emit downloadFinished(episodeId, false);
            return;
        }
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
        if (target.isEmpty()) {
            // (289) Was loaded silently to the phone before
            emitMessage(tr("A file of this episode is already in the Music folder – loading to the phone"));
        }
    }
    m_downloads->download(episodeId, target);
    // (289) Loading when the app ends: goes on at the next start
    m_resumable.insert(episodeId);
    saveResumable();
    emit downloadsChanged();
}

// (289) The downloads started by the app (not those of an archive copy) are
// kept in a setting while they run: the queue lives in memory only, and after
// the app was closed or stopped by the system the waiting ones were forgotten
void AppCore::saveResumable()
{
    QStringList ids;
    foreach (int id, m_resumable) {
        ids << QString::number(id);
    }
    m_database->setSetting(QStringLiteral("downloads.resume"), ids.join(QLatin1Char(',')));
}

void AppCore::forgetResumable(int episodeId)
{
    if (!m_closing && m_resumable.remove(episodeId)) {
        saveResumable();
    }
}

void AppCore::resumeDownloads()
{
    const QStringList ids = m_database->setting(QStringLiteral("downloads.resume"))
            .split(QLatin1Char(','), QString::SkipEmptyParts);
    if (ids.isEmpty()) {
        return;
    }
    if (!downloadAllowed()) {
        return;     // no Wi-Fi: they are not started over mobile data without asking
                    // (321, code review: the list stays for the next start, was cleared before)
    }
    m_resumable.clear();
    saveResumable();
    int started = 0;
    for (int i = 0; i < ids.size(); ++i) {
        const EpisodeRow e = m_database->episode(ids.at(i).toInt());
        if (e.id && !e.downloaded && !e.url.isEmpty()) {
            download(int(e.id));
            ++started;
        }
    }
    if (started > 0) {
        emitMessage(tr("%n interrupted downloads continue", "", started));
    }
}

void AppCore::cancelDownload(int episodeId)
{
    forgetResumable(episodeId);
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
        if (episodeId == m_currentEpisode) {
            // (289) The file of the episode in the player is in use
            emitMessage(tr("Not while this episode is loaded in the player"));
            return;
        }
        // (320, code review) Not gone (read-only card, foreign owner): the
        // entry stays, otherwise a new download would make a second file
        if (!QFile::remove(e.downloadPath) && QFileInfo::exists(e.downloadPath)) {
            emitMessage(tr("The file could not be deleted"));
            return;
        }
        m_database->clearDownload(episodeId);
        forgetPendingPlaylistDownload(episodeId);
    }
    emit episodeChanged(episodeId);
    emit dataChanged();
}

// (314) Deleted on purpose: not loaded again by the catch-up after the next
// update (a marker could still be there when the file came another way)
void AppCore::forgetPendingPlaylistDownload(int episodeId)
{
    QSet<int> pending = pendingPlaylistDownloads();
    if (pending.remove(episodeId)) {
        storePendingPlaylistDownloads(pending);
    }
}

int AppCore::deleteDownloads(const QVariantList &episodeIds)
{
    int deleted = 0;
    int stays = 0;
    for (const QVariant &v : episodeIds) {
        const int id = v.toInt();
        const EpisodeRow e = m_database->episode(id);
        if (!e.downloaded) {
            continue;
        }
        // The archive and the episode in the player keep their file
        if (isKept(e) || id == m_currentEpisode) {
            ++stays;
            continue;
        }
        if (!QFile::remove(e.downloadPath) && QFileInfo::exists(e.downloadPath)) {
            ++stays;    // (320, code review) could not be deleted: stays noted
            continue;
        }
        m_database->clearDownload(id);
        forgetPendingPlaylistDownload(id);
        emit episodeChanged(id);
        ++deleted;
    }
    if (deleted > 0) {
        emit dataChanged();
    }
    if (stays > 0) {
        emitMessage(tr("%n episodes stay: archive or playing now", "", stays));
    }
    return deleted;
}

QList<EpisodeRow> AppCore::deletableDownloadRows(int podcastId) const
{
    QList<EpisodeRow> result;
    const QList<EpisodeRow> downloads = m_database->episodes(podcastId, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        const EpisodeRow &e = downloads.at(i);
        if (!e.downloaded || isKept(e) || e.id == m_currentEpisode || m_downloads->isActive(e.id)) {
            continue;
        }
        result.append(e);
    }
    return result;
}

QVariantMap AppCore::deletableDownloads(int podcastId) const
{
    const QList<EpisodeRow> rows = deletableDownloadRows(podcastId);
    double bytes = 0;
    int inPlace = 0;
    int videos = 0;
    for (int i = 0; i < rows.size(); ++i) {
        const EpisodeRow &e = rows.at(i);
        bytes += double(QFileInfo(e.downloadPath).size());
        // (368) Already in the folder "Keep" would move it to (archive folder,
        // Videos for video): keeping only marks it, nothing moves
        if (isInside(e.downloadPath, mediaRootFor(e))) {
            ++inPlace;
        }
        if (e.mimeType.startsWith(QLatin1String("video/"))) {
            ++videos;
        }
    }
    QVariantMap map;
    map.insert(QStringLiteral("count"), rows.size());
    map.insert(QStringLiteral("bytes"), bytes);
    map.insert(QStringLiteral("inPlace"), inPlace);
    // (369, Thomas 19 Nov) The podcast's own folder, short ("Music/Podcasts/
    // <podcast>"): all already there -> where they are; else its archive
    // folder (known after the first save), else the root
    const QString root = !rows.isEmpty() && videos == rows.size() ? videoRoot() : archiveRoot();
    QString folder = root;
    if (!rows.isEmpty() && inPlace == rows.size()) {
        folder = QFileInfo(rows.first().downloadPath).absolutePath();
    } else {
        const QString name = m_database->podcast(podcastId).archiveFolder;
        if (!name.isEmpty()) {
            folder = QDir(root).absoluteFilePath(name);
        }
    }
    const QString home = QDir::homePath() + QLatin1Char('/');
    if (folder.startsWith(home)) {
        folder = folder.mid(home.size());
    }
    map.insert(QStringLiteral("folder"), folder);
    return map;
}

int AppCore::deletePodcastDownloads(int podcastId)
{
    QVariantList ids;
    QSet<QString> folders;
    const QList<EpisodeRow> rows = deletableDownloadRows(podcastId);
    for (int i = 0; i < rows.size(); ++i) {
        ids.append(int(rows.at(i).id));
        folders.insert(QFileInfo(rows.at(i).downloadPath).absolutePath());
    }
    const int deleted = deleteDownloads(ids);
    removeEmptyFolders(folders);    // (366) all of the podcast: its folder too
    return deleted;
}

bool AppCore::podcastInPlayer(int podcastId) const
{
    return m_currentEpisode > 0 && m_database->episode(m_currentEpisode).podcastId == podcastId;
}

void AppCore::removeEmptyFolders(const QSet<QString> &folders)
{
    QStringList roots;
    roots << m_downloads->downloadDirectory() << archiveRoot() << videoRoot() << oldArchiveRoots();
    for (const QString &folder : folders) {
        const QString path = QDir::cleanPath(QDir(folder).absolutePath());
        if (path.isEmpty() || path == QLatin1String("/")) {
            continue;
        }
        bool isRoot = false;
        bool inside = false;
        for (const QString &root : roots) {
            if (root.isEmpty()) {
                continue;
            }
            if (QDir::cleanPath(QDir(root).absolutePath()) == path) {
                isRoot = true;
            } else if (isInside(path, root)) {
                inside = true;
            }
        }
        if (isRoot || !inside) {
            continue;
        }
        QDir dir(path);
        if (dir.exists() && dir.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty()) {
            if (QDir().rmdir(path)) {
                qInfo("[AppCore] Removed the empty folder %s", qPrintable(QFileInfo(path).fileName()));
            }
        }
    }
}

void AppCore::keepEpisode(int episodeId)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (isKept(e)) {
        emitMessage(tr("Already saved in %1").arg(QFileInfo(e.downloadPath).absolutePath()));
        return;
    }
    // (209) Already in Music ("Downloads in Music"): only marked as kept;
    // (245) a video file in Videos
    if (e.downloaded && isInside(e.downloadPath, mediaRootFor(e))) {
        m_database->setArchiveFile(e.id, QFileInfo(e.downloadPath).fileName());
        emit episodeChanged(episodeId);
        emit dataChanged();
        emitMessage(tr("Saved in %1").arg(QFileInfo(e.downloadPath).absolutePath()));
        return;
    }
    if (episodeId == m_currentEpisode && e.downloaded) {
        // (289) Moving the file would break the episode playing
        emitMessage(tr("Not while this episode is loaded in the player – save it afterwards"));
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
    // (289) Everything at once: the whole size must fit before anything starts
    qint64 needed = 0;
    for (const EpisodeRow &e : episodes) {
        if (!(e.downloaded || e.url.isEmpty() || (unplayedOnly && e.played)
                || m_downloadProgress.contains(int(e.id)))) {
            needed += qMax<qint64>(0, e.fileSize);
        }
    }
    if (!storageAllows(needed, false)) {
        return 0;
    }
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
        afterMarkedPlayed();
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
        // (289) Like the menu: skipped before = back where it was left
        const int at = skippedAt(id);
        if (at > 0) {
            m_database->recordPlayback(id, 0, at, e.duration);
            clearSkipped(id);
        }
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

int AppCore::queuePodcastCount(int podcastId) const
{
    int count = 0;
    const QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::Unplayed, true);
    for (const EpisodeRow &e : episodes) {
        if (!m_queue.contains(int(e.id))) {
            ++count;
        }
    }
    return count;
}

int AppCore::queueDownloadedCount() const
{
    int count = 0;
    const QList<EpisodeRow> episodes = m_database->episodes(-1, -1, Database::Downloaded);
    for (const EpisodeRow &e : episodes) {
        if (!e.played && !m_queue.contains(int(e.id)) && e.id != m_currentEpisode) {
            ++count;
        }
    }
    return count;
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
        afterMarkedPlayed();
        emit dataChanged();
    }
    return marked;
}

int AppCore::downloadEpisodes(const QVariantList &episodeIds)
{
    qint64 needed = 0;
    for (int i = 0; i < episodeIds.size(); ++i) {
        const int id = episodeIds.at(i).toInt();
        if (id > 0 && !m_downloadProgress.contains(id)) {
            needed += qMax<qint64>(0, m_database->episode(id).fileSize);
        }
    }
    if (!storageAllows(needed, false)) {
        return 0;
    }
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
        // (245) Video files belong to the Videos folder
        if (isInside(e.downloadPath, mediaRootFor(e)) != inMusic.value(e.podcastId)) {
            result.append(e);
        }
    }
    return result;
}

QVariantList AppCore::misplacedDownloads(int podcastId) const
{
    QHash<qint64, QVariantMap> byPodcast;
    QList<qint64> order;
    const QList<EpisodeRow> episodes = misplacedEpisodes(podcastId);
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (!byPodcast.contains(e.podcastId)) {
            QVariantMap map;
            map.insert(QStringLiteral("podcastId"), int(e.podcastId));
            map.insert(QStringLiteral("title"), e.podcastTitle);
            map.insert(QStringLiteral("count"), 0);
            map.insert(QStringLiteral("videos"), 0);
            map.insert(QStringLiteral("bytes"), 0.0);
            map.insert(QStringLiteral("toMusic"), !isInside(e.downloadPath, mediaRootFor(e)));
            byPodcast.insert(e.podcastId, map);
            order.append(e.podcastId);
        }
        QVariantMap &map = byPodcast[e.podcastId];
        map.insert(QStringLiteral("count"), map.value(QStringLiteral("count")).toInt() + 1);
        // (246) video files go to Videos, not Music
        if (e.mimeType.startsWith(QLatin1String("video/"))) {
            map.insert(QStringLiteral("videos"), map.value(QStringLiteral("videos")).toInt() + 1);
        }
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
    int moved = 0;
    int failed = 0;
    for (int p = 0; p < podcastIds.size(); ++p) {
        const QList<EpisodeRow> episodes = misplacedEpisodes(podcastIds.at(p).toLongLong());
        for (int i = 0; i < episodes.size(); ++i) {
            const EpisodeRow &e = episodes.at(i);
            const QString target = isInside(e.downloadPath, mediaRootFor(e)) ? m_downloads->defaultTarget(e.id)
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
    QSet<QString> movedFrom;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (!e.downloaded || isKept(e)) {
            continue;
        }
        if (isInside(e.downloadPath, mediaRootFor(e))) {
            // (209) Already in Music: only marked as kept (245: or Videos)
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
        movedFrom.insert(QFileInfo(e.downloadPath).absolutePath());
        folder = QFileInfo(path).absolutePath();
        ++saved;
        emit episodeChanged(int(e.id));
    }
    // (366) All downloads moved to the archive: the old folder, now empty, goes
    removeEmptyFolders(movedFrom);
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
        queueAddImpl(int(e.id), false, true, false);   // (288) sorted in with "Sorted: keep" like the other bulk paths
        ++added;
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

ParsedAlternate AppCore::videoAlternate(const EpisodeRow &e) const
{
    ParsedAlternate best;
    if (m_database->setting(QStringLiteral("podcast.%1.version").arg(e.podcastId)) != QLatin1String("video")) {
        return best;
    }
    const QList<ParsedAlternate> list = m_database->alternates(e.id);
    for (int i = 0; i < list.size(); ++i) {
        const ParsedAlternate &a = list.at(i);
        const QString type = a.mimeType.toLower();
        if (!type.startsWith(QLatin1String("video/")) && !type.contains(QLatin1String("mpegurl"))) {
            continue;
        }
        // The default one, else the first; up to 1080 lines preferred
        if (best.url.isEmpty() || (a.isDefault && !best.isDefault)
                || (best.height > 1080 && a.height > 0 && a.height <= 1080)) {
            best = a;
        }
    }
    return best;
}

bool AppCore::podcastHasVideoVersion(int podcastId) const
{
    return m_database->hasVideoAlternates(podcastId);
}

void AppCore::logListening(int episodeId, int seconds, int mediaSeconds)
{
    // A stretch is at most a few minutes (the player sends every 30 s);
    // (245) the episode time at most 4x the wall clock
    if (seconds > 0 && seconds <= 3600) {
        m_database->addListening(episodeId, seconds, qBound(0, mediaSeconds, 4 * seconds));
    }
}

QString AppCore::mimeType(int episodeId) const
{
    // (244) Playing the video version: its type (HLS counts as video)
    const ParsedAlternate alt = videoAlternate(m_database->episode(episodeId));
    if (!alt.url.isEmpty()) {
        return alt.mimeType.toLower().startsWith(QLatin1String("video/"))
                ? alt.mimeType : QStringLiteral("video/x-mpegurl");
    }
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
    // (275) Of them the archived ones (own section on the Downloads page)
    qint64 archivedBytes = 0;
    QHash<qint64, bool> podcastArchived;
    const QList<EpisodeRow> downloads = m_database->episodes(-1, -1, Database::Downloaded);
    for (int i = 0; i < downloads.size(); ++i) {
        const QFileInfo info(downloads.at(i).downloadPath);
        if (info.exists()) {
            ++count;
            bytes += info.size();
            if (!podcastArchived.contains(downloads.at(i).podcastId)) {
                podcastArchived.insert(downloads.at(i).podcastId, m_database->podcast(downloads.at(i).podcastId).archived);
            }
            if (podcastArchived.value(downloads.at(i).podcastId) || !downloads.at(i).archiveFile.isEmpty()) {
                archivedBytes += info.size();
            }
            // (245) The Videos folder counts with Music (both shared folders)
            if (isInside(downloads.at(i).downloadPath, root) || isInside(downloads.at(i).downloadPath, videoRoot())) {
                ++music;
                musicBytes += info.size();
            }
        }
    }
    // (190) Played ones: what "Delete played downloads" would free - (274)
    // counted by the cleanup itself (dry run), so the tile and the button
    // agree: kept files, "never" podcasts and archived ones are not counted
    {
        const CleanupResult freeable = m_downloads->cleanup(0, archiveRoot(), m_currentEpisode, podcastCleanupDays(), true, true);
        played = freeable.files;
        playedBytes = freeable.bytes;
    }
    QVariantMap map;
    map.insert(QStringLiteral("count"), count);
    map.insert(QStringLiteral("bytes"), double(bytes));
    map.insert(QStringLiteral("played"), played);
    map.insert(QStringLiteral("playedBytes"), double(playedBytes));
    map.insert(QStringLiteral("archivedBytes"), double(archivedBytes));
    map.insert(QStringLiteral("music"), music);
    map.insert(QStringLiteral("musicBytes"), double(musicBytes));
    // (190) Free space where the downloads go
    QStorageInfo storage(m_downloads->downloadDirectory());
    map.insert(QStringLiteral("free"), storage.isValid() ? double(storage.bytesAvailable()) : -1.0);
    map.insert(QStringLiteral("reserve"), double(DownloadManager::reserveBytes()));
    return map;
}

QVariantMap AppCore::queueStats(int excludeId) const
{
    // (190) The playlist page tiles: episodes, time left, downloaded;
    // (246) the episode playing left out - the page adds it with its live
    // position
    const QList<EpisodeRow> rows = m_database->episodes(-1, -1, Database::Queue);
    int count = 0;
    qint64 seconds = 0;
    int downloaded = 0;
    for (const EpisodeRow &e : rows) {
        if (e.id == excludeId) {
            continue;
        }
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
    const CleanupResult r = m_downloads->cleanup(days, archiveRoot(), m_currentEpisode, podcastCleanupDays(), all);
    emit dataChanged();
    // (274) Say what stayed and why when the user asked and something stayed
    // (Thomas 7 Nov: "0 downloads deleted" with nine played ones listed)
    QStringList why;
    if (all && r.keptNever > 0) {
        why << tr("podcast set to \"Never\"");
    }
    if (all && r.keptArchived > 0) {
        why << tr("archived podcast");
    }
    if (all && r.keptInMusic > 0) {
        why << tr("saved in Music or Videos");
    }
    if (why.isEmpty()) {
        emitMessage(tr("%n downloads deleted", "", r.files));
    } else {
        const int kept = r.keptNever + r.keptArchived + r.keptInMusic;
        emitMessage(tr("%n downloads deleted", "", r.files) + QStringLiteral(" · ") + tr("%n kept", "", kept)
                    + QStringLiteral(" (") + why.join(QStringLiteral(", ")) + QLatin1Char(')'));
    }
    return r.files;
}

// (320, code review) Episodes were marked as played or states came from the
// sync: the player may hold one of them (see PlayerController)
void AppCore::afterMarkedPlayed()
{
    afterPlayed();
    emit listenStateChanged();
}

void AppCore::afterPlayed()
{
    // Played episodes leave the playlist
    const QList<qint64> queue = m_database->queueIds();
    bool queueChangedHere = false;
    QSet<int> pending = pendingPlaylistDownloads();
    const int pendingBefore = pending.size();
    for (int i = 0; i < queue.size(); ++i) {
        if (m_database->episode(queue.at(i)).played) {
            m_database->queueRemove(queue.at(i));
            // (337, review 16 Nov) Out of the playlist = off the waiting list
            // (else the catch-up loaded it after a later "without downloading")
            pending.remove(int(queue.at(i)));
            queueChangedHere = true;
        }
    }
    if (pending.size() != pendingBefore) {
        storePendingPlaylistDownloads(pending);
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
    m_archiver->setCurrentEpisode(episodeId);
    // (189) Played again: the skip position has been used (or not wanted) -
    // (254) not here any more but when the new position is saved, see
    // recordPlayback(): clearing it as soon as the episode was loaded lost it
    // when nothing was saved afterwards (left again before it played, a
    // stream that did not start, the app closed with the episode only
    // restored) - the played episode then started from the beginning
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
    m_downloads->cleanup(days, archiveRoot(), m_currentEpisode, podcastCleanupDays());
    // (209) Half downloads of failed attempts never tried again
    m_downloads->removeStaleParts(archiveRoot(), 1);   // (289) 1 day, was 7
    m_downloads->removeStaleParts(videoRoot(), 1);
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
    if (autoSetting != QLatin1String("1")) {
        return;
    }
    if (!m_networkReady && !onWifi()) {
        // (289) The list of connections is not complete yet
        m_autoDeferred.append(qMakePair(podcastId, newEpisodes));
        return;
    }
    if (!downloadAllowed()) {
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
    // (289) The episodes of THIS update (not "all new ones of the podcast": old
    // ones left over from earlier updates took the places), without those
    // already heard (also on another device, see podcastUpdated) or loaded,
    // in the podcast's own order - the first ones to listen to come first
    const bool oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(podcastId),
                                                 QStringLiteral("0")) == QLatin1String("1");
    if (!storageAllows(0, true)) {
        // (289) Storage protection: stops for the whole run, told once
        if (!m_lowSpaceTold) {
            m_lowSpaceTold = true;
            emitMessage(tr("Storage almost full – automatic downloads stopped"));
            QTimer::singleShot(60000, this, [this]() { m_lowSpaceTold = false; });
        }
        return;
    }
    QList<EpisodeRow> episodes = updateEpisodes(podcastId, newEpisodes);
    for (int i = episodes.size() - 1; i >= 0; --i) {
        if (episodes.at(i).played || episodes.at(i).downloaded || episodes.at(i).url.isEmpty()) {
            episodes.removeAt(i);
        }
    }
    std::stable_sort(episodes.begin(), episodes.end(), [oldestFirst](const EpisodeRow &a, const EpisodeRow &b) {
        return oldestFirst ? (a.published != b.published ? a.published < b.published : a.id < b.id)
                           : (a.published != b.published ? a.published > b.published : a.id > b.id);
    });
    while (episodes.size() > AutoDownloadLimit) {
        episodes.removeLast();
    }
    for (int i = 0; i < episodes.size(); ++i) {
        // (289) A video is large: automatically only on Wi-Fi, also with
        // "Only on Wi-Fi" off (by hand it asks)
        if (episodes.at(i).mimeType.startsWith(QLatin1String("video/")) && !onWifi()) {
            continue;
        }
        if (!storageAllows(qMax<qint64>(0, episodes.at(i).fileSize), true)) {
            break;
        }
        download(int(episodes.at(i).id));
    }
}

// (289) The rows inserted by the last update of the podcast: new rows have the
// highest ids
QList<EpisodeRow> AppCore::updateEpisodes(qint64 podcastId, int newEpisodes) const
{
    QList<EpisodeRow> episodes = m_database->episodes(podcastId, -1, Database::AllEpisodes);
    std::sort(episodes.begin(), episodes.end(), [](const EpisodeRow &a, const EpisodeRow &b) {
        return a.id > b.id;
    });
    while (episodes.size() > newEpisodes) {
        episodes.removeLast();
    }
    return episodes;
}

// --- Playlist ----------------------------------------------------------------

// (334, review Downloads 11, Thomas 15 Nov) A heard episode put into the
// playlist by hand is to be heard again: unplayed as with "Mark as unplayed"
// (goes to the sync), else the next update/sync took it out again
// (afterPlayed). Only played == true: a started, unheard episode keeps its
// position. Auto-Add does not come here (it takes no heard episodes).
void AppCore::replayIfPlayed(int episodeId)
{
    const EpisodeRow e = m_database->episode(episodeId);
    if (e.id && e.played) {
        markUnplayed(episodeId);
    }
}

void AppCore::queueAddDefault(int episodeId)
{
    replayIfPlayed(episodeId);
    const bool keep = keepSorted();
    const bool next = !keep && m_database->setting(QStringLiteral("playlist.addTo"), QStringLiteral("end")) == QLatin1String("next");
    queueAdd(episodeId, next);
    triageByUser(episodeId, true);
    downloadForPlaylist(episodeId);
}

// (334, forum 16 Nov) Context menu: like ➕, with the other download choice
// than the setting "Download playlist episodes" (without downloading when it
// is on, with downloading when it is off)
void AppCore::queueAddDefaultOtherDownload(int episodeId)
{
    replayIfPlayed(episodeId);
    const bool loads = m_database->setting(QStringLiteral("playlist.download"), QStringLiteral("1")) == QLatin1String("1");
    const bool keep = keepSorted();
    const bool next = !keep && m_database->setting(QStringLiteral("playlist.addTo"), QStringLiteral("end")) == QLatin1String("next");
    queueAdd(episodeId, next);
    triageByUser(episodeId, true);
    if (!loads) {
        downloadForPlaylist(episodeId, true);
    } else {
        // (337) "Without downloading": no older waiting mark loads it later
        forgetPendingPlaylistDownload(episodeId);
    }
}

bool AppCore::keepSorted() const
{
    return m_database->setting(QStringLiteral("playlist.sortKeep"), QStringLiteral("0")) == QLatin1String("1")
            && !m_database->setting(QStringLiteral("playlist.sortMode")).isEmpty();
}

void AppCore::queueAdd(int episodeId, bool next)
{
    queueAddImpl(episodeId, next, true, false);
}

// (277) The explicit choice of the context menu ("Play next", "Add to end
// of playlist"): with "Sorted: keep" the place is not overruled by the
// sorting - the episode is pinned there instead (without it no pin: a
// later sorting may place it like any other)
void AppCore::queueAddPlaced(int episodeId, bool next)
{
    replayIfPlayed(episodeId);
    const bool keep = keepSorted();
    // (288) An episode in the playlist moved by "Play next" (Up next tab)
    // keeps a pin it had
    const bool wasPinned = m_pinned.contains(episodeId);
    queueAddImpl(episodeId, next, !keep, keep || wasPinned);
    triageByUser(episodeId, true);
    downloadForPlaylist(episodeId);
}

void AppCore::queueAddImpl(int episodeId, bool next, bool sortIn, bool pin, bool autoAdded)
{
    // The row is written again (INSERT OR REPLACE): pin and Auto-Add mark are gone
    m_database->queueAdd(episodeId, next);
    m_pinned.remove(episodeId);
    m_autoAdded.remove(episodeId);
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
    // (288) To the very front but behind pins that stand at the front: they
    // keep their places
    if (next && !m_pinned.isEmpty() && !(m_currentEpisode > 0 && m_currentEpisode != episodeId
                                          && m_database->queueIds().contains(m_currentEpisode))) {
        QList<qint64> ids = m_database->queueIds();
        ids.removeAll(episodeId);
        int lead = 0;
        while (lead < ids.size() && m_pinned.contains(int(ids.at(lead)))) {
            ++lead;
        }
        if (lead > 0) {
            m_database->queueMove(episodeId, lead);
        }
    }
    // (275) "Sorted: keep": added to the end = put in its place
    if (!next && sortIn) {
        placeSorted(episodeId);
    }
    if (pin) {
        m_database->queueSetPinned(episodeId, true);
        m_pinned.insert(episodeId);
    }
    // (286) Only Auto-Add sets the mark (hand-added episodes never carry it)
    if (autoAdded && !pin) {
        m_database->queueSetAuto(episodeId, true);
        m_autoAdded.insert(episodeId);
    }
    m_queue.insert(episodeId);
    emit queueChanged();
    emit episodeChanged(episodeId);
}

// (277) Releases one pin; with "Sorted: keep" the episode goes to its
// place in the sorting at once
void AppCore::queueUnpin(int episodeId)
{
    if (!m_pinned.contains(episodeId)) {
        return;
    }
    m_database->queueSetPinned(episodeId, false);
    m_pinned.remove(episodeId);
    if (keepSorted()) {
        const QString mode = m_database->setting(QStringLiteral("playlist.sortMode"));
        if (mode == QLatin1String("smartShuffle")) {
            // (334, code review Downloads 17) Smart Shuffle is random: a new
            // sorting mixed the whole list. Only this episode goes to its
            // place (insertion rule of placeSorted), the rest stays.
            // (336, review 16 Nov) The episode playing stays where it is: the
            // playlist goes on after its place
            if (episodeId != m_currentEpisode) {
                placeSorted(episodeId);
            }
            emit queueChanged();
        } else {
            queueSort(mode, true);
        }
    } else {
        emit queueChanged();
    }
}

void AppCore::queueRemove(int episodeId)
{
    // (309) A download "to the playlist" started and still running stops with it
    if (m_downloadByPlaylist.remove(episodeId) && m_downloadProgress.contains(episodeId)) {
        cancelDownload(episodeId);
    }
    // (310) Not caught up any more
    {
        QSet<int> pending = pendingPlaylistDownloads();
        if (pending.remove(episodeId)) {
            storePendingPlaylistDownloads(pending);
        }
    }
    m_database->queueRemove(episodeId);
    m_queue.remove(episodeId);
    m_pinned.remove(episodeId);
    m_autoAdded.remove(episodeId);
    emit queueChanged();
    // (302) The flag this session's "to the playlist" took away: back (only for
    // an episode not heard yet - setNew checks)
    const bool restore = m_triageByPlaylist.remove(episodeId) && m_database->setNew(episodeId);
    emit episodeChanged(episodeId);
    if (restore) {
        emit dataChanged();
    }
}

QSet<int> AppCore::pendingPlaylistDownloads() const
{
    QSet<int> ids;
    const QStringList parts = m_database->setting(QStringLiteral("playlist.pendingDownload"))
            .split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString &part : parts) {
        const int id = part.toInt();
        if (id > 0) {
            ids.insert(id);
        }
    }
    return ids;
}

void AppCore::storePendingPlaylistDownloads(const QSet<int> &ids)
{
    QStringList parts;
    for (int id : ids) {
        parts << QString::number(id);
    }
    m_database->setSetting(QStringLiteral("playlist.pendingDownload"), parts.join(QLatin1Char(',')));
    // (336) The "always" marks are a part of the waiting list: what leaves
    // it (loaded, removed, deleted on purpose) loses its mark, too
    const QSet<int> always = pendingAlwaysDownloads();
    QSet<int> keep = always;
    keep.intersect(ids);
    if (keep != always) {
        storePendingAlwaysDownloads(keep);
    }
}

// (336, review 16 Nov) "Add to playlist and download" with the setting
// "Download playlist episodes" off: waiting for Wi-Fi it must be loaded by
// the catch-up all the same (that one dropped every waiting episode when
// the setting was off). Setting "playlist.pendingDownloadAlways".
QSet<int> AppCore::pendingAlwaysDownloads() const
{
    QSet<int> ids;
    const QStringList parts = m_database->setting(QStringLiteral("playlist.pendingDownloadAlways"))
            .split(QLatin1Char(','), QString::SkipEmptyParts);
    for (const QString &part : parts) {
        const int id = part.toInt();
        if (id > 0) {
            ids.insert(id);
        }
    }
    return ids;
}

void AppCore::storePendingAlwaysDownloads(const QSet<int> &ids)
{
    QStringList parts;
    for (int id : ids) {
        parts << QString::number(id);
    }
    m_database->setSetting(QStringLiteral("playlist.pendingDownloadAlways"), parts.join(QLatin1Char(',')));
}

void AppCore::downloadForPlaylist(int episodeId, bool always)
{
    if (!always && m_database->setting(QStringLiteral("playlist.download"), QStringLiteral("1")) != QLatin1String("1")) {
        return;
    }
    const EpisodeRow e = m_database->episode(episodeId);
    if (!e.id || e.downloaded || m_downloadProgress.contains(episodeId)) {
        return;
    }
    // Without Wi-Fi under "Wi-Fi only", or a video without Wi-Fi (245):
    // streamed for now, remembered and loaded after the next update with
    // Wi-Fi (downloadPlaylistCatchUp)
    if (!downloadAllowed() || (e.mimeType.startsWith(QLatin1String("video/")) && !onWifi())) {
        QSet<int> pending = pendingPlaylistDownloads();
        pending.insert(episodeId);
        storePendingPlaylistDownloads(pending);
        if (always) {
            QSet<int> marks = pendingAlwaysDownloads();
            marks.insert(episodeId);
            storePendingAlwaysDownloads(marks);
        }
        return;
    }
    m_downloadByPlaylist.insert(episodeId);
    download(episodeId);
}

void AppCore::downloadPlaylistCatchUp()
{
    QSet<int> pending = pendingPlaylistDownloads();
    if (pending.isEmpty()) {
        return;
    }
    if (m_database->setting(QStringLiteral("playlist.download"), QStringLiteral("1")) != QLatin1String("1")) {
        // (336) Setting off: only what was added with "… and download" waits on
        pending.intersect(pendingAlwaysDownloads());
        if (pending.isEmpty()) {
            storePendingPlaylistDownloads(pending);
            return;
        }
    }
    if (!downloadAllowed()) {
        storePendingPlaylistDownloads(pending);
        return;
    }
    // (310) Only what was added by hand and could not be loaded then - not
    // every episode of the playlist that is not loaded
    const QSet<qint64> queued = QSet<qint64>::fromList(m_database->queueIds());
    const QList<int> ids = pending.toList();
    for (int id : ids) {
        const EpisodeRow e = m_database->episode(id);
        if (!e.id || !queued.contains(id) || e.downloaded) {
            pending.remove(id);
            continue;
        }
        if (e.mimeType.startsWith(QLatin1String("video/")) && !onWifi()) {
            continue;
        }
        pending.remove(id);
        if (!m_downloadProgress.contains(id)) {
            m_downloadByPlaylist.insert(id);
            download(id);
        }
    }
    storePendingPlaylistDownloads(pending);
}

void AppCore::queueMove(int episodeId, int newIndex)
{
    m_database->queueMove(episodeId, newIndex);
    emit queueChanged();
}

void AppCore::queueSetOrder(const QVariantList &episodeIds, int movedId)
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
    const bool changed = ids != current;
    m_database->queueSetOrder(ids);
    // (275) An episode dragged to another place stays there when sorting
    if (changed && movedId > 0 && m_queue.contains(movedId)) {
        m_database->queueSetPinned(movedId, true);
        m_pinned.insert(movedId);
        // (286) Moved by hand: no longer "put there by Auto-Add"
        if (m_autoAdded.remove(movedId)) {
            m_database->queueSetAuto(movedId, false);
        }
    }
    emit queueChanged();
}

namespace {

// (275) What sorting the playlist needs of an episode
struct SortItem
{
    qint64 id = 0;
    qint64 podcastId = 0;
    QString podcastTitle;
    qint64 published = 0;
    int duration = 0;
    qint64 downloadedAt = 0;
    bool oldestFirst = false;   // the podcast's own episode order
};

// Order within one podcast: as set for the podcast (oldest or newest first)
bool inPodcastOrder(const SortItem &a, const SortItem &b)
{
    if (a.published != b.published) {
        return a.oldestFirst ? a.published < b.published : a.published > b.published;
    }
    return a.id < b.id;
}

bool sortLess(const QString &mode, const SortItem &a, const SortItem &b)
{
    if (mode == QLatin1String("oldest")) {
        return a.published != b.published ? a.published < b.published : a.id < b.id;
    }
    if (mode == QLatin1String("newest")) {
        return a.published != b.published ? a.published > b.published : a.id < b.id;
    }
    if (mode == QLatin1String("shortest")) {
        // Unknown length (0) last
        const bool au = a.duration <= 0;
        const bool bu = b.duration <= 0;
        if (au != bu) {
            return bu;
        }
        if (a.duration != b.duration) {
            return a.duration < b.duration;
        }
        // (288) Same length: the newer one first (a visible reason), then storage order
        return a.published != b.published ? a.published > b.published : a.id < b.id;
    }
    if (mode == QLatin1String("lastDownloaded")) {
        if (a.downloadedAt != b.downloadedAt) {
            // Not downloaded (0) last
            return a.downloadedAt > b.downloadedAt;
        }
        return a.published != b.published ? a.published > b.published : a.id < b.id;
    }
    // byPodcast
    const int c = QString::compare(a.podcastTitle, b.podcastTitle, Qt::CaseInsensitive);
    if (c != 0) {
        return c < 0;
    }
    if (a.podcastId != b.podcastId) {
        return a.podcastId < b.podcastId;
    }
    return inPodcastOrder(a, b);
}

} // namespace

void AppCore::queueSort(const QString &mode, bool keep)
{
    static const QStringList modes = QStringList() << QStringLiteral("oldest") << QStringLiteral("newest")
            << QStringLiteral("shortest") << QStringLiteral("lastDownloaded") << QStringLiteral("byPodcast")
            << QStringLiteral("smartShuffle");
    if (!modes.contains(mode)) {
        return;
    }
    m_database->setSetting(QStringLiteral("playlist.sortMode"), mode);
    m_database->setSetting(QStringLiteral("playlist.sortKeep"), keep ? QStringLiteral("1") : QStringLiteral("0"));

    const QList<qint64> ids = m_database->queueIds();
    QList<SortItem> items;
    QList<bool> fixed;
    QHash<qint64, bool> oldestFirst;
    for (int i = 0; i < ids.size(); ++i) {
        const EpisodeRow e = m_database->episode(ids.at(i));
        SortItem item;
        item.id = ids.at(i);
        item.podcastId = e.podcastId;
        item.podcastTitle = e.podcastTitle;
        item.published = e.published;
        item.duration = e.duration;
        item.downloadedAt = e.downloadedAt;
        if (!oldestFirst.contains(e.podcastId)) {
            oldestFirst.insert(e.podcastId, m_database->setting(
                    QStringLiteral("podcast.%1.oldestFirst").arg(e.podcastId), QStringLiteral("0")) == QLatin1String("1"));
        }
        item.oldestFirst = oldestFirst.value(e.podcastId);
        items.append(item);
        // The episode playing and pinned ones keep their place
        fixed.append(int(ids.at(i)) == m_currentEpisode || m_pinned.contains(int(ids.at(i))));
    }

    QList<SortItem> movable;
    for (int i = 0; i < items.size(); ++i) {
        if (!fixed.at(i)) {
            movable.append(items.at(i));
        }
    }
    if (mode == QLatin1String("smartShuffle")) {
        // Per podcast in its own order; every episode gets a place on the
        // way 0..1 (its rank plus a random part, divided by the number of
        // the podcast's episodes): the podcasts alternate, the order within
        // a podcast stays
        static bool seeded = false;
        if (!seeded) {
            qsrand(uint(QDateTime::currentMSecsSinceEpoch() & 0x7fffffff));
            seeded = true;
        }
        QHash<qint64, QList<SortItem> > groups;
        for (int i = 0; i < movable.size(); ++i) {
            groups[movable.at(i).podcastId].append(movable.at(i));
        }
        QList<QPair<double, SortItem> > keyed;
        for (QHash<qint64, QList<SortItem> >::iterator g = groups.begin(); g != groups.end(); ++g) {
            std::sort(g.value().begin(), g.value().end(), inPodcastOrder);
            const int n = g.value().size();
            for (int i = 0; i < n; ++i) {
                const double r = qrand() / (double(RAND_MAX) + 1.0);
                keyed.append(qMakePair((i + r) / n, g.value().at(i)));
            }
        }
        std::stable_sort(keyed.begin(), keyed.end(), [](const QPair<double, SortItem> &a, const QPair<double, SortItem> &b) {
            return a.first != b.first ? a.first < b.first : a.second.id < b.second.id;
        });
        movable.clear();
        for (int i = 0; i < keyed.size(); ++i) {
            movable.append(keyed.at(i).second);
        }
    } else {
        std::stable_sort(movable.begin(), movable.end(), [&mode](const SortItem &a, const SortItem &b) {
            return sortLess(mode, a, b);
        });
    }

    QList<qint64> result;
    int next = 0;
    for (int i = 0; i < ids.size(); ++i) {
        result.append(fixed.at(i) ? ids.at(i) : movable.at(next++).id);
    }
    m_database->queueSetOrder(result);
    emit queueChanged();
}

// (275) "Sorted: keep": an episode added to the end goes where the chosen
// sorting wants it. Never before the episode playing (the playlist goes on
// after its place). Smart Shuffle: behind the last episode of its podcast
// already in the list (before the first when the podcast is newest first),
// without one at the end.
void AppCore::placeSorted(int episodeId)
{
    if (m_database->setting(QStringLiteral("playlist.sortKeep"), QStringLiteral("0")) != QLatin1String("1")) {
        return;
    }
    const QString mode = m_database->setting(QStringLiteral("playlist.sortMode"));
    if (mode.isEmpty()) {
        return;
    }
    QList<qint64> ids = m_database->queueIds();
    if (!ids.removeOne(episodeId)) {
        return;
    }
    auto itemOf = [this](qint64 id) {
        const EpisodeRow e = m_database->episode(id);
        SortItem item;
        item.id = id;
        item.podcastId = e.podcastId;
        item.podcastTitle = e.podcastTitle;
        item.published = e.published;
        item.duration = e.duration;
        item.downloadedAt = e.downloadedAt;
        item.oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(e.podcastId),
                                               QStringLiteral("0")) == QLatin1String("1");
        return item;
    };
    // (288) With pins in the list: one sorting around them (the pins keep
    // their places; a single move would push them). Not Smart Shuffle:
    // that sorting is random.
    if (!m_pinned.isEmpty() && mode != QLatin1String("smartShuffle")) {
        queueSort(mode, true);
        return;
    }
    const SortItem added = itemOf(episodeId);
    const int current = m_currentEpisode > 0 ? ids.indexOf(m_currentEpisode) : -1;
    int minIndex = current >= 0 ? current + 1 : 0;
    // (337, review 16 Nov) Never before the pins right behind the episode
    // playing ("Play next" with Keep sorted pins): they come next
    while (minIndex < ids.size() && m_pinned.contains(int(ids.at(minIndex)))) {
        ++minIndex;
    }
    int target = ids.size();
    if (mode == QLatin1String("smartShuffle")) {
        // (288) By the podcast's own order and the dates: before the first
        // episode of the podcast that comes after the new one, else behind
        // the last one (an older episode used to land before newer ones)
        int firstAfter = -1;
        int last = -1;
        for (int i = minIndex; i < ids.size(); ++i) {
            if (m_database->episode(ids.at(i)).podcastId == added.podcastId) {
                last = i;
                if (firstAfter < 0 && inPodcastOrder(added, itemOf(ids.at(i)))) {
                    firstAfter = i;
                }
            }
        }
        if (last >= 0) {
            target = firstAfter >= 0 ? firstAfter : last + 1;
        }
    } else {
        for (int i = minIndex; i < ids.size(); ++i) {
            if (sortLess(mode, added, itemOf(ids.at(i)))) {
                target = i;
                break;
            }
        }
    }
    // (336) Also to the end (an unpinned episode whose podcast has no other
    // episode in the list goes there, as a new one would); an episode added
    // at the end is there already
    if (target != m_database->queueIds().indexOf(episodeId)) {
        m_database->queueMove(episodeId, target);
    }
}

int AppCore::nextEpisodeOfPodcast(int finishedEpisodeId) const
{
    const EpisodeRow finished = m_database->episode(finishedEpisodeId);
    if (!finished.id || finished.podcastId <= 0) {
        return -1;
    }
    const bool oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(finished.podcastId),
                                                 QStringLiteral("0")) == QLatin1String("1");
    const QList<EpisodeRow> episodes = m_database->episodes(finished.podcastId, -1, Database::AllEpisodes, oldestFirst);
    bool after = false;
    for (int i = 0; i < episodes.size(); ++i) {
        if (episodes.at(i).id == finished.id) {
            after = true;
        } else if (after && !episodes.at(i).played && !episodes.at(i).url.isEmpty()) {
            return int(episodes.at(i).id);
        }
    }
    return -1;
}

bool AppCore::isPinned(int episodeId) const
{
    return m_pinned.contains(episodeId);
}

bool AppCore::isAutoAdded(int episodeId) const
{
    return m_autoAdded.contains(episodeId);
}

int AppCore::pinnedCount() const
{
    return m_pinned.size();
}

void AppCore::queueClearPins()
{
    m_database->queueClearPins();
    m_pinned.clear();
    // (288) Like releasing one pin: with "Sorted: keep" the episodes go to
    // their places at once
    if (keepSorted()) {
        queueSort(m_database->setting(QStringLiteral("playlist.sortMode")), true);
    } else {
        emit queueChanged();
    }
}

// (275) Auto-Add per podcast ("podcast.<id>.autoAdd": empty = off, "end",
// "top" or (277) "sorted"; "podcast.<id>.autoAddLimit": "all", "1", "3", "5" - at most that
// many of the new episodes per update, the newest ones). Not for the first
// update of a subscription (the whole back catalog), archived or not
// subscribed podcasts. Played episodes and those already in it are skipped.
void AppCore::autoAddToPlaylist(qint64 podcastId, int newEpisodes)
{
    const QString where = m_database->setting(QStringLiteral("podcast.%1.autoAdd").arg(podcastId));
    if (where != QLatin1String("end") && where != QLatin1String("top") && where != QLatin1String("sorted")) {
        return;
    }
    const PodcastRow podcast = m_database->podcast(podcastId);
    if (!podcast.id || podcast.archived || podcast.provisional || newEpisodes >= podcast.episodeCount) {
        return;
    }
    const QString limitText = m_database->setting(QStringLiteral("podcast.%1.autoAddLimit").arg(podcastId),
                                                  QStringLiteral("3"));
    const int limit = limitText == QLatin1String("all") ? newEpisodes : qBound(1, limitText.toInt(), newEpisodes);
    const bool oldestFirst = m_database->setting(QStringLiteral("podcast.%1.oldestFirst").arg(podcastId),
                                                 QStringLiteral("0")) == QLatin1String("1");
    // (288) The episodes of THIS update (new rows have the highest ids), of
    // those the newest ones up to the limit. Before it took the newest
    // "new" episodes of the podcast: older ones found in a later update
    // were skipped as long as newer unplayed ones were there.
    QList<EpisodeRow> episodes = updateEpisodes(podcastId, newEpisodes);
    // (289) Heard ones (on another device too) and ones already in the list
    // are left out BEFORE the limit counts - before, a heard newest episode
    // used up the only place
    for (int i = episodes.size() - 1; i >= 0; --i) {
        if (episodes.at(i).played || m_queue.contains(int(episodes.at(i).id))) {
            episodes.removeAt(i);
        }
    }
    std::sort(episodes.begin(), episodes.end(), [](const EpisodeRow &a, const EpisodeRow &b) {
        return a.published != b.published ? a.published > b.published : a.id > b.id;
    });
    while (episodes.size() > limit) {
        episodes.removeLast();
    }
    // Added in the podcast's own order
    std::stable_sort(episodes.begin(), episodes.end(), [oldestFirst](const EpisodeRow &a, const EpisodeRow &b) {
        return oldestFirst ? a.published < b.published : a.published > b.published;
    });
    if (where == QLatin1String("top")) {
        // One after the other to the front: the last one added ends up first
        std::reverse(episodes.begin(), episodes.end());
    }
    bool any = false;
    for (int i = 0; i < episodes.size(); ++i) {
        const EpisodeRow &e = episodes.at(i);
        if (e.played || m_queue.contains(int(e.id))) {
            continue;
        }
        // (277) "end" and "top" are literal; "sorted" follows the sorting of
        // the playlist (with "Sorted: keep"; otherwise = end)
        queueAddImpl(int(e.id), where == QLatin1String("top"), where == QLatin1String("sorted"), false, true);
        any = true;
    }
    if (any) {
        emit dataChanged();
    }
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
    // (289) The whole podcast must fit (what is not on the phone yet)
    {
        qint64 needed = 0;
        const QList<EpisodeRow> all = m_database->episodes(podcastId);
        for (int i = 0; i < all.size(); ++i) {
            if (!all.at(i).downloaded && !all.at(i).url.isEmpty()) {
                needed += qMax<qint64>(0, all.at(i).fileSize);
            }
        }
        if (!storageAllows(needed, false)) {
            return;
        }
    }
    m_archiver->setArchiveRoot(archiveRoot());
    m_archiver->setVideoRoot(videoRoot());
    m_archiver->setDownloadDirectory(m_downloads->downloadDirectory());
    m_downloads->setVideoRoot(videoRoot());
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
    // (289) Remember the folder that was in use: files kept there stay protected
    const QString before = archiveRoot();
    m_database->setSetting(QStringLiteral("archive.dir"), folder);
    if (archiveRoot() != before) {
        QStringList old = oldArchiveRoots();
        old.removeAll(archiveRoot());
        if (!old.contains(before)) {
            old.append(before);
        }
        m_database->setSetting(QStringLiteral("archive.oldRoots"), old.join(QLatin1Char('\n')));
    }
    m_archiver->setArchiveRoot(archiveRoot());
    m_archiver->setVideoRoot(videoRoot());
    m_archiver->setDownloadDirectory(m_downloads->downloadDirectory());
    m_downloads->setVideoRoot(videoRoot());
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

// (320, code review) The pending change of episodes.json of one podcast is
// written now (before the podcast and its rows go: afterwards there is
// nothing to write it from)
void AppCore::flushArchiveMetadata(qint64 podcastId)
{
    if (!m_metadataDirty.contains(podcastId)) {
        return;
    }
    if (m_archiver->isRunning() && podcastId == m_archivingId) {
        return;
    }
    QString error;
    if (!m_archiver->writeMetadata(podcastId, &error)) {
        qWarning("[AppCore] episodes.json: %s", qPrintable(error));
    }
    m_metadataDirty.remove(podcastId);
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
    emitMessage(added > 0 ? tr("%n podcasts added – loading the episodes", "", added)
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
    // (320, code review) Before anything is deleted: the file must be an SQLite
    // database, and the current one is kept (Settings -> Backup: undo) -
    // before, a wrong file or a failed copy left an empty database
    {
        QFile probe(path);
        const bool header = probe.open(QIODevice::ReadOnly) && probe.read(16) == QByteArray("SQLite format 3\0", 16);
        if (!header) {
            emitMessage(tr("Import failed: %1").arg(tr("not a database file")));
            return;
        }
    }
    QDir().mkpath(backupDirectory());
    const QString undo = backupDirectory() + QStringLiteral("/before-restore.db");
    if (QFileInfo(path).absoluteFilePath() != QFileInfo(undo).absoluteFilePath() && !copyDatabaseTo(undo)) {
        emitMessage(tr("Backup failed: %1").arg(undo));
        return;
    }
    writeArchiveMetadata();
    const QString target = databasePath();
    m_database->close();
    QFile::remove(target);
    QFile::remove(target + QStringLiteral("-wal"));
    QFile::remove(target + QStringLiteral("-shm"));
    bool copied = QFile::copy(path, target);
    if (copied) {
        QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
    }
    openDatabase();
    if ((!copied || !m_databaseError.isEmpty()) && QFile::exists(undo)
            && QFileInfo(path).absoluteFilePath() != QFileInfo(undo).absoluteFilePath()) {
        // The new one does not work (copy failed, schema too new): the old one back
        const QString reason = m_databaseError.isEmpty() ? path : m_databaseError;
        m_database->close();
        QFile::remove(target);
        QFile::remove(target + QStringLiteral("-wal"));
        QFile::remove(target + QStringLiteral("-shm"));
        if (QFile::copy(undo, target)) {
            QFile::setPermissions(target, QFile::ReadOwner | QFile::WriteOwner);
        }
        openDatabase();
        reloadQueue();
        emit settingsChanged();
        emit dataChanged();
        emit queueChanged();
        emitMessage(tr("Import failed: %1").arg(reason) + QLatin1Char(' ') + tr("The previous data is back."));
        return;
    }
    // Folders of the other device (e.g. /tmp on the desktop) do not exist
    // here: back to the defaults - not for a backup of this phone (93)
    if (!keepFolders) {
        m_database->setSetting(QStringLiteral("archive.dir"), QString());
        m_database->setSetting(QStringLiteral("downloads.dir"), QString());
    }
    m_archiver->setArchiveRoot(archiveRoot());
    m_archiver->setVideoRoot(videoRoot());
    m_archiver->setDownloadDirectory(m_downloads->downloadDirectory());
    m_downloads->setVideoRoot(videoRoot());
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

// (294) --- HTML export ----------------------------------------------------

static QString clockText(qint64 seconds)
{
    const qint64 h = seconds / 3600;
    const qint64 m = (seconds / 60) % 60;
    const qint64 sec = seconds % 60;
    return h > 0 ? QStringLiteral("%1:%2:%3").arg(h).arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'))
                 : QStringLiteral("%1:%2").arg(m, 2, 10, QLatin1Char('0')).arg(sec, 2, 10, QLatin1Char('0'));
}

// The export works in slices on the main thread: the database connection
// belongs to it, so a second thread would need its own connection. After each
// ~40 ms the event loop runs again, the page shows the progress.
struct AppCore::HtmlExportJob
{
    QList<QPair<int, PodcastRow> > plan;    // folder index, podcast
    QList<HtmlExport::Folder> folders;      // by folder index, "" = other podcasts (last)
    int skipped = 0;                        // private feeds left out
    int excluded = 0;                       // (348) left out by choice
    int next = 0;
    bool cancelled = false;
};

void AppCore::deleteHtmlJob()
{
    delete m_htmlJob;
    m_htmlJob = nullptr;
}

namespace {
struct HtmlNeeds
{
    QList<qint64> ids;      // episodes whose chapters would be loaded (for the chosen mode)
    int last5 = 0;          // ... if only the last 5 episodes are exported
    int all = 0;            // ... if all episodes are
    int episodes = 0;
};

HtmlNeeds htmlNeeds(const Database *db, const QList<QPair<int, PodcastRow> > &plan, int mode)
{
    HtmlNeeds needs;
    for (int k = 0; k < plan.size(); ++k) {
        const QList<EpisodeRow> episodes = db->episodes(plan.at(k).second.id);   // newest first
        needs.episodes += episodes.size();
        for (int i = 0; i < episodes.size(); ++i) {
            const EpisodeRow &e = episodes.at(i);
            if (e.chapterCount > 0 || e.chaptersUrl.isEmpty()) {
                continue;
            }
            ++needs.all;
            if (i < 5) {
                ++needs.last5;
            }
            if (mode >= 2 || (mode == 1 && i < 5)) {
                needs.ids.append(e.id);
            }
        }
    }
    return needs;
}
} // namespace

// Which podcasts, in which folders: folders alphabetically, "Other podcasts"
// last, podcasts by title; provisional ones and feeds with a login or a key
// in the address stay out (a key must not travel in a page)
void AppCore::planHtmlExport(const QVariantList &folderIds, const QVariantList &excludedIds, HtmlExportJob *job) const
{
    QSet<int> wanted;
    for (int i = 0; i < folderIds.size(); ++i) {
        wanted.insert(folderIds.at(i).toInt());
    }
    QSet<qint64> excluded;
    for (int i = 0; i < excludedIds.size(); ++i) {
        excluded.insert(excludedIds.at(i).toLongLong());
    }
    const QList<PodcastRow> podcasts = m_database->podcasts();
    const QList<FolderRow> folders = m_database->folders();
    QHash<qint64, QString> folderNames;
    for (int f = 0; f < folders.size(); ++f) {
        folderNames.insert(folders.at(f).id, folders.at(f).name);
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    collator.setNumericMode(true);
    QList<FolderRow> sortedFolders = folders;
    std::sort(sortedFolders.begin(), sortedFolders.end(), [&collator](const FolderRow &a, const FolderRow &b) {
        return collator.compare(a.name, b.name) < 0;
    });
    QList<PodcastRow> sortedPodcasts = podcasts;
    std::sort(sortedPodcasts.begin(), sortedPodcasts.end(), [&collator](const PodcastRow &a, const PodcastRow &b) {
        return collator.compare(a.title, b.title) < 0;
    });
    for (int f = 0; f <= sortedFolders.size(); ++f) {
        const bool other = f == sortedFolders.size();
        const qint64 folderId = other ? 0 : sortedFolders.at(f).id;
        HtmlExport::Folder folder;
        folder.name = other ? QString() : sortedFolders.at(f).name;
        job->folders.append(folder);
        for (int i = 0; i < sortedPodcasts.size(); ++i) {
            const PodcastRow &p = sortedPodcasts.at(i);
            if (p.folderId != folderId && !(other && !folderNames.contains(p.folderId))) {
                continue;
            }
            const int key = folderNames.contains(p.folderId) ? int(p.folderId) : 0;
            if ((!wanted.isEmpty() && !wanted.contains(key)) || p.provisional) {
                continue;
            }
            if (!p.authUser.isEmpty() || UrlMask::maskText(p.url) != p.url) {
                ++job->skipped;
                continue;
            }
            // (348) Left out on the page "Podcasts": not named anywhere on
            // the page (statistics names only podcasts of the plan, 333)
            if (excluded.contains(p.id)) {
                ++job->excluded;
                continue;
            }
            job->plan.append(qMakePair(f, p));
        }
    }
}

// One podcast with what the options ask for (covers from the cache only)
HtmlExport::Podcast AppCore::buildHtmlPodcast(const PodcastRow &p, const QVariantMap &options) const
{
    // episodes: 0 none, 1 the last 5, 2 all (5 shown, the rest folded)
    const int episodeMode = options.value(QStringLiteral("episodes")).toInt();
    const bool withEpisodes = episodeMode > 0;
    const bool withChapters = withEpisodes && options.value(QStringLiteral("chapters")).toBool();
    const bool withBookmarks = withEpisodes && options.value(QStringLiteral("bookmarks")).toBool();
    const bool withNotes = withEpisodes && options.value(QStringLiteral("episodeNotes"), true).toBool();
    // statistics: 0 none, 1 totals, 2 everything (also the time per podcast)
    const bool statistics = options.value(QStringLiteral("statistics")).toInt() >= 2;
    HtmlExport::Podcast h;
    h.title = p.title;
    h.author = p.author;
    h.language = languageName(p.language);
    h.categories = p.categories;
    h.description = p.description;
    h.link = p.link;
    h.feedUrl = p.url;
    const QList<EpisodeRow> episodes = m_database->episodes(p.id);
    h.episodeCount = episodes.size();
    QList<qint64> dates;
    int played = 0;
    for (const EpisodeRow &e : episodes) {
        if (e.published > 0) {
            dates.append(e.published);
        }
        played += e.played ? 1 : 0;
    }
    h.rhythm = rhythmOf(dates).value(QStringLiteral("rhythm")).toString();
    // (334) A podcast hidden from the statistics shows no listening time
    if (statistics && !m_database->statsHidden().contains(p.id)) {
        h.listenedSeconds = p.listenedSeconds;
        h.listenedEpisodes = played;
    }
    if (withEpisodes) {
        const int count = episodeMode >= 2 ? episodes.size() : qMin(5, int(episodes.size()));
        for (int k = 0; k < count; ++k) {
            const EpisodeRow &e = episodes.at(k);
            HtmlExport::Episode he;
            he.title = e.title;
            he.date = e.published > 0
                    ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(e.published * 1000).date(), QLocale::ShortFormat)
                    : QString();
            he.duration = e.duration > 0 ? tr("%1 min").arg(e.duration / 60) : QString();
            he.played = e.played;
            if (withNotes) {
                he.description = m_database->episodeDescription(e.id, false);
            }
            if (withChapters) {
                const QList<ParsedChapter> chapters = m_database->chapters(e.id);
                for (const ParsedChapter &c : chapters) {
                    he.chapters << clockText(c.startMs / 1000) + QLatin1Char(' ') + c.title;
                }
            }
            if (withBookmarks && e.bookmarkCount > 0) {
                const QList<BookmarkRow> bookmarks = m_database->bookmarks(e.id);
                for (const BookmarkRow &b : bookmarks) {
                    QString text = clockText(b.position);
                    if (!b.title.isEmpty()) {
                        text += QLatin1Char(' ') + b.title;
                    }
                    if (!b.note.isEmpty()) {
                        text += QStringLiteral(" – ") + b.note;
                    }
                    he.bookmarks << text;
                }
            }
            h.episodes.append(he);
        }
    }
    // Cover: only from the cache (no loading here), 96 px JPEG
    if (!p.coverUrl.isEmpty()) {
        const QString local = QUrl(ImageCache::instance()->url(p.coverUrl)).toLocalFile();
        QImage image(local);
        if (!image.isNull()) {
            image = image.scaled(96, 96, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (image.save(&buffer, "JPEG", 80)) {
                h.coverDataUri = QStringLiteral("data:image/jpeg;base64,")
                        + QString::fromLatin1(bytes.toBase64());
            }
        }
    }
    return h;
}

QVariantList AppCore::htmlExportPodcasts(const QVariantList &folderIds) const
{
    HtmlExportJob job;
    planHtmlExport(folderIds, QVariantList(), &job);
    QVariantList result;
    for (int i = 0; i < job.plan.size(); ++i) {
        const PodcastRow &p = job.plan.at(i).second;
        QVariantMap map;
        map.insert(QStringLiteral("podcastId"), int(p.id));
        map.insert(QStringLiteral("title"), p.title);
        map.insert(QStringLiteral("coverUrl"), p.coverUrl);
        result.append(map);
    }
    return result;
}

QVariantMap AppCore::htmlExportPreview(const QVariantList &folderIds, const QVariantList &excluded) const
{
    HtmlExportJob job;
    planHtmlExport(folderIds, excluded, &job);
    const HtmlNeeds needs = htmlNeeds(m_database, job.plan, 0);
    QVariantMap map;
    map.insert(QStringLiteral("podcasts"), job.plan.size());
    map.insert(QStringLiteral("skipped"), job.skipped);
    map.insert(QStringLiteral("excluded"), job.excluded);
    map.insert(QStringLiteral("chapterEpisodes"), needs.last5);       // last 5
    map.insert(QStringLiteral("chapterEpisodesAll"), needs.all);
    map.insert(QStringLiteral("episodesAll"), needs.episodes);
    return map;
}

void AppCore::startHtmlExport(const QVariantList &folderIds, const QVariantMap &options)
{
    if (m_htmlChapters || m_htmlJob) {
        return;                 // one at a time
    }
    m_htmlFolders = folderIds;
    m_htmlOptions = options;
    m_htmlDone = 0;
    m_htmlTotal = 0;
    const int mode = options.value(QStringLiteral("episodes")).toInt();
    if (mode > 0 && options.value(QStringLiteral("chapters")).toBool()) {
        HtmlExportJob job;
        planHtmlExport(folderIds, options.value(QStringLiteral("excluded")).toList(), &job);
        const HtmlNeeds needs = htmlNeeds(m_database, job.plan, mode);
        if (!needs.ids.isEmpty()) {
            m_htmlTotal = needs.ids.size();
            m_htmlChapters = new ChaptersLoader(m_database, this);
            const auto step = [this]() {
                ++m_htmlDone;
                emit htmlExportProgress(m_htmlDone, m_htmlTotal);
            };
            connect(m_htmlChapters, &ChaptersLoader::loaded, this, step);
            connect(m_htmlChapters, &ChaptersLoader::failed, this, step);
            connect(m_htmlChapters, &ChaptersLoader::finished, this, [this]() { finishHtmlExport(false); });
            emit htmlExportProgress(0, m_htmlTotal);
            m_htmlChapters->load(needs.ids);
            return;
        }
    }
    finishHtmlExport(false);
}

void AppCore::fillHtmlStatistics(HtmlExport::Statistics *st, int level, const QHash<qint64, PodcastRow> &shared) const
{
    const ListeningSummary sum = m_database->listeningSummary();
    const auto time = [](qint64 seconds) {
        // (326, blind test) Rounded like the app (TimeFormat.length), not cut
        // off: the export said "5 h 3 min" where the app says "5 h 4 min"
        const qint64 minutes = (seconds + 30) / 60;
        return minutes < 60 ? tr("%1 min").arg(minutes)
                            : (minutes % 60 == 0 ? tr("%1 h").arg(minutes / 60)
                                                 : tr("%1 h %2 min").arg(minutes / 60).arg(minutes % 60));
    };
    st->level = level;
    const QDate first = QDate::fromString(QString::number(sum.firstDay), QStringLiteral("yyyyMMdd"));
    st->title = first.isValid() ? tr("Listening statistics (since %1)").arg(QLocale().toString(first, QLocale::ShortFormat))
                                : tr("Listening statistics");
    st->total = time(sum.seconds);
    st->totalLabel = tr("Total listening time");
    // (321, code review) The same figure as on the statistics page (sum per row, not the difference of the sums)
    st->saved = time(m_database->listeningTotals(0).savedSeconds);
    st->savedLabel = tr("Time saved");
    st->episodes = QString::number(sum.episodes);
    st->episodesLabel = tr("Episodes listened to");
    st->perDay = time(sum.days > 0 ? sum.seconds / sum.days : 0);
    st->perDayLabel = tr("Per listening day");
    st->streak = QString::number(sum.longestStreak);
    st->streakLabel = tr("Days in a row (record, all time)");
    // (297) Explanations, as on the statistics page
    st->totalsNote = tr("Listening time: the time actually listened. Time saved: how much less time was needed by playing faster. Episodes: listened to for at least one minute.");
    st->streakNote = tr("Days in a row: consecutive days on which something was listened to.");
    st->topTitle = tr("Top podcasts");
    // (333, code review "Downloads 4") Podcast names and categories only from
    // the podcasts on this page: the log also holds podcasts listened to
    // without a subscription, unsubscribed ones, feeds with a login and
    // podcasts of folders that were not chosen. The totals stay for all.
    QList<ListeningRank> sharedRanking;
    {
        const QList<ListeningRank> everyone = m_database->listeningRanking(0, 100000);
        for (int i = 0; i < everyone.size(); ++i) {
            if (shared.contains(everyone.at(i).podcastId) && everyone.at(i).seconds > 0) {
                sharedRanking.append(everyone.at(i));
            }
        }
    }
    const qint64 most = sharedRanking.isEmpty() ? 1 : qMax<qint64>(1, sharedRanking.first().seconds);
    for (int i = 0; i < sharedRanking.size() && i < 5; ++i) {
        HtmlExport::Bar bar;
        bar.label = shared.value(sharedRanking.at(i).podcastId).title;
        bar.value = time(sharedRanking.at(i).seconds);
        bar.share = double(sharedRanking.at(i).seconds) / double(most);
        st->top.append(bar);
    }
    st->weekdaysTitle = tr("Weekdays");
    qint64 maxDay = 1;
    for (int i = 0; i < 7; ++i) {
        maxDay = qMax(maxDay, sum.weekdays[i]);
    }
    for (int i = 0; i < 7; ++i) {
        HtmlExport::Bar bar;
        bar.label = QLocale().dayName(i + 1, QLocale::ShortFormat);
        bar.value = time(sum.weekdays[i]);
        bar.share = double(sum.weekdays[i]) / double(maxDay);
        st->weekdays.append(bar);
    }
    st->note = tr("Counts only what was played in Sailcast on this device");
    st->topNote = tr("Only the podcasts on this page.");
    if (level < 2) {
        return;
    }
    // (297) The parts of the statistics page that make sense on a page that
    // shows the day of the export: course per month, heat map, categories
    // (main category), records - the same numbers as the page ("All")
    const QVariantMap page = listeningStatistics(0);
    st->activeDays = QStringLiteral("%1 %").arg(page.value(QStringLiteral("activePercent")).toInt());
    st->activeDaysLabel = tr("Days with listening");
    // (314) The unit follows the length of the log (day, week, month)
    const QString unit = page.value(QStringLiteral("barUnit")).toString();
    st->monthsTitle = unit == QLatin1String("day") ? tr("Over time, per day")
                    : unit == QLatin1String("week") ? tr("Over time, per week") : tr("Over time, per month");
    const QVariantList months = page.value(QStringLiteral("bars")).toList();
    double maxMonth = 1;
    for (int i = 0; i < months.size(); ++i) {
        maxMonth = qMax(maxMonth, months.at(i).toMap().value(QStringLiteral("seconds")).toDouble());
    }
    for (int i = 0; i < months.size(); ++i) {
        const QVariantMap m = months.at(i).toMap();
        HtmlExport::Bar bar;
        bar.label = m.value(QStringLiteral("label")).toString();
        bar.value = time(qint64(m.value(QStringLiteral("seconds")).toDouble()));
        bar.share = m.value(QStringLiteral("seconds")).toDouble() / maxMonth;
        st->months.append(bar);
    }
    st->heatTitle = tr("Activity in the last year");
    const QVariantList heat = page.value(QStringLiteral("heat")).toList();
    double maxHeat = 1;
    for (int i = 0; i < heat.size(); ++i) {
        maxHeat = qMax(maxHeat, heat.at(i).toDouble());
    }
    for (int i = 0; i < heat.size(); ++i) {
        const double v = heat.at(i).toDouble();
        st->heat.append(v < 0 ? -1 : (v <= 0 ? 0 : qBound(1, int(std::ceil(4.0 * v / maxHeat)), 4)));
    }
    const QVariantList heatMonths = page.value(QStringLiteral("heatMonths")).toList();
    for (int i = 0; i < heatMonths.size(); ++i) {
        const QVariantMap m = heatMonths.at(i).toMap();
        st->heatMonths.append(qMakePair(m.value(QStringLiteral("week")).toInt(), m.value(QStringLiteral("label")).toString()));
    }
    st->categoriesTitle = tr("Categories");
    st->categoriesNote = tr("Each podcast counts only for its first category. Only the podcasts on this page.");
    // (333) Main category per shared podcast, as listeningStatistics does it
    const QList<FyydCategory> categoryTree = m_discoverFyyd->cachedCategories();
    QHash<QString, double> byMain;
    double total = 0;
    for (int i = 0; i < sharedRanking.size(); ++i) {
        const PodcastRow &p = shared[sharedRanking.at(i).podcastId];
        QString top;
        for (int c = 0; c < p.categories.size() && top.isEmpty(); ++c) {
            top = p.categories.at(c).trimmed().section(QLatin1Char('/'), 0, 0);
        }
        const QString label = top.isEmpty() ? tr("No category")
                                            : categoryLabelsIn(QStringList(top), categoryTree).value(0, top);
        byMain[label] += double(sharedRanking.at(i).seconds);
        total += double(sharedRanking.at(i).seconds);
    }
    total = qMax(1.0, total);
    QList<QPair<double, QString>> sortedMain;
    for (QHash<QString, double>::const_iterator it = byMain.constBegin(); it != byMain.constEnd(); ++it) {
        sortedMain.append(qMakePair(it.value(), it.key()));
    }
    std::sort(sortedMain.begin(), sortedMain.end(), [](const QPair<double, QString> &a, const QPair<double, QString> &b) {
        return a.first != b.first ? a.first > b.first : a.second < b.second;
    });
    QVariantList categories;
    for (int i = 0; i < sortedMain.size(); ++i) {
        QVariantMap item;
        item.insert(QStringLiteral("label"), sortedMain.at(i).second);
        item.insert(QStringLiteral("seconds"), sortedMain.at(i).first);
        categories.append(item);
    }
    const double topCategory = categories.isEmpty() ? 1.0
            : qMax(1.0, categories.first().toMap().value(QStringLiteral("seconds")).toDouble());
    for (int i = 0; i < categories.size() && i < 10; ++i) {
        const QVariantMap c = categories.at(i).toMap();
        const double seconds = c.value(QStringLiteral("seconds")).toDouble();
        HtmlExport::Bar bar;
        bar.label = c.value(QStringLiteral("label")).toString();
        bar.value = QStringLiteral("%1 · %2 %").arg(time(qint64(seconds))).arg(qRound(100.0 * seconds / total));
        bar.share = seconds / topCategory;
        st->categories.append(bar);
    }
    st->recordsTitle = tr("Records");
    if (page.value(QStringLiteral("bestDaySeconds")).toDouble() > 0) {
        st->records.append(qMakePair(tr("Longest listening day"),
                                     time(qint64(page.value(QStringLiteral("bestDaySeconds")).toDouble()))
                                     + QStringLiteral(" · ") + page.value(QStringLiteral("bestDay")).toString()));
    }
    // (314) The most listened podcast instead of the "most listened episode";
    // (333) of the podcasts on this page
    if (!sharedRanking.isEmpty()) {
        st->records.append(qMakePair(tr("Most listened podcast"),
                                     shared.value(sharedRanking.first().podcastId).title + QStringLiteral(" · ")
                                     + time(sharedRanking.first().seconds)));
    }
    if (!page.value(QStringLiteral("favouriteWeekday")).toString().isEmpty()) {
        st->records.append(qMakePair(tr("Favourite weekday"), page.value(QStringLiteral("favouriteWeekday")).toString()));
    }
}

void AppCore::cancelHtmlExport()
{
    if (m_htmlChapters) {
        finishHtmlExport(true);
    }
}

void AppCore::finishHtmlExport(bool cancelled)
{
    if (m_htmlChapters) {
        m_htmlChapters->disconnect(this);
        m_htmlChapters->deleteLater();
        m_htmlChapters = nullptr;
    }
    delete m_htmlJob;
    m_htmlJob = new HtmlExportJob;
    m_htmlJob->cancelled = cancelled;
    planHtmlExport(m_htmlFolders, m_htmlOptions.value(QStringLiteral("excluded")).toList(), m_htmlJob);
    emit htmlExportBuilding(0, m_htmlJob->plan.size());
    QTimer::singleShot(0, this, [this]() { stepHtmlExport(); });
}

void AppCore::stepHtmlExport()
{
    HtmlExportJob *job = m_htmlJob;
    if (!job) {
        return;
    }
    QElapsedTimer timer;
    timer.start();
    while (job->next < job->plan.size() && timer.elapsed() < 40) {
        const QPair<int, PodcastRow> &item = job->plan.at(job->next);
        job->folders[item.first].podcasts.append(buildHtmlPodcast(item.second, m_htmlOptions));
        ++job->next;
    }
    emit htmlExportBuilding(job->next, job->plan.size());
    if (job->next < job->plan.size()) {
        QTimer::singleShot(0, this, [this]() { stepHtmlExport(); });
        return;
    }
    writeHtmlExport();
}

void AppCore::writeHtmlExport()
{
    QList<HtmlExport::Folder> folders = m_htmlJob->folders;
    // (333) Names in the statistics only from the podcasts on this page
    QHash<qint64, PodcastRow> shared;
    for (int i = 0; i < m_htmlJob->plan.size(); ++i) {
        shared.insert(m_htmlJob->plan.at(i).second.id, m_htmlJob->plan.at(i).second);
    }
    const int skipped = m_htmlJob->skipped;
    const bool cancelled = m_htmlJob->cancelled;
    delete m_htmlJob;
    m_htmlJob = nullptr;
    int count = 0;
    for (int f = 0; f < folders.size(); ++f) {
        folders[f].countText = tr("%n podcasts", "", folders.at(f).podcasts.size());
        count += folders.at(f).podcasts.size();
    }
    HtmlExport::Options options;
    options.descriptions = m_htmlOptions.value(QStringLiteral("descriptions"), true).toBool();
    const int statsLevel = m_htmlOptions.value(QStringLiteral("statistics")).toInt();
    options.statistics = statsLevel >= 2;
    if (statsLevel > 0) {
        fillHtmlStatistics(&options.stats, statsLevel, shared);
    }
    HtmlExport::Labels &l = options.labels;
    l.lang = QLocale().name().left(2);
    l.title = tr("My podcasts");
    int named = 0;
    for (int f = 0; f < folders.size(); ++f) {
        named += (!folders.at(f).name.isEmpty() && !folders.at(f).podcasts.isEmpty()) ? 1 : 0;
    }
    l.subtitle = (named > 0 ? tr("%n podcasts", "", count) + QStringLiteral(" · ") + tr("%n folders", "", named)
                            : tr("%n podcasts", "", count))
            + QStringLiteral(" · ") + tr("exported %1").arg(QLocale().toString(QDate::currentDate(), QLocale::ShortFormat));
    if (skipped > 0) {
        l.privateNote = tr("%n private feeds (with a password or a personal link) are not part of this page.", "", skipped);
    }
    l.intro = tr("Tap a folder or a podcast to see details and the latest episodes.");
    l.introPoints << tr("Subscribe: opens the podcast in your podcast app, if it supports such links")
                  << tr("Feed address: link to the RSS feed, to paste into any podcast app")
                  << tr("Save list: a file (OPML) that almost every podcast app can import – all podcasts, one folder or the ones you tick")
                  << tr("Search: looks for the podcast by name on fyyd, Podcast Index, Spotify or Apple Podcasts, where it is listed");
    l.opmlAll = tr("Save all as a list (OPML)");
    l.opml = tr("Save list");
    l.otherPodcasts = tr("Other podcasts");
    l.category = tr("Category");
    l.language = tr("Language");
    l.episodes = tr("Episodes");
    l.rhythm = tr("Rhythm");
    l.listened = tr("Listened");
    l.subscribe = tr("Subscribe");
    l.feed = tr("Feed address");
    l.website = tr("Website");
    l.lastEpisodes = tr("Last episodes");
    l.moreEpisodes = tr("More episodes (%1)");
    l.fyyd = tr("fyyd");
    l.podcastIndex = tr("Podcast Index");
    l.spotify = tr("Spotify");
    l.apple = tr("Apple Podcasts");
    l.sourceCode = tr("Sailcast on GitHub");
    l.sailfishOs = tr("More about Sailfish OS");
    l.promo = tr("Sailcast is a free, open-source podcast app for Sailfish OS, a Linux-based mobile operating system. It sorts podcasts into folders and offers chapters, bookmarks, a playlist and synchronisation with gpodder.net and Nextcloud.");
    l.about = tr("The podcast search uses fyyd and Podcast Index.");
    l.chapters = tr("Chapters");
    l.bookmarks = tr("Bookmarks");
    l.showNotes = tr("Description");
    l.selected = tr("%1 selected");
    l.all = tr("All");
    l.none = tr("Deselect");
    l.saveSelection = tr("Save selection as a list");
    l.madeWith = tr("Made with Sailcast for Sailfish OS");
    l.hour = tr("h");
    l.minute = tr("min");
    {
        QImage icon(QStringLiteral("/usr/share/icons/hicolor/86x86/apps/harbour-sailcast.png"));
        if (!icon.isNull()) {
            icon = icon.scaled(64, 64, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            QByteArray bytes;
            QBuffer buffer(&bytes);
            buffer.open(QIODevice::WriteOnly);
            if (icon.save(&buffer, "PNG")) {
                options.iconDataUri = QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
            }
        }
    }
    const QString html = HtmlExport::build(folders, options);
    const QString path = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation)
            + QStringLiteral("/sailcast-subscriptions-%1.html").arg(today());
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(html.toUtf8()) < 0 || !file.commit()) {
        emitMessage(tr("Export failed: %1").arg(path));
        emit htmlExportFinished(QString(), cancelled);
        return;
    }
    if (skipped > 0) {
        emitMessage(tr("%n private feeds (with a password or a personal link) were left out", "", skipped));
    }
    emit htmlExportFinished(path, cancelled);
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
        emitMessage(nextcloudBackupPossible() ? tr("Backup saved – copying it to the Nextcloud …")
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
                                                  << QStringLiteral("before-restore.db")
                                                  << QStringLiteral("before-merge.db"),
                                                  QDir::Files, QDir::Name | QDir::Reversed);
    for (int i = 0; i < files.size(); ++i) {
        const QFileInfo &f = files.at(i);
        QVariantMap map;
        map.insert(QStringLiteral("path"), f.absoluteFilePath());
        map.insert(QStringLiteral("name"), f.fileName());
        map.insert(QStringLiteral("modified"), double(f.lastModified().toMSecsSinceEpoch()));
        map.insert(QStringLiteral("size"), double(f.size()));
        map.insert(QStringLiteral("undo"), f.fileName() == QLatin1String("before-restore.db")
                                           || f.fileName() == QLatin1String("before-merge.db"));
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
    afterMarkedPlayed();
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
        // (348) Loosely: "Sud Stream" finds "Sudstream"
        // (353) Every word in one of the fields, umlauts as ae/oe/ue/ss
        if (Database::matchesSearch(QStringList { p.title, p.author, p.feedTitle }, needle)) {
            QVariantMap map;
            map.insert(QStringLiteral("podcastId"), int(p.id));
            map.insert(QStringLiteral("title"), p.title);
            map.insert(QStringLiteral("author"), p.author);
            map.insert(QStringLiteral("coverUrl"), p.coverUrl);
            map.insert(QStringLiteral("unplayed"), p.unplayedCount);    // (348) podcast menu
            podcasts.append(map);
        }
    }
    result.insert(QStringLiteral("podcasts"), podcasts);

    // Episodes: titles over all podcasts
    QVariantList episodes;
    // (353) Word by word over the title and the podcast name ("ct4004 iphone")
    const QList<EpisodeRow> found = m_database->searchEpisodes(needle, 20);
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
        // (348) Found loosely in the title already: not again under "In descriptions"
        QSet<qint64> inTitle;
        for (int i = 0; i < found.size(); ++i) {
            inTitle.insert(found.at(i).id);
        }
        for (int i = 0; i < hits.size(); ++i) {
            const EpisodeRow &e = hits.at(i).first;
            if (inTitle.contains(e.id)) {
                continue;
            }
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
        if (Database::matchesSearch(QStringList { b.value(QStringLiteral("title")).toString(),
                                                  b.value(QStringLiteral("note")).toString(),
                                                  b.value(QStringLiteral("episodeTitle")).toString(),
                                                  b.value(QStringLiteral("podcastTitle")).toString() }, needle)) {
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
    afterMarkedPlayed();
    if (podcastId > 0 && !m_database->podcast(podcastId).archiveFolder.isEmpty()) {
        m_metadataDirty.insert(podcastId);
        m_metadataTimer->start();
    }
    emit dataChanged();
    emitMessage(tr("%n episodes marked as played", "", count));
}

// --- App update (350) -------------------------------------------------------
// The GitHub build looks for a newer release once a day (setting
// "update.check", on by default) and offers the RPM for this architecture;
// the Jolla Store build (SAILCAST_STORE) has none of this. Kept in settings:
// update.json (the last answer of releases/latest), update.etag,
// update.lastCheck (seconds), update.dismissed (version whose hint line on
// the Podcasts page was closed with x).

bool AppCore::appUpdateSupported() const
{
#ifdef SAILCAST_STORE
    return false;
#else
    return true;
#endif
}

bool AppCore::appUpdateEnabled() const
{
    return appUpdateSupported() && m_database->setting(QStringLiteral("update.check"), QStringLiteral("1")) == QLatin1String("1");
}

double AppCore::appUpdateChecked() const
{
    return m_database->setting(QStringLiteral("update.lastCheck"), QStringLiteral("0")).toDouble();
}

bool AppCore::appUpdateHint() const
{
    return appUpdateEnabled()
            && (m_updState == QLatin1String("available") || m_updState == QLatin1String("ready")
                || m_updState == QLatin1String("downloading"))
            && m_database->setting(QStringLiteral("update.dismissed")) != m_updVersion;
}

#ifndef SAILCAST_STORE
// (365) "What's new" of an update on a German phone: the German block of
// the GitHub release text (tools/release_notes.py), else the English text
static QString localizedUpdateNotes(const UpdateInfo &info)
{
    return QLocale::system().language() == QLocale::German && !info.notesDe.isEmpty()
           ? info.notesDe : info.notes;
}
#endif

QVariantList AppCore::changelog() const
{
    // Installed next to the qml folder (harbour-sailcast.pro, INSTALLS)
    auto readSections = [](const QString &name) {
        QStringList candidates;
        candidates << QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("../share/harbour-sailcast/") + name)
                   << QStringLiteral("/usr/share/harbour-sailcast/") + name;
        QFile file;
        for (int i = 0; i < candidates.size() && !file.isOpen(); ++i) {
            file.setFileName(candidates.at(i));
            file.open(QIODevice::ReadOnly | QIODevice::Text);
        }
        return file.isOpen() ? ReleaseNotes::sections(QString::fromUtf8(file.readAll()))
                             : QList<ReleaseNotes::Section>();
    };
    QVariantList result;
    QList<ReleaseNotes::Section> sections = readSections(QStringLiteral("CHANGELOG.md"));
    // (364, Thomas 19 Nov) German phone: the German text of each version
    // from CHANGELOG.de.md; a version missing there stays English
    if (QLocale::system().language() == QLocale::German) {
        const QList<ReleaseNotes::Section> german = readSections(QStringLiteral("CHANGELOG.de.md"));
        for (int i = 0; i < sections.size(); ++i) {
            for (const ReleaseNotes::Section &de : german) {
                if (de.version == sections.at(i).version) {
                    sections[i] = de;
                    break;
                }
            }
        }
    }
    if (sections.isEmpty()) {
        return result;
    }
    const QString running = version();
    for (int i = 0; i < sections.size(); ++i) {
        const ReleaseNotes::Section &s = sections.at(i);
        // A section for a version still in work (newer than this build) is left out
        if (ReleaseNotes::compareVersions(s.version, running) > 0) {
            continue;
        }
        const QDate date = QDate::fromString(s.date, Qt::ISODate);
        QVariantMap entry;
        entry.insert(QStringLiteral("version"), s.version);
        entry.insert(QStringLiteral("dateText"), date.isValid() ? QLocale().toString(date, QLocale::ShortFormat) : QString());
        entry.insert(QStringLiteral("text"), s.text);
        // (351) Laid out line by line on the About page
        QVariantList blockList;
        static const char *const kinds[] = { "heading", "item", "subitem", "numbered", "paragraph", "blank" };
        for (const ReleaseNotes::Block &block : s.blocks) {
            QVariantMap b;
            b.insert(QStringLiteral("kind"), QString::fromLatin1(kinds[block.kind]));
            b.insert(QStringLiteral("text"), block.text);
            blockList << b;
        }
        entry.insert(QStringLiteral("blocks"), blockList);
        entry.insert(QStringLiteral("current"), ReleaseNotes::compareVersions(s.version, running) == 0);
        result << entry;
    }
    return result;
}

#ifdef SAILCAST_STORE
void AppCore::setupAppUpdate() {}
void AppCore::checkAppUpdate(bool) {}
void AppCore::setAppUpdateEnabled(bool) {}
void AppCore::downloadAppUpdate() {}
void AppCore::cancelAppUpdate() {}
void AppCore::installAppUpdate() {}
void AppCore::dismissAppUpdate() {}
#else
QString AppCore::updateDirectory() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void AppCore::setUpdateState(const QString &state)
{
    m_updState = state;
    emit appUpdateChanged();
}

// The version compared with GitHub. For tests of the whole way (download,
// installer) before a newer release exists: start Sailcast from the
// terminal with SAILCAST_PRETEND_VERSION=1.4.0 (TEST-PROTOCOL 4.292)
static QString updateBaseVersion(const QString &running)
{
    const QString pretend = QString::fromLocal8Bit(qgetenv("SAILCAST_PRETEND_VERSION"));
    return UpdateChecker::parseVersion(pretend).isEmpty() ? running : pretend;
}

void AppCore::applyRelease(const QByteArray &json)
{
    if (m_updState == QLatin1String("downloading")) {
        return;
    }
    m_updInfo = UpdateChecker::parseRelease(json, UpdateChecker::architecture());
    const QString base = updateBaseVersion(version());
    if (!m_updInfo.valid || UpdateChecker::compareVersions(m_updInfo.version, base) <= 0) {
        // The release of the running version: its text is "What's new" on
        // the About page
        const bool same = m_updInfo.valid && UpdateChecker::compareVersions(m_updInfo.version, base) == 0;
        m_updVersion = same ? m_updInfo.version : QString();
        m_updNotes = same ? localizedUpdateNotes(m_updInfo) : QString();
        m_updSize = 0;
        setUpdateState(json.isEmpty() ? QStringLiteral("idle") : QStringLiteral("uptodate"));
        return;
    }
    m_updVersion = m_updInfo.version;
    m_updNotes = localizedUpdateNotes(m_updInfo);
    m_updSize = m_updInfo.assetSize;
    // Downloaded before (e.g. the installer refused: "Allow untrusted
    // software" was off): straight to "Install now"
    const QFileInfo file(QDir(updateDirectory()).filePath(m_updInfo.assetName));
    const bool have = file.exists() && (m_updInfo.assetSize <= 0 || file.size() == m_updInfo.assetSize);
    setUpdateState(have ? QStringLiteral("ready") : QStringLiteral("available"));
}

void AppCore::setupAppUpdate()
{
    m_updateChecker = new UpdateChecker(this);
    connect(m_updateChecker, &UpdateChecker::checkFinished, this,
            [this](bool notModified, const QByteArray &body, const QString &etag) {
        m_updChecking = false;
        m_database->setSetting(QStringLiteral("update.lastCheck"),
                               QString::number(QDateTime::currentMSecsSinceEpoch() / 1000));
        if (!notModified) {
            m_database->setSetting(QStringLiteral("update.json"), QString::fromUtf8(body));
            m_database->setSetting(QStringLiteral("update.etag"), etag);
        }
        applyRelease(m_database->setting(QStringLiteral("update.json")).toUtf8());
        emit appUpdateChanged();
    });
    connect(m_updateChecker, &UpdateChecker::checkFailed, this, [this](const QString &error) {
        qWarning() << "Update check failed:" << error;
        m_updChecking = false;
        // The daily check fails silently
        if (m_updManual) {
            m_updErrorKind = QStringLiteral("check");
        }
        emit appUpdateChanged();
    });
    connect(m_updateChecker, &UpdateChecker::downloadProgress, this, [this](qint64 received, qint64 total) {
        m_updReceived = received;
        if (total > 0 && total != m_updSize) {
            m_updSize = total;
            emit appUpdateChanged();    // (review 346) size unknown before
        }
        if (received - m_updEmitted >= 32 * 1024 || received == total) {
            m_updEmitted = received;
            emit appUpdateProgressChanged();
        }
    });
    connect(m_updateChecker, &UpdateChecker::downloadFinished, this, [this](const QString &path) {
        // (review 346) Only files Sailcast downloaded itself are cleaned up
        m_database->setSetting(QStringLiteral("update.file"), QFileInfo(path).fileName());
        setUpdateState(QStringLiteral("ready"));
    });
    connect(m_updateChecker, &UpdateChecker::downloadFailed, this, [this](const QString &error) {
        qWarning() << "Update download failed:" << error;
        m_updErrorKind = QStringLiteral("download");
        setUpdateState(QStringLiteral("available"));
    });

    if (appUpdateEnabled()) {
        // The last answer at once (offline too), then the daily check after the start
        applyRelease(m_database->setting(QStringLiteral("update.json")).toUtf8());
        QTimer::singleShot(8000, this, [this]() { checkAppUpdate(false); });
    }

    // Clean-up (review 346: only what Sailcast downloaded itself - a package
    // the user keeps in Downloads by hand stays): unfinished downloads (none
    // runs at the start), and the downloaded package once it is installed
    // (version <= running) or replaced by a newer release
    const QDir dir(updateDirectory());
    const QString base = updateBaseVersion(version());
    const QStringList obsolete = UpdateChecker::obsoletePackages(dir.entryList(QDir::Files), base);
    const QString own = m_database->setting(QStringLiteral("update.file"));
    for (int i = 0; i < obsolete.size(); ++i) {
        if (obsolete.at(i).endsWith(QLatin1String(".part")) || obsolete.at(i) == own) {
            QFile::remove(dir.filePath(obsolete.at(i)));
        }
    }
    if (!own.isEmpty() && !obsolete.contains(own) && m_updInfo.valid && own != m_updInfo.assetName
            && UpdateChecker::isOwnPackage(own)) {
        QFile::remove(dir.filePath(own));
    }
    if (!QFileInfo::exists(dir.filePath(own))) {
        m_database->setSetting(QStringLiteral("update.file"), QString());
    }
}

void AppCore::checkAppUpdate(bool manual)
{
    if (!m_updateChecker || m_updateChecker->busy() || m_updState == QLatin1String("downloading")) {
        return;
    }
    if (!manual) {
        const qint64 now = QDateTime::currentMSecsSinceEpoch() / 1000;
        if (!appUpdateEnabled() || now - qint64(appUpdateChecked()) < 20 * 3600) {
            return;
        }
    }
    m_updManual = manual;
    m_updChecking = true;
    m_updErrorKind.clear();
    emit appUpdateChanged();
    // Without a stored answer the ETag would make GitHub answer "not modified"
    // with nothing to show
    const bool haveJson = !m_database->setting(QStringLiteral("update.json")).isEmpty();
    m_updateChecker->check(haveJson ? m_database->setting(QStringLiteral("update.etag")) : QString());
}

void AppCore::setAppUpdateEnabled(bool enabled)
{
    m_database->setSetting(QStringLiteral("update.check"), enabled ? QStringLiteral("1") : QStringLiteral("0"));
    if (enabled) {
        applyRelease(m_database->setting(QStringLiteral("update.json")).toUtf8());
        checkAppUpdate(true);
    } else {
        if (m_updState == QLatin1String("downloading")) {
            cancelAppUpdate();
        }
        m_updErrorKind.clear();
        // (review 346) Same as after a restart: "Automatic check is off"
        m_updVersion.clear();
        m_updNotes.clear();
        setUpdateState(QStringLiteral("idle"));
    }
}

void AppCore::downloadAppUpdate()
{
    if (!m_updateChecker || m_updState != QLatin1String("available") || !m_updInfo.valid) {
        return;
    }
    m_updErrorKind.clear();
    QDir().mkpath(updateDirectory());
    const QStorageInfo storage(updateDirectory());
    if (storage.isValid() && storage.bytesAvailable() < m_updInfo.assetSize + 20 * 1024 * 1024) {
        m_updErrorKind = QStringLiteral("space");
        emit appUpdateChanged();
        return;
    }
    m_updReceived = 0;
    m_updEmitted = 0;
    setUpdateState(QStringLiteral("downloading"));
    m_updateChecker->download(m_updInfo, updateDirectory());
}

void AppCore::cancelAppUpdate()
{
    if (m_updateChecker && m_updState == QLatin1String("downloading")) {
        m_updateChecker->cancelDownload();
        m_updReceived = 0;
        setUpdateState(QStringLiteral("available"));
    }
}

void AppCore::installAppUpdate()
{
    const QString path = QDir(updateDirectory()).filePath(m_updInfo.assetName);
    if (m_updState != QLatin1String("ready") || !QFileInfo::exists(path)) {
        applyRelease(m_database->setting(QStringLiteral("update.json")).toUtf8());
        return;
    }
    m_updErrorKind.clear();
    // Detached: the installer is handed the file even though Sailcast quits
    // (test 4.29x: with "Allow untrusted software" off and on)
    bool started = QProcess::startDetached(QStringLiteral("/usr/bin/xdg-open"), QStringList() << path);
    if (!started) {
        started = QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    }
    if (!started) {
        m_updErrorKind = QStringLiteral("install");
        emit appUpdateChanged();
        return;
    }
    QTimer::singleShot(500, qApp, &QCoreApplication::quit);
}

void AppCore::dismissAppUpdate()
{
    m_database->setSetting(QStringLiteral("update.dismissed"), m_updVersion);
    emit appUpdateChanged();
}
#endif

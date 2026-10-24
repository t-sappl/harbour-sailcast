// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SYNCENGINE_H
#define SYNCENGINE_H

#include <QByteArray>
#include <QList>
#include <QMetaObject>
#include <QObject>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <functional>

#include "database.h"
#include "episodeaction.h"

class FeedUpdater;
class QNetworkAccessManager;
class QNetworkReply;

struct SyncResult
{
    bool firstSync = false;
    int podcastsAdded = 0;          // subscribed here because of the server
    int podcastsRemoved = 0;        // unsubscribed here because of the server
    int subscriptionsUploaded = 0;  // local adds/removes sent to the server
    int feedsLoaded = 0;            // new podcasts whose feed was loaded
    int actionsReceived = 0;
    ApplyResult applied;            // of all pending actions, incl. older ones
    int actionsUploaded = 0;
    qint64 elapsedMs = 0;
};

// Synchronizes subscriptions and listening state with a sync server.
//
// Two server types behind the same steps (see DESIGN.md "Sync"):
// - Nextcloud with the app "GPodder Sync" (gpoddersync): Basic Auth with an
//   app password, endpoints under /index.php/apps/gpoddersync/, no devices
// - gpodder.net API (gpodder.net, mygpo, goPodder, ...): Basic Auth,
//   endpoints under /api/2/, a device is registered first
//
// The account comes from the database settings (sync.backend, sync.server,
// sync.user, sync.device); the password is set with setPassword() (app:
// Sailfish Secrets, CLI: its credentials file).
//
// Steps: (register device) -> get subscription changes -> apply them ->
// upload local subscription changes -> load the feeds of new podcasts ->
// get episode actions -> apply them (unmatched ones stay pending) ->
// upload local episode actions. The "since" timestamps are stored after
// each download step, so an interrupted sync continues where it stopped.
//
// First sync (no "since" stored): the server's subscriptions are added
// here, local subscriptions missing on the server are uploaded (union);
// server-side removals and local listening states are not applied/sent.
class SyncEngine : public QObject
{
    Q_OBJECT
public:
    enum Backend { Nextcloud, GpodderApi };

    SyncEngine(Database *database, FeedUpdater *updater, QObject *parent = nullptr);

    void setPassword(const QString &password) { m_password = password; }
    // The password of the last sync (also used for the Nextcloud backup, 83)
    QString password() const { return m_password; }
    bool isRunning() const { return m_running; }
    void start();

    // "nextcloud" / "gpodder" <-> Backend; returns false for unknown names
    static bool backendFromName(const QString &name, Backend *backend);
    static QString backendName(Backend backend);

    // ISO 8601 as used by the API ("2026-10-09T05:20:00", UTC) <-> seconds
    static QString formatTimestamp(qint64 seconds);
    static qint64 parseTimestamp(const QString &text);

signals:
    void progress(const QString &step);
    void finished(const SyncResult &result);
    void failed(const QString &error);

private:
    typedef std::function<void(const QByteArray &body)> Handler;

    void registerDevice();
    void fetchSubscriptions();
    void applySubscriptions(const QByteArray &body);
    void uploadSubscriptions();
    void loadNewFeeds();
    void fetchActions();
    void applyActions(const QByteArray &body);
    void uploadActions();
    void finish();
    void fail(const QString &error);

    void get(const QUrl &url, const Handler &onSuccess);
    void post(const QUrl &url, const QByteArray &json, const Handler &onSuccess);
    void send(const QByteArray &verb, const QUrl &url, const QByteArray &json, const Handler &onSuccess);
    QUrl endpoint(const QString &path, qint64 since = -1) const;
    QString userPath() const;

    Database *m_database;
    FeedUpdater *m_updater;
    QNetworkAccessManager *m_network;
    QString m_password;
    Backend m_backend = Nextcloud;
    QString m_server;
    QString m_user;
    QString m_device;
    bool m_running = false;
    bool m_firstSync = false;
    SyncResult m_result;
    QList<qint64> m_newPodcasts;
    QStringList m_uploadedAdds;
    QStringList m_uploadedRemoves;
    QMetaObject::Connection m_updaterConnection;
    qint64 m_startedMs = 0;
    int m_uploadRounds = 0;
};

#endif // SYNCENGINE_H

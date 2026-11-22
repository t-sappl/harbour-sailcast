// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SAILPLAYBRIDGE_H
#define SAILPLAYBRIDGE_H

#include <QObject>
#include <QTimer>
#include <QVariantMap>

class Database;
class SailplayProvider;
struct EpisodeRow;

// Sailcast in the car with Sailplay (wireless CarPlay / Android Auto, see
// sailplay-sdk/README.md). Sailplay's car screen shows the player through
// MPRIS as for any music app; with the SDK it can also browse Sailcast's
// episodes (Continue listening, New episodes, Playlist, Podcasts, Search)
// and start one.
//
// The bridge exports the SDK objects on the session bus under Sailcast's
// existing D-Bus name (harbour-sailcast.harbour-sailcast, declared with
// X-Sailplay-Service in the .desktop file). It only reads the database;
// playing is left to the QML player: playRequested() -> PlayerController.
//
// The car asks for one page at a time (content.browse / content.search,
// at most 50 rows); the lists are read fresh for every request. The
// catalog version changes with the lists (dataChanged, queueChanged), not
// with the listening position, so the car's list stays in place while an
// episode plays.
class SailplayBridge : public QObject
{
    Q_OBJECT

public:
    explicit SailplayBridge(Database *database, QObject *parent = nullptr);
    ~SailplayBridge() override;

    // Exports the SDK objects. Call before the D-Bus name is registered,
    // so that a car starting Sailcast over D-Bus finds them at once.
    bool start();
    bool active() const { return m_provider != nullptr; }

    // The QML player tells what is playing (the SDK state "media")
    Q_INVOKABLE void setPlayback(int episodeId, bool playing);

    // Catalog version, for tests
    qulonglong catalogVersion() const { return m_version; }

    // The pages as the car gets them, for tests (no D-Bus)
    QVariantMap root() const;
    QVariantMap query(const QVariantMap &parameters, bool search, QString *error = nullptr) const;

public slots:
    // A list may have changed: new catalog version (coalesced)
    void catalogChanged();

signals:
    // The car chose an episode. fromPlaylist: chosen in the playlist, so
    // the player continues with the playlist afterwards.
    void playRequested(int episodeId, bool fromPlaylist);

private:
    QVariantMap episodeItem(const EpisodeRow &e, bool fromPlaylist, bool showPodcast) const;
    QVariantList folderItems(const QString &id, QString *title, QString *message, bool *found) const;
    QVariantList searchItems(const QString &query) const;
    QVariantMap state() const;
    void publish();

    Database *m_database;
    SailplayProvider *m_provider = nullptr;
    QTimer m_changeTimer;
    qulonglong m_version = 1;
    int m_episodeId = -1;
    bool m_playing = false;
};

#endif // SAILPLAYBRIDGE_H

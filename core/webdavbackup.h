// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef WEBDAVBACKUP_H
#define WEBDAVBACKUP_H

#include <QObject>
#include <QString>

class QNetworkAccessManager;
class QNetworkReply;

// Copies a database backup into the user's Nextcloud (83) - the same
// account as the sync (Nextcloud GPodder Sync), over WebDAV:
//   MKCOL  <server>/remote.php/dav/files/<user>/<folder>   (201, or 405 = exists)
//   PUT    .../<folder>/<file>
//   PROPFIND (Depth 1) .../<folder>, DELETE the oldest sailcast-*.db
//          beyond "keep"
// The login (app password) is only sent to the server of the account.
// One upload at a time; finished() reports the result.
// Restore (93): list() reads the backups of the folder (name, time, size;
// listed()), fetch() downloads one into a local file (fetched()).
class WebDavBackup : public QObject
{
    Q_OBJECT
public:
    explicit WebDavBackup(QObject *parent = nullptr);

    bool isRunning() const { return m_running; }
    void upload(const QString &server, const QString &user, const QString &password,
                const QString &localPath, const QString &folder, int keep);
    void list(const QString &server, const QString &user, const QString &password, const QString &folder);
    void fetch(const QString &server, const QString &user, const QString &password, const QString &folder,
               const QString &name, const QString &localPath);

    struct Entry
    {
        QString name;
        qint64 modified = 0;    // ms since the epoch, 0 = unknown
        qint64 size = 0;
    };

signals:
    void finished(bool ok, const QString &error);
    void listed(bool ok, const QList<WebDavBackup::Entry> &entries, const QString &error);
    void fetched(bool ok, const QString &localPath, const QString &error);

private:
    QNetworkReply *send(const QByteArray &verb, const QString &url, const QByteArray &body = QByteArray(),
                        bool depthOne = false);
    void makeFolder();
    void put();
    void listAndPrune();
    void done(bool ok, const QString &error = QString());
    bool prepare(const QString &server, const QString &user, const QString &password, const QString &folder);
    QString errorText(QNetworkReply *reply) const;

    QNetworkAccessManager *m_network;
    bool m_running = false;
    QString m_folderUrl;    // .../remote.php/dav/files/<user>/<folder>
    QString m_localPath;
    QByteArray m_auth;
    int m_keep = 5;
};

#endif // WEBDAVBACKUP_H

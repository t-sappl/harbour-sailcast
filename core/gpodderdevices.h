// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef GPODDERDEVICES_H
#define GPODDERDEVICES_H

#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

class QNetworkAccessManager;
class QNetworkReply;

struct GpodderDevice
{
    QString id;             // the device name in the API paths
    QString caption;
    QString type;           // desktop, laptop, mobile, server, other
    int subscriptions = 0;
    int group = -1;         // sync group on the server (index), -1 = none (83)
};

// Devices of a gpodder.net API account (51): the API keeps subscriptions
// per device (other apps such as KDE Kasts ask "new or existing device").
//  - list(): GET /api/2/devices/{user}.json
//  - synchronize(): POST /api/2/sync-devices/{user}.json with
//    {"synchronize": [[ids]]} - the server then keeps the subscriptions of
//    these devices the same (a sync group). A device that does not exist
//    yet (a new Sailcast device) is registered first (77): the server only
//    groups devices it knows - before, a new device stayed without the
//    subscriptions of the others
//  - list() also reads GET /api/2/sync-devices/{user}.json (83), so the
//    dialog shows which devices already share their subscriptions; a server
//    without it (some compatible servers) just gives no groups
//  - stopIds (83): devices taken out of their sync group
//    ("stop-synchronize") - the devices unchecked in the dialog
// Nextcloud GPodder Sync has no devices - only for the gpodder.net API.
class GpodderDevices : public QObject
{
    Q_OBJECT
public:
    explicit GpodderDevices(QObject *parent = nullptr);

    void setAccount(const QString &server, const QString &user, const QString &password);
    void list();
    void synchronize(const QStringList &deviceIds, const QString &registerFirst = QString(),
                     const QStringList &stopIds = QStringList());

signals:
    void listed(const QList<GpodderDevice> &devices);
    void synchronized();
    void failed(const QString &error);
    void synchronizeFailed(const QString &error);

private:
    QNetworkReply *send(const QByteArray &verb, const QString &path, const QByteArray &json);
    QString errorText(QNetworkReply *reply) const;
    void postGroup(const QStringList &deviceIds, const QStringList &stopIds);
    void listGroups(const QList<GpodderDevice> &devices);

    QNetworkAccessManager *m_network;
    QString m_server;
    QString m_user;
    QString m_password;
};

#endif // GPODDERDEVICES_H

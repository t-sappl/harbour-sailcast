// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef SECRETSTORE_H
#define SECRETSTORE_H

#include <QList>
#include <QObject>
#include <QString>

#include <functional>

namespace Sailfish {
namespace Secrets {
class SecretManager;
class Request;
}
}

// Stores small secrets (the gpodder sync password) with Sailfish Secrets instead
// of the app's own database. Available in QML as "secretStore".
//
// All secrets live in one collection of the app, encrypted by the system
// service, owner-only, locked together with the device and kept unlocked
// after the first unlock (no extra password prompt). The collection is only
// created when the first store fails (creating it asks the user for access);
// a store is tried up to three times (see storeSecret). The API is
// asynchronous; operations are queued and run one after the other, so a
// store that is still running cannot overtake a later one. Results arrive
// via the signals with the name of the secret.
class SecretStore : public QObject
{
    Q_OBJECT
public:
    explicit SecretStore(QObject *parent = nullptr);
    ~SecretStore();

    // Replaces the secret (an existing one is deleted first)
    Q_INVOKABLE void store(const QString &name, const QString &value);
    Q_INVOKABLE void load(const QString &name);
    Q_INVOKABLE void remove(const QString &name);

signals:
    void stored(const QString &name, bool ok, const QString &error);
    void loaded(const QString &name, bool ok, const QString &value, const QString &error);
    void removed(const QString &name, bool ok, const QString &error);

private:
    typedef std::function<void()> Operation;
    typedef std::function<void(bool ok, const QString &error)> Done;

    void enqueue(const Operation &op);
    void next();
    void createCollection(const std::function<void()> &then);
    // attempt: 0 = first try, 1 = after creating the collection, 2 = last try
    void storeSecret(const QString &name, const QString &value, int attempt);
    // Starts the request and calls done once it has finished; the request
    // deletes itself afterwards
    void run(Sailfish::Secrets::Request *request, const Done &done);

    Sailfish::Secrets::SecretManager *m_manager;
    QList<Operation> m_queue;
    bool m_busy;
};

#endif // SECRETSTORE_H

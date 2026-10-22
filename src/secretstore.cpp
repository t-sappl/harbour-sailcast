// SPDX-License-Identifier: GPL-3.0-or-later
#include "secretstore.h"

#include <QDebug>
#include <QTimer>

#include <Secrets/secretmanager.h>
#include <Secrets/secret.h>
#include <Secrets/request.h>
#include <Secrets/result.h>
#include <Secrets/createcollectionrequest.h>
#include <Secrets/storesecretrequest.h>
#include <Secrets/storedsecretrequest.h>
#include <Secrets/deletesecretrequest.h>

using namespace Sailfish::Secrets;

namespace {
const QString CollectionName = QStringLiteral("sailcast");
// Pause before the last store attempt (see storeSecret)
const int RetryDelayMs = 1500;

Secret::Identifier identifierFor(const QString &name)
{
    return Secret::Identifier(name, CollectionName,
                              SecretManager::DefaultEncryptedStoragePluginName);
}
}

SecretStore::SecretStore(QObject *parent)
    : QObject(parent)
    , m_manager(new SecretManager(this))
    , m_busy(false)
{
}

SecretStore::~SecretStore()
{
}

void SecretStore::enqueue(const Operation &op)
{
    m_queue.append(op);
    if (!m_busy) {
        next();
    }
}

void SecretStore::next()
{
    if (m_queue.isEmpty()) {
        m_busy = false;
        return;
    }
    m_busy = true;
    Operation op = m_queue.takeFirst();
    op();
}

void SecretStore::run(Request *request, const Done &done)
{
    connect(request, &Request::statusChanged, this, [request, done]() {
        if (request->status() != Request::Finished) {
            return;
        }
        const Result result = request->result();
        done(result.code() == Result::Succeeded, result.errorMessage());
        request->deleteLater();
    });
    request->startRequest();
}

// Creates the app's collection. Only called when storing failed, i.e. in
// practice once, on the very first save: creating a collection makes the
// system ask the user for access ("Allow access") - doing it at every start
// (as up to v61) brought that question at every start.
void SecretStore::createCollection(const std::function<void()> &then)
{
    CreateCollectionRequest *request = new CreateCollectionRequest(this);
    request->setManager(m_manager);
    request->setCollectionName(CollectionName);
    request->setAccessControlMode(SecretManager::OwnerOnlyMode);
    request->setCollectionLockType(CreateCollectionRequest::DeviceLock);
    request->setDeviceLockUnlockSemantic(SecretManager::DeviceLockKeepUnlocked);
    request->setStoragePluginName(SecretManager::DefaultEncryptedStoragePluginName);
    request->setEncryptionPluginName(SecretManager::DefaultEncryptedStoragePluginName);
    run(request, [then](bool, const QString &) {
        then();
    });
}

// Stores the secret, with up to three attempts:
// 1. as is - fails if the collection does not exist yet;
// 2. after creating the collection - creating it makes the system ask the
//    user for access, and on the very first save this attempt could still
//    fail although access was granted (test: the password was only kept
//    when it was entered a second time);
// 3. once more after a short pause.
// Only the last failure is reported (stored with ok = false).
void SecretStore::storeSecret(const QString &name, const QString &value, int attempt)
{
    Secret secret(identifierFor(name));
    secret.setType(Secret::TypeBlob);
    secret.setData(value.toUtf8());

    StoreSecretRequest *request = new StoreSecretRequest(this);
    request->setManager(m_manager);
    request->setSecretStorageType(StoreSecretRequest::CollectionSecret);
    request->setUserInteractionMode(SecretManager::SystemInteraction);
    request->setSecret(secret);
    run(request, [this, name, value, attempt](bool ok, const QString &error) {
        if (!ok && attempt == 0) {
            createCollection([this, name, value]() {
                storeSecret(name, value, 1);
            });
            return;
        }
        if (!ok && attempt == 1) {
            qWarning() << "[SecretStore] Storing failed after creating the collection, retrying:" << error;
            QTimer::singleShot(RetryDelayMs, this, [this, name, value]() {
                storeSecret(name, value, 2);
            });
            return;
        }
        emit stored(name, ok, error);
        next();
    });
}

void SecretStore::store(const QString &name, const QString &value)
{
    enqueue([this, name, value]() {
        // Delete an existing secret first (fails harmlessly if there is
        // none, or if the collection does not exist yet)
        DeleteSecretRequest *del = new DeleteSecretRequest(this);
        del->setManager(m_manager);
        del->setIdentifier(identifierFor(name));
        del->setUserInteractionMode(SecretManager::SystemInteraction);
        run(del, [this, name, value](bool, const QString &) {
            storeSecret(name, value, 0);
        });
    });
}

// Reading never creates the collection: it is only called when a secret
// was stored before (so the collection exists)
void SecretStore::load(const QString &name)
{
    enqueue([this, name]() {
        StoredSecretRequest *request = new StoredSecretRequest(this);
        request->setManager(m_manager);
        request->setIdentifier(identifierFor(name));
        request->setUserInteractionMode(SecretManager::SystemInteraction);
        run(request, [this, request, name](bool ok, const QString &error) {
            // Still valid here: the request deletes itself only later
            const QString value = ok ? QString::fromUtf8(request->secret().data()) : QString();
            emit loaded(name, ok, value, error);
            next();
        });
    });
}

void SecretStore::remove(const QString &name)
{
    enqueue([this, name]() {
        DeleteSecretRequest *request = new DeleteSecretRequest(this);
        request->setManager(m_manager);
        request->setIdentifier(identifierFor(name));
        request->setUserInteractionMode(SecretManager::SystemInteraction);
        run(request, [this, name](bool ok, const QString &error) {
            emit removed(name, ok, error);
            next();
        });
    });
}

// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef NETUTIL_H
#define NETUTIL_H

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QObject>
#include <QtGlobal>

// Works around a Qt 5.6 bearer management problem on Sailfish OS:
// after an offline phase (e.g. flight mode) a QNetworkAccessManager can stay
// in the state "not accessible" even when the connection is back. Every
// request then fails immediately with "Network access is disabled" until
// the app is restarted.
//
// Keeping the manager "accessible" is safe: when the device really is
// offline, requests still fail, just with a normal network error.
// (Taken over from Sailwave.)
namespace NetworkAccess {

inline void keepAccessible(QNetworkAccessManager *manager)
{
#if QT_VERSION >= QT_VERSION_CHECK(5, 15, 0)
    // Bearer management is deprecated from Qt 5.15 on and the problem does
    // not occur there - only needed on Sailfish OS (Qt 5.6). This also keeps
    // the desktop build of the CLI free of deprecation warnings.
    Q_UNUSED(manager)
#else
    manager->setNetworkAccessible(QNetworkAccessManager::Accessible);
    QObject::connect(manager, &QNetworkAccessManager::networkAccessibleChanged, manager,
                     [manager](QNetworkAccessManager::NetworkAccessibility accessible) {
        if (accessible != QNetworkAccessManager::Accessible) {
            manager->setNetworkAccessible(QNetworkAccessManager::Accessible);
        }
    });
#endif
}

// User agent for feed and API requests. Some hosters block requests
// without one or with a generic Qt user agent.
inline QByteArray userAgent()
{
    // Honest, specific app name (61): Podcast Index blocks generic and
    // sample user agents since its terms of service 2.0 (Oct 2026)
    return QByteArrayLiteral("harbour-sailcast/1.1 (Sailfish OS; +https://github.com/t-sappl/harbour-sailcast)");
}

} // namespace NetworkAccess

#endif // NETUTIL_H

// SPDX-License-Identifier: GPL-3.0-or-later
#include <sailfishapp.h>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QDBusConnection>
#include <QDBusError>
#include <QDebug>
#include <QtQml>

#include "appcore.h"
#include "applog.h"
#include "artcomposer.h"
#include "episodelistmodel.h"
#include "imagecache.h"
#include "networkaccess.h"
#include "podcastlistmodel.h"
#include "sailplaybridge.h"
#include "secretstore.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));
    // (351) Warnings also into the app's own log file ("Show log", About).
    // (356) SAILCAST_NO_LOG=1 starts without it - to test whether the crash
    // when switching to the app overview (Lipstick, 18 Nov) comes from it
    if (qgetenv("SAILCAST_NO_LOG") != "1") {
        AppLog::install(AppCore::versionText());
    } else {
        qWarning("Sailcast: app log off (SAILCAST_NO_LOG=1)");
    }

    // Harbour requires private QML imports to start with the app name
    qmlRegisterType<PodcastListModel>("harbour.sailcast", 1, 0, "PodcastListModel");
    qmlRegisterType<FolderListModel>("harbour.sailcast", 1, 0, "FolderListModel");
    qmlRegisterType<EpisodeListModel>("harbour.sailcast", 1, 0, "EpisodeListModel");
    qmlRegisterType<ArtComposer>("harbour.sailcast", 1, 0, "ArtComposer");

    // Declared before the view so they outlive the QML engine that uses them
    NetworkAccess::Factory networkFactory;
    NetworkAccess::ImageCacheControl imageCacheControl;
    SecretStore secretStore;
    // Scaled-down local copies of covers and episode images
    ImageCache covers;
    // Database and workers of the core library (see appcore.h); must exist
    // before the first list model is created
    AppCore core;
    // Sailcast in the car with Sailplay (see sailplaybridge.h); its lists
    // follow the core's change signals
    SailplayBridge sailplay(core.database());
    QObject::connect(&core, &AppCore::dataChanged, &sailplay, &SailplayBridge::catalogChanged);
    QObject::connect(&core, &AppCore::queueChanged, &sailplay, &SailplayBridge::catalogChanged);

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    // Must be set before any QML is loaded (see networkaccess.h)
    view->engine()->setNetworkAccessManagerFactory(&networkFactory);
    view->rootContext()->setContextProperty(QStringLiteral("imageCache"), &imageCacheControl);
    view->rootContext()->setContextProperty(QStringLiteral("secretStore"), &secretStore);
    view->rootContext()->setContextProperty(QStringLiteral("covers"), &covers);
    view->rootContext()->setContextProperty(QStringLiteral("core"), &core);
    view->rootContext()->setContextProperty(QStringLiteral("sailplay"), &sailplay);

    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-sailcast.qml")));

    // Share target (84): the D-Bus name of the app (OrganizationName +
    // "." + ApplicationName of the .desktop file's [X-Sailjail] section).
    // Registered here, after the QML has created its adaptor
    // (/share/podcast) - as the Sailfish.Share documentation asks, to avoid a
    // race between the name and the objects. With ExecDBus a share also
    // starts the app.
    // The Sailplay SDK objects too, before the name: a car that starts
    // Sailcast over D-Bus opens its session as soon as the name appears
    sailplay.start();
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (!bus.registerService(QStringLiteral("harbour-sailcast.harbour-sailcast"))) {
        qWarning() << "D-Bus name not registered:" << bus.lastError().message();
    }

    view->show();

    return app->exec();
}

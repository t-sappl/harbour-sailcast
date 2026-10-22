// SPDX-License-Identifier: GPL-3.0-or-later
#include <sailfishapp.h>
#include <QGuiApplication>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickView>
#include <QtQml>

#include "appcore.h"
#include "artcomposer.h"
#include "episodelistmodel.h"
#include "imagecache.h"
#include "networkaccess.h"
#include "podcastlistmodel.h"
#include "secretstore.h"

int main(int argc, char *argv[])
{
    QScopedPointer<QGuiApplication> app(SailfishApp::application(argc, argv));

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

    QScopedPointer<QQuickView> view(SailfishApp::createView());

    // Must be set before any QML is loaded (see networkaccess.h)
    view->engine()->setNetworkAccessManagerFactory(&networkFactory);
    view->rootContext()->setContextProperty(QStringLiteral("imageCache"), &imageCacheControl);
    view->rootContext()->setContextProperty(QStringLiteral("secretStore"), &secretStore);
    view->rootContext()->setContextProperty(QStringLiteral("covers"), &covers);
    view->rootContext()->setContextProperty(QStringLiteral("core"), &core);

    view->setSource(SailfishApp::pathTo(QStringLiteral("qml/harbour-sailcast.qml")));
    view->show();

    return app->exec();
}

# SPDX-License-Identifier: GPL-3.0-or-later
TARGET = harbour-sailcast

CONFIG += sailfishapp sailfishapp_i18n
TRANSLATIONS += translations/harbour-sailcast-de.ts

QT += network sql multimedia

# Sailfish Secrets for the gpodder sync password (see src/secretstore.h).
# No own "CONFIG += link_pkgconfig": qmake loads CONFIG features in reverse
# order, so it would be processed before sailfishapp.prf adds its own
# PKGCONFIG entry - and -lsailfishapp would be missing when linking.
# sailfishapp.prf enables link_pkgconfig itself. (Same as in Sailwave.)
PKGCONFIG += sailfishsecrets
INCLUDEPATH += /usr/include/Sailfish

# The core library (database, feeds, OPML, importers), shared with the
# desktop test tool in cli/
include(core/core.pri)

SOURCES += src/harbour-sailcast.cpp \
    src/appcore.cpp \
    src/artcomposer.cpp \
    src/episodelistmodel.cpp \
    src/imagecache.cpp \
    src/podcastlistmodel.cpp \
    src/secretstore.cpp

HEADERS += \
    src/appcore.h \
    src/artcomposer.h \
    src/episodelistmodel.h \
    src/imagecache.h \
    src/imagefingerprint.h \
    src/networkaccess.h \
    src/podcastlistmodel.h \
    src/rowlistmodel.h \
    src/secretstore.h

# Listed only so that Qt Creator shows them in the project tree.
# Installation is handled by CONFIG += sailfishapp (the whole qml/ folder).
OTHER_FILES += rpm/harbour-sailcast.spec \
    podcastindex.pri.example \
    harbour-sailcast.desktop \
    qml/harbour-sailcast.qml \
    qml/ChapterList.qml \
    qml/CoverImage.qml \
    qml/EpisodeArt.qml \
    qml/EpisodeDelegate.qml \
    qml/MessageBanner.qml \
    qml/MprisIntegration.qml \
    qml/PlayerBar.qml \
    qml/PlayerPanel.qml \
    qml/NewDot.qml \
    qml/PodcastMenu.qml \
    qml/PlayerController.qml \
    qml/SeekBar.qml \
    qml/SkipButton.qml \
    qml/SleepBadge.qml \
    qml/SleepTimer.qml \
    qml/TimeFormat.js \
    qml/cover/CoverPage.qml \
    qml/pages/AddPodcastDialog.qml \
    qml/pages/ArchiveDialog.qml \
    qml/pages/ContinueListeningPage.qml \
    qml/pages/DownloadsPage.qml \
    qml/pages/EpisodePage.qml \
    qml/pages/EpisodesPage.qml \
    qml/pages/FeedLoginDialog.qml \
    qml/pages/HomePage.qml \
    qml/pages/ImportExportItem.qml \
    qml/pages/ImportExportPage.qml \
    qml/pages/MobileDataDialog.qml \
    qml/pages/PasswordDialog.qml \
    qml/pages/PlaylistPage.qml \
    qml/pages/PodcastsMenu.qml \
    qml/pages/PodcastsPage.qml \
    qml/pages/SearchPage.qml \
    qml/pages/SettingsPage.qml \
    qml/pages/FolderNameDialog.qml \
    qml/pages/FolderPickerDialog.qml \
    qml/pages/FolderSelectPage.qml \
    qml/pages/FolderPodcastsPage.qml \
    qml/pages/FolderDeleteDialog.qml \
    qml/pages/GpodderDeviceDialog.qml \
    qml/pages/BookmarksPage.qml \
    qml/pages/BookmarkNoteDialog.qml \
    qml/FolderSection.qml \
    qml/OnceHint.qml \
    qml/RingHint.qml \
    qml/FolderComboBox.qml \
    qml/PodcastTile.qml \
    qml/PodcastRowItem.qml \
    qml/pages/AboutPage.qml \
    qml/ToolbarButton.qml \
    qml/pages/PodcastSettingsPage.qml \
    translations/*.ts

# Install the app icons in the four sizes Sailfish OS expects
icon86.files = icons/86x86/harbour-sailcast.png
icon86.path = /usr/share/icons/hicolor/86x86/apps
icon108.files = icons/108x108/harbour-sailcast.png
icon108.path = /usr/share/icons/hicolor/108x108/apps
icon128.files = icons/128x128/harbour-sailcast.png
icon128.path = /usr/share/icons/hicolor/128x128/apps
icon172.files = icons/172x172/harbour-sailcast.png
icon172.path = /usr/share/icons/hicolor/172x172/apps

INSTALLS += icon86 icon108 icon128 icon172

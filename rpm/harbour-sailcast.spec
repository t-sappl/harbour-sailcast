# SPDX-License-Identifier: GPL-3.0-or-later
Name:       harbour-sailcast

Summary:    Podcast player for Sailfish OS
Version:    1.0
Release:    1
License:    GPL-3.0-or-later
Source0:    %{name}-%{version}.tar.bz2

# Unversioned on purpose: Harbour only allows unversioned Requires
Requires:   sailfishsilica-qt5
# No Requires for the QtSql SQLite driver: not allowed in Harbour (55), and
# it is part of every Sailfish OS installation (as in Sailfeed)
Requires:   qt5-qtmultimedia
# QML module QtMultimedia (MediaPlayer for playback)
Requires:   qt5-qtdeclarative-import-multimedia
# Lock screen and headset controls (Amber.Mpris): required, so the package
# manager installs it with the app (37, as in Sailwave - with Recommends
# users without amber-mpris had no lock screen controls). The QML side
# still loads it through a Loader.
Requires:   qml(Amber.Mpris)
BuildRequires:  pkgconfig(sailfishapp) >= 1.0.2
BuildRequires:  pkgconfig(Qt5Core)
BuildRequires:  pkgconfig(Qt5Qml)
BuildRequires:  pkgconfig(Qt5Quick)
BuildRequires:  pkgconfig(Qt5Network)
BuildRequires:  pkgconfig(Qt5Sql)
BuildRequires:  pkgconfig(Qt5Multimedia)
BuildRequires:  pkgconfig(sailfishsecrets)
BuildRequires:  desktop-file-utils
# lrelease for the translations (CONFIG += sailfishapp_i18n)
BuildRequires:  qt5-qttools-linguist

%description
Sailcast is a podcast player for Sailfish OS with a native C++ core and
gpodder.net-compatible synchronization of subscriptions and playback state.

%prep
%setup -q -n %{name}-%{version}

%build
%qmake5
make %{?_smp_mflags}

%install
rm -rf %{buildroot}
%qmake5_install

desktop-file-install --delete-original \
  --dir %{buildroot}%{_datadir}/applications \
   %{buildroot}%{_datadir}/applications/*.desktop

%files
%defattr(-,root,root,-)
%{_bindir}/%{name}
%{_datadir}/%{name}
%{_datadir}/applications/%{name}.desktop
%{_datadir}/icons/hicolor/*/apps/%{name}.png

%changelog
* Thu Oct 22 2026 Thomas Sappl <thomas.sappl@gmail.com> - 1.0-1
- First public release: podcast search (Podcast Index), folders, playlist,
  player with chapters, bookmarks and sleep timer, per-podcast speed and
  intro/outro skipping, downloads, archive, lock screen controls, portrait
  and landscape, sync with Nextcloud GPodder Sync (browser sign-in) or a
  gpodder.net compatible server (devices and sync group), OPML import and
  export, English and German.
* Fri Oct 09 2026 Thomas Sappl <thomas.sappl@gmail.com> - 0.1-1
- Stage 4a/4b: lists (new episodes, podcasts, playlist, episodes,
  episode page) and the player (mini player, player page, chapters,
  speed, sleep timer, lock screen controls).
* Tue Oct 06 2026 Thomas Sappl <thomas.sappl@gmail.com> - 0.1-1
- Stage 1: project skeleton, core library (database, feed parser and
  updater, OPML, gpodder-sailfish import), command line test tool.

# Sailplay SDK (provider side)

These files let Sailcast appear in the car with
[Sailplay](https://github.com/0312birdzhang/harbour-sailplay) (wireless
CarPlay / Android Auto for Sailfish OS): a browsable list of episodes on the
car screen, playback through Sailcast's existing MPRIS player.

They are copied from Sailplay at commit
`e29d1bf7d62b7689fab35667e97a76a4d9e246be` (21 Nov 2026), with one change
(branch `sailplay-broadcast`): `SailplayProvider::sendSignal` broadcasts its
signals instead of addressing them to the car's client name. The Sailjail
D-Bus proxy (xdg-dbus-proxy) lets a sandboxed app broadcast, but drops
signals addressed to another name unless an extra profile in
`/etc/sailjail/permissions` allows them - a file the Jolla Store does not
accept. Sailplay's client matches on sender, path and interface, so it needs
no change:

| Here | In Sailplay |
|---|---|
| `sailplayprovider.h/.cpp` | `sdk/qt/` |
| `sailplaycontent.h/.cpp` | `sdk/qt/` |
| `interfaces/*.xml` | `sailplay_sdk/interfaces/` |

`sailplay-sdk.pri` and `qmake_sailplay_contracts.qrc` (the name the provider
loads with `Q_INIT_RESOURCE`) are Sailcast's own (Sailplay's
`.pri` takes the XML from an installed Sailplay or its source tree).

The SDK is licensed GPL-3.0-or-later, like Sailcast (see `../LICENSE`).

On the branch `sailplay-presentation`, `sailplaycontent.h/.cpp` come
instead from the proposed Sailplay change that adds presentation hints
(images, playback status, tabs and split pages), commit
`149a7100887490b59faea54b8e8d49dce6ac8f92` of the branch
`content-presentation` of https://github.com/solboki/harbour-sailplay. They
only add functions; until Sailplay ships them, the car screen ignores the
hints and shows the plain lists.

To update: copy the same files from a newer Sailplay checkout and change
the commit above. The contract is versioned (`org.sailplay.App1`,
`Session1`, `Content1`); a new major version would get new interface names.

Sailcast's side is `src/sailplaybridge.cpp` - see DESIGN.md, "Sailplay".

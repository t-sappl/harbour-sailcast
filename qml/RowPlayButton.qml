// SPDX-License-Identifier: GPL-3.0-or-later
import QtQuick 2.6
import Sailfish.Silica 1.0

// ▶ at the end of a list row (195): the theme's play symbol (a grey disc in
// Sailfish OS 5) a size smaller - at full size it looked huge next to the
// text. The touch area stays as large as before.
// inset: space between the symbol and the button's edge, so the symbol
// can line up with the page margin (rightMargin: margin - inset).
IconButton {
    readonly property real inset: (width - icon.width) / 2

    width: Theme.iconSizeMedium + 2 * Theme.paddingMedium
    height: width
    icon.width: Theme.iconSizeSmallPlus
    icon.height: Theme.iconSizeSmallPlus
    icon.sourceSize.width: Theme.iconSizeSmallPlus
    icon.sourceSize.height: Theme.iconSizeSmallPlus
    icon.source: "image://theme/icon-m-play"
}

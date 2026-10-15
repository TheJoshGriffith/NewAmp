#pragma once

#include "skin/Skin.h"

#include <QList>

namespace newamp {

// Builds NewAmp's own built-in skin by drawing every sheet in code, in the
// classic layout. No bitmap assets are shipped, so there is nothing to
// license and nothing to load from disk.
//
// It comes in several colour themes; the first is the default.
class DefaultSkin
{
public:
    struct ThemeInfo
    {
        QString id;
        QString name;
    };

    static QList<ThemeInfo> themes();
    // Unknown ids give the default theme.
    static Skin create(const QString &themeId = QString());
};

} // namespace newamp

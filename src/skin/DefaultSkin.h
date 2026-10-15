#pragma once

#include "skin/Skin.h"

namespace newamp {

// Builds NewAmp's own built-in skin by drawing every sheet in code, in the
// classic layout. No bitmap assets are shipped, so there is nothing to
// license and nothing to load from disk.
class DefaultSkin
{
public:
    static Skin create();
};

} // namespace newamp

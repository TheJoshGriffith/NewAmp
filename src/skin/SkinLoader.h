#pragma once

#include "skin/Skin.h"

#include <optional>

namespace newamp {

// Loads classic skins from a .wsz/.zip archive or an unpacked folder.
// Any bitmap the skin lacks is taken from the fallback skin, as the classic
// player did with its base skin.
class SkinLoader
{
public:
    static std::optional<Skin> load(const QString &path, const Skin &fallback, QString *error = nullptr);

    // Parses pledit.txt; keys the file doesn't set keep their fallback value.
    static PlaylistStyle parsePlaylistStyle(const QByteArray &text, const PlaylistStyle &fallback);
    // Parses viscolor.txt ("r,g,b" per line, comments allowed).
    static VisColors parseVisColors(const QByteArray &text, const VisColors &fallback);
};

} // namespace newamp

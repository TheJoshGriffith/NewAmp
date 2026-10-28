#pragma once

#include "skin/Skin.h"

#include <QImage>
#include <QStringList>

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
    // Parses region.txt: per section, NumPoints lists how many points each
    // polygon has and PointList holds them all as x,y pairs.
    static SkinRegions parseRegions(const QByteArray &text);
    // Reads the library colours from the swatches along genex.bmp's top row.
    static PlaylistStyle parseGenExColors(const QImage &genex, const PlaylistStyle &fallback);
    // Decodes a Windows .cur (or .ico) file; hotspot is set for cursors.
    static QImage parseCursor(const QByteArray &data, QPoint *hotspot = nullptr);
    // The cursor files a classic skin may contain, without ".cur".
    static const QStringList &cursorNames();
};

} // namespace newamp

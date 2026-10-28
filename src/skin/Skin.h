#pragma once

#include "skin/SkinSprites.h"

#include <QColor>
#include <QCursor>
#include <QHash>
#include <QPixmap>
#include <QPolygon>
#include <QString>

#include <array>
#include <utility>

class QPainter;

namespace newamp {

// Colours and font for the playlist (pledit.txt in classic skins).
struct PlaylistStyle
{
    QColor normal{0x00, 0xff, 0x00};
    QColor current{0xff, 0xff, 0xff};
    QColor normalBackground{0x00, 0x00, 0x00};
    QColor selectedBackground{0x00, 0x00, 0xc6};
    QString fontFamily;
};

// The 24 visualizer colours (viscolor.txt): 0 background, 1 grid dots,
// 2..17 spectrum from top to bottom, 18..22 oscilloscope, 23 peak dots.
using VisColors = std::array<QColor, 24>;

// Window shapes from region.txt, by lower-case section name ("normal",
// "windowshade", "equalizer", "equalizerws"), in skin pixels. A window is
// the union of its section's polygons.
using SkinRegions = QHash<QString, QList<QPolygon>>;

// A loaded set of skin bitmaps. Windows draw sprites out of it by sheet and
// rect; they never know whether it came from the default skin or a file.
class Skin
{
public:
    using Sheet = sprites::Sheet;

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    const QPixmap &sheet(Sheet sheet) const { return m_sheets[static_cast<int>(sheet)]; }
    void setSheet(Sheet sheet, const QPixmap &pixmap);

    const PlaylistStyle &playlistStyle() const { return m_playlistStyle; }
    void setPlaylistStyle(const PlaylistStyle &style) { m_playlistStyle = style; }

    const VisColors &visColors() const { return m_visColors; }
    void setVisColors(const VisColors &colors) { m_visColors = colors; }

    // Colours for generic windows such as the media library, from genex.bmp;
    // skins without it use the playlist's.
    const PlaylistStyle &libraryStyle() const { return m_hasLibraryStyle ? m_libraryStyle : m_playlistStyle; }
    void setLibraryStyle(const PlaylistStyle &style)
    {
        m_libraryStyle = style;
        m_hasLibraryStyle = true;
    }

    const SkinRegions &regions() const { return m_regions; }
    void setRegions(const SkinRegions &regions) { m_regions = regions; }

    // The skin's cursor for an area, named after its file without ".cur"
    // ("normal", "titlebar", "volbal", ...), or null for the system cursor.
    const QCursor *cursor(const QString &name) const;
    void setCursors(const QHash<QString, QCursor> &cursors) { m_cursors = cursors; }

    void drawSprite(QPainter &painter, const QPoint &at, Sheet sheet, const QRect &source) const;

    // Draws text with the skin's bitmap font, clipped to maxWidth pixels.
    void drawText(QPainter &painter, const QPoint &at, const QString &text, int maxWidth = -1) const;
    static int textWidth(const QString &text);

    // Title text for generic windows, drawn with the variable-width letters
    // in the Gen sheet (A-Z only; anything else is a gap).
    void drawGenText(QPainter &painter, const QPoint &at, const QString &text, bool active) const;
    int genTextWidth(const QString &text) const;

    // Cell (column, row) of a character in the Text sheet. Unknown
    // characters map to the space cell.
    static QPoint glyphCell(QChar c);

private:
    QString m_name;
    PlaylistStyle m_playlistStyle;
    PlaylistStyle m_libraryStyle;
    bool m_hasLibraryStyle = false;
    VisColors m_visColors;
    SkinRegions m_regions;
    QHash<QString, QCursor> m_cursors;
    std::array<QPixmap, static_cast<int>(Sheet::Count)> m_sheets;
    std::array<std::pair<int, int>, 26> m_genLetters{}; // x and width of A-Z in the Gen sheet
    QPixmap m_genText; // the Gen sheet's letter rows, background made transparent
};

} // namespace newamp

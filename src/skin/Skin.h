#pragma once

#include "skin/SkinSprites.h"

#include <QColor>
#include <QPixmap>
#include <QString>

#include <array>

class QPainter;

namespace newamp {

// A loaded set of skin bitmaps. Windows draw sprites out of it by sheet and
// rect; they never know whether it came from the default skin or a file.
class Skin
{
public:
    using Sheet = sprites::Sheet;

    QString name() const { return m_name; }
    void setName(const QString &name) { m_name = name; }

    const QPixmap &sheet(Sheet sheet) const { return m_sheets[static_cast<int>(sheet)]; }
    void setSheet(Sheet sheet, const QPixmap &pixmap) { m_sheets[static_cast<int>(sheet)] = pixmap; }

    void drawSprite(QPainter &painter, const QPoint &at, Sheet sheet, const QRect &source) const;

    // Draws text with the skin's bitmap font, clipped to maxWidth pixels.
    void drawText(QPainter &painter, const QPoint &at, const QString &text, int maxWidth = -1) const;
    static int textWidth(const QString &text);

    // Cell (column, row) of a character in the Text sheet. Unknown
    // characters map to the space cell.
    static QPoint glyphCell(QChar c);

private:
    QString m_name;
    std::array<QPixmap, static_cast<int>(Sheet::Count)> m_sheets;
};

} // namespace newamp

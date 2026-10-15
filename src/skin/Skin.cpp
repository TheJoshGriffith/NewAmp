#include "skin/Skin.h"

#include <QPainter>

namespace newamp {

namespace {
// The classic text.bmp character layout, one string per row.
const char16_t *const GlyphRows[] = {
    u"ABCDEFGHIJKLMNOPQRSTUVWXYZ\"@",
    u"0123456789….:()-'!_+\\/[]^&%,=$#",
    u"ÅÖÄ?*",
};
constexpr QPoint SpaceCell{30, 0};
} // namespace

void Skin::drawSprite(QPainter &painter, const QPoint &at, Sheet sheet, const QRect &source) const
{
    painter.drawPixmap(at, this->sheet(sheet), source);
}

QPoint Skin::glyphCell(QChar c)
{
    const QChar upper = c.toUpper();
    for (int row = 0; row < 3; ++row) {
        const int col = QStringView(GlyphRows[row]).indexOf(upper);
        if (col >= 0)
            return {col, row};
    }
    return SpaceCell;
}

int Skin::textWidth(const QString &text)
{
    return text.size() * sprites::GlyphSize.width();
}

void Skin::drawText(QPainter &painter, const QPoint &at, const QString &text, int maxWidth) const
{
    const QSize glyph = sprites::GlyphSize;
    int x = at.x();
    for (const QChar c : text) {
        if (maxWidth >= 0 && x - at.x() + glyph.width() > maxWidth)
            break;
        const QPoint cell = glyphCell(c);
        drawSprite(painter, {x, at.y()}, Sheet::Text,
                   QRect(QPoint(cell.x() * glyph.width(), cell.y() * glyph.height()), glyph));
        x += glyph.width();
    }
}

} // namespace newamp

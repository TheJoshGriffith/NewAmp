#include "skin/Skin.h"

#include <QImage>
#include <QList>
#include <QPainter>

#include <algorithm>

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

const QCursor *Skin::cursor(const QString &name) const
{
    const auto it = m_cursors.constFind(name);
    return it == m_cursors.constEnd() ? nullptr : &*it;
}

void Skin::setSheet(Sheet sheet, const QPixmap &pixmap)
{
    m_sheets[static_cast<int>(sheet)] = pixmap;
    if (sheet != Sheet::Gen)
        return;
    m_genLetters = {};
    m_genText = QPixmap();

    // Letters sit side by side on a background colour, which is also the
    // colour of the row's first column. Find each run of letter columns.
    const QImage image = pixmap.toImage();
    const int y0 = sprites::GenTextActiveY;
    if (image.height() < y0 + sprites::GenTextHeight)
        return;
    const QRgb background = image.pixel(0, y0);
    QList<std::pair<int, int>> runs;
    int start = -1;
    for (int x = 0; x <= image.width(); ++x) {
        bool letter = false;
        for (int y = y0; x < image.width() && y < y0 + sprites::GenTextHeight && !letter; ++y)
            letter = image.pixel(x, y) != background;
        if (letter && start < 0)
            start = x;
        else if (!letter && start >= 0) {
            runs.append({start, x - start});
            start = -1;
        }
    }
    if (runs.isEmpty())
        return;

    // Some skins (the classic base one included) draw neighbouring letters
    // touching; split runs that are clearly several letters wide.
    QList<int> widths;
    for (const auto &run : std::as_const(runs))
        widths.append(run.second);
    std::sort(widths.begin(), widths.end());
    const int typical = qMax(1, widths[widths.size() / 2]);
    QList<std::pair<int, int>> letters;
    for (const auto &[x, width] : std::as_const(runs)) {
        const int parts = qMax(1, qRound(double(width) / typical));
        for (int i = 0; i < parts; ++i) {
            const int from = x + width * i / parts;
            letters.append({from, x + width * (i + 1) / parts - from});
        }
    }
    for (int i = 0; i < 26 && i < letters.size(); ++i)
        m_genLetters[size_t(i)] = letters[i];

    // Letters are drawn without their background so the title bar shows
    // through any gaps.
    QImage text = image.copy(0, y0, image.width(), sprites::GenTextInactiveY + sprites::GenTextHeight - y0)
                      .convertToFormat(QImage::Format_ARGB32);
    for (int y = 0; y < text.height(); ++y)
        for (int x = 0; x < text.width(); ++x)
            if (text.pixel(x, y) == background)
                text.setPixel(x, y, qRgba(0, 0, 0, 0));
    m_genText = QPixmap::fromImage(text);
}

void Skin::drawGenText(QPainter &painter, const QPoint &at, const QString &text, bool active) const
{
    const int y = active ? sprites::GenTextActiveY : sprites::GenTextInactiveY;
    int x = at.x();
    for (const QChar c : text) {
        const int index = c.toUpper().unicode() - 'A';
        if (index < 0 || index >= 26 || m_genLetters[size_t(index)].second == 0) {
            x += 5;
            continue;
        }
        const auto [sx, width] = m_genLetters[size_t(index)];
        painter.drawPixmap(QPoint(x, at.y()), m_genText,
                           QRect(sx, y - sprites::GenTextActiveY, width, sprites::GenTextHeight));
        x += width;
    }
}

int Skin::genTextWidth(const QString &text) const
{
    int width = 0;
    for (const QChar c : text) {
        const int index = c.toUpper().unicode() - 'A';
        const bool known = index >= 0 && index < 26 && m_genLetters[size_t(index)].second > 0;
        width += known ? m_genLetters[size_t(index)].second : 5;
    }
    return width;
}

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

#include "skin/DefaultSkin.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPolygon>

#include <cstring>

namespace newamp {

using namespace sprites;

namespace {

struct Palette
{
    QColor bodyTop{0x36, 0x3b, 0x48};
    QColor bodyBottom{0x24, 0x27, 0x30};
    QColor light{0x55, 0x5c, 0x6e};
    QColor shadow{0x14, 0x16, 0x1b};
    QColor display{0x0b, 0x0e, 0x12};
    QColor accent{0x38, 0xd6, 0xc4};
    QColor accentDim{0x1d, 0x6b, 0x63};
    QColor accentHot{0x9c, 0xff, 0xf1};
    QColor titleTop{0x2a, 0x6e, 0x8a};
    QColor titleBottom{0x1c, 0x45, 0x5a};
    QColor titleIdleTop{0x40, 0x45, 0x52};
    QColor titleIdleBottom{0x2c, 0x30, 0x3a};
};

const Palette P;

// 4x5 pixel glyphs, drawn into the 5x6 cells of the Text sheet.
struct Glyph
{
    char16_t ch;
    const char *rows[5];
};

const Glyph Glyphs[] = {
    {u'A', {".##.", "#..#", "####", "#..#", "#..#"}},
    {u'B', {"###.", "#..#", "###.", "#..#", "###."}},
    {u'C', {".###", "#...", "#...", "#...", ".###"}},
    {u'D', {"###.", "#..#", "#..#", "#..#", "###."}},
    {u'E', {"####", "#...", "###.", "#...", "####"}},
    {u'F', {"####", "#...", "###.", "#...", "#..."}},
    {u'G', {".###", "#...", "#.##", "#..#", ".###"}},
    {u'H', {"#..#", "#..#", "####", "#..#", "#..#"}},
    {u'I', {"###.", ".#..", ".#..", ".#..", "###."}},
    {u'J', {"...#", "...#", "...#", "#..#", ".##."}},
    {u'K', {"#..#", "#.#.", "##..", "#.#.", "#..#"}},
    {u'L', {"#...", "#...", "#...", "#...", "####"}},
    {u'M', {"#..#", "####", "####", "#..#", "#..#"}},
    {u'N', {"#..#", "##.#", "#.##", "#..#", "#..#"}},
    {u'O', {".##.", "#..#", "#..#", "#..#", ".##."}},
    {u'P', {"###.", "#..#", "###.", "#...", "#..."}},
    {u'Q', {".##.", "#..#", "#..#", "#.#.", ".#.#"}},
    {u'R', {"###.", "#..#", "###.", "#.#.", "#..#"}},
    {u'S', {".###", "#...", ".##.", "...#", "###."}},
    {u'T', {"###.", ".#..", ".#..", ".#..", ".#.."}},
    {u'U', {"#..#", "#..#", "#..#", "#..#", ".##."}},
    {u'V', {"#..#", "#..#", "#..#", ".##.", ".##."}},
    {u'W', {"#..#", "#..#", "####", "####", "#..#"}},
    {u'X', {"#..#", "#..#", ".##.", "#..#", "#..#"}},
    {u'Y', {"#.#.", "#.#.", ".#..", ".#..", ".#.."}},
    {u'Z', {"####", "..#.", ".#..", "#...", "####"}},
    {u'0', {".##.", "#.##", "##.#", "#..#", ".##."}},
    {u'1', {".#..", "##..", ".#..", ".#..", "###."}},
    {u'2', {"###.", "...#", ".##.", "#...", "####"}},
    {u'3', {"###.", "...#", ".##.", "...#", "###."}},
    {u'4', {"#..#", "#..#", "####", "...#", "...#"}},
    {u'5', {"####", "#...", "###.", "...#", "###."}},
    {u'6', {".##.", "#...", "###.", "#..#", ".##."}},
    {u'7', {"####", "...#", "..#.", ".#..", ".#.."}},
    {u'8', {".##.", "#..#", ".##.", "#..#", ".##."}},
    {u'9', {".##.", "#..#", ".###", "...#", ".##."}},
    {u'"', {"#.#.", "#.#.", "....", "....", "...."}},
    {u'@', {".##.", "#.##", "#.##", "#...", ".##."}},
    {u'…', {"....", "....", "....", "....", "#.#."}},
    {u'.', {"....", "....", "....", "....", ".#.."}},
    {u':', {"....", ".#..", "....", ".#..", "...."}},
    {u'(', {"..#.", ".#..", ".#..", ".#..", "..#."}},
    {u')', {".#..", "..#.", "..#.", "..#.", ".#.."}},
    {u'-', {"....", "....", "###.", "....", "...."}},
    {u'\'', {".#..", ".#..", "....", "....", "...."}},
    {u'!', {".#..", ".#..", ".#..", "....", ".#.."}},
    {u'_', {"....", "....", "....", "....", "####"}},
    {u'+', {"....", ".#..", "###.", ".#..", "...."}},
    {u'\\', {"#...", ".#..", "..#.", "...#", "...."}},
    {u'/', {"...#", "..#.", ".#..", "#...", "...."}},
    {u'[', {".##.", ".#..", ".#..", ".#..", ".##."}},
    {u']', {".##.", "..#.", "..#.", "..#.", ".##."}},
    {u'^', {".#..", "#.#.", "....", "....", "...."}},
    {u'&', {".#..", "#.#.", ".#..", "#.#.", ".#.#"}},
    {u'%', {"#..#", "..#.", ".#..", "#...", "#..#"}},
    {u',', {"....", "....", "....", ".#..", "#..."}},
    {u'=', {"....", "###.", "....", "###.", "...."}},
    {u'$', {".###", "#.#.", ".##.", ".#.#", "###."}},
    {u'#', {"#.#.", "####", "#.#.", "####", "#.#."}},
    {u'Å', {".##.", "#..#", "####", "#..#", "#..#"}},
    {u'Ö', {"#..#", ".##.", "#..#", "#..#", ".##."}},
    {u'Ä', {"#..#", ".##.", "#..#", "####", "#..#"}},
    {u'?', {"###.", "...#", ".##.", "....", ".#.."}},
    {u'*', {"....", "#.#.", ".#..", "#.#.", "...."}},
};

QPixmap blank(Sheet sheet, const QColor &fill = Qt::transparent)
{
    QPixmap pixmap(sheetSize(sheet));
    pixmap.fill(fill);
    return pixmap;
}

void bevel(QPainter &p, const QRect &r, bool raised)
{
    p.setPen(raised ? P.light : P.shadow);
    p.drawLine(r.topLeft(), r.topRight());
    p.drawLine(r.topLeft(), r.bottomLeft());
    p.setPen(raised ? P.shadow : P.light);
    p.drawLine(r.bottomLeft(), r.bottomRight());
    p.drawLine(r.topRight(), r.bottomRight());
}

void panel(QPainter &p, const QRect &r, const QColor &top, const QColor &bottom, bool raised)
{
    QLinearGradient g(r.topLeft(), r.bottomLeft());
    g.setColorAt(0, top);
    g.setColorAt(1, bottom);
    p.fillRect(r, g);
    bevel(p, r, raised);
}

void inset(QPainter &p, const QRect &r)
{
    p.fillRect(r, P.display);
    bevel(p, r, false);
}

QPixmap makeText()
{
    QPixmap pixmap = blank(Sheet::Text, P.display);
    QPainter p(&pixmap);
    p.setPen(P.accent);
    for (const Glyph &glyph : Glyphs) {
        const QPoint cell = Skin::glyphCell(QChar(glyph.ch));
        const QPoint origin(cell.x() * GlyphSize.width(), cell.y() * GlyphSize.height());
        for (int y = 0; y < 5; ++y)
            for (int x = 0; x < 4; ++x)
                if (glyph.rows[y][x] == '#')
                    p.drawPoint(origin + QPoint(x, y));
    }
    return pixmap;
}

QPixmap makeNumbers()
{
    // Seven-segment digits: segments a..g as rects inside a 9x13 cell.
    static const QRect Segments[7] = {
        {2, 1, 5, 1},  // a: top
        {7, 2, 1, 4},  // b: upper right
        {7, 7, 1, 4},  // c: lower right
        {2, 11, 5, 1}, // d: bottom
        {1, 7, 1, 4},  // e: lower left
        {1, 2, 1, 4},  // f: upper left
        {2, 6, 5, 1},  // g: middle
    };
    static const char *const Digits[10] = {
        "abcdef", "bc", "abdeg", "abcdg", "bcfg", "acdfg", "acdefg", "abc", "abcdefg", "abcdfg",
    };

    QPixmap pixmap = blank(Sheet::Numbers, P.display);
    QPainter p(&pixmap);
    for (int d = 0; d < 10; ++d) {
        const QPoint origin(d * DigitSize.width(), 0);
        for (int s = 0; s < 7; ++s) {
            const bool lit = std::strchr(Digits[d], 'a' + s) != nullptr;
            p.fillRect(Segments[s].translated(origin), lit ? P.accent : P.display.lighter(160));
        }
    }
    return pixmap;
}

QPixmap makePlayPause()
{
    QPixmap pixmap = blank(Sheet::PlayPause, P.display);
    QPainter p(&pixmap);
    p.setPen(Qt::NoPen);
    p.setBrush(P.accent);
    p.drawPolygon(QPolygon({QPoint(2, 1), QPoint(7, 4), QPoint(2, 7)}));
    p.fillRect(QRect(11, 1, 2, 7), P.accent);
    p.fillRect(QRect(14, 1, 2, 7), P.accent);
    p.fillRect(QRect(20, 2, 5, 5), P.accentDim);
    return pixmap;
}

void drawIcon(QPainter &p, const QRect &r, int which, const QColor &color)
{
    // Icons are drawn around the button's centre on whole pixels.
    const QPoint c = r.center();
    p.setPen(Qt::NoPen);
    p.setBrush(color);
    auto tri = [&](int x, int dir) {
        p.drawPolygon(QPolygon({QPoint(x, c.y() - 3), QPoint(x + 3 * dir, c.y()), QPoint(x, c.y() + 3)}));
    };
    switch (which) {
    case 0: // previous
        p.fillRect(QRect(c.x() - 5, c.y() - 3, 1, 7), color);
        tri(c.x() - 1, -1);
        tri(c.x() + 3, -1);
        break;
    case 1: // play
        p.drawPolygon(QPolygon({QPoint(c.x() - 2, c.y() - 4), QPoint(c.x() + 3, c.y()), QPoint(c.x() - 2, c.y() + 4)}));
        break;
    case 2: // pause
        p.fillRect(QRect(c.x() - 3, c.y() - 3, 2, 7), color);
        p.fillRect(QRect(c.x() + 1, c.y() - 3, 2, 7), color);
        break;
    case 3: // stop
        p.fillRect(QRect(c.x() - 3, c.y() - 3, 7, 7), color);
        break;
    case 4: // next
        tri(c.x() - 4, 1);
        tri(c.x(), 1);
        p.fillRect(QRect(c.x() + 4, c.y() - 3, 1, 7), color);
        break;
    case 5: // eject
        p.drawPolygon(QPolygon({QPoint(c.x() - 4, c.y()), QPoint(c.x(), c.y() - 4), QPoint(c.x() + 4, c.y())}));
        p.fillRect(QRect(c.x() - 4, c.y() + 2, 9, 2), color);
        break;
    }
}

QPixmap makeCButtons()
{
    QPixmap pixmap = blank(Sheet::CButtons);
    QPainter p(&pixmap);
    const Sprite buttons[] = {PreviousButton, PlayButton, PauseButton, StopButton, NextButton, EjectButton};
    for (int i = 0; i < 6; ++i) {
        const QRect n = buttons[i].normal.adjusted(0, 0, -1, -1);
        const QRect d = buttons[i].pressed.adjusted(0, 0, -1, -1);
        panel(p, n, P.light.darker(110), P.bodyBottom, true);
        drawIcon(p, n, i, P.accent);
        panel(p, d, P.bodyBottom, P.shadow, false);
        drawIcon(p, d.translated(1, 1), i, P.accentHot);
    }
    return pixmap;
}

QPixmap makeTitleBar(const QPixmap &text)
{
    QPixmap pixmap = blank(Sheet::TitleBar, P.bodyBottom);
    QPainter p(&pixmap);

    auto bar = [&](const QRect &r, const QColor &top, const QColor &bottom, const QColor &grip) {
        panel(p, r.adjusted(0, 0, -1, -1), top, bottom, true);
        // Grip lines either side of the title.
        for (int y = r.top() + 4; y <= r.top() + 9; y += 2) {
            p.fillRect(QRect(r.left() + 20, y, 95, 1), grip);
            p.fillRect(QRect(r.left() + 160, y, 75, 1), grip);
        }
        Skin label;
        label.setSheet(Sheet::Text, text);
        const QString title = QStringLiteral("NEWAMP");
        const int x = r.left() + 118 + (40 - Skin::textWidth(title)) / 2;
        p.fillRect(QRect(x - 2, r.top() + 3, Skin::textWidth(title) + 3, 8), P.display);
        label.drawText(p, {x, r.top() + 4}, title);
    };
    bar(TitleBarActive, P.titleTop, P.titleBottom, P.accentDim);
    bar(TitleBarInactive, P.titleIdleTop, P.titleIdleBottom, P.light);

    auto smallButton = [&](const Sprite &s, int icon) {
        for (const bool down : {false, true}) {
            const QRect r = (down ? s.pressed : s.normal).adjusted(0, 0, -1, -1);
            panel(p, r, down ? P.shadow : P.light, down ? P.bodyBottom : P.bodyTop, !down);
            const QColor c = down ? P.accentHot : P.accent;
            const QPoint o = r.topLeft() + QPoint(down ? 3 : 2, down ? 3 : 2);
            switch (icon) {
            case 0: p.fillRect(QRect(o, QSize(4, 4)), c); break;               // options
            case 1: p.fillRect(QRect(o + QPoint(0, 3), QSize(5, 1)), c); break; // minimize
            case 2:                                                             // close
                p.setPen(c);
                p.drawLine(o, o + QPoint(4, 4));
                p.drawLine(o + QPoint(0, 4), o + QPoint(4, 0));
                break;
            }
        }
    };
    smallButton(OptionsButton, 0);
    smallButton(MinimizeButton, 1);
    smallButton(CloseButton, 2);
    return pixmap;
}

QPixmap makePosBar()
{
    QPixmap pixmap = blank(Sheet::PosBar);
    QPainter p(&pixmap);
    inset(p, QRect(0, 3, 248, 4).adjusted(0, 0, -1, -1));
    panel(p, PosBarThumb.normal.adjusted(0, 0, -1, -1), P.light, P.bodyBottom, true);
    panel(p, PosBarThumb.pressed.adjusted(0, 0, -1, -1), P.accent, P.accentDim, true);
    for (const QRect &thumb : {PosBarThumb.normal, PosBarThumb.pressed})
        for (int x = 11; x <= 17; x += 3)
            p.fillRect(QRect(thumb.left() + x, 3, 1, 4), P.shadow);
    return pixmap;
}

QPixmap makeVolume()
{
    QPixmap pixmap = blank(Sheet::Volume);
    QPainter p(&pixmap);
    for (int step = 0; step < VolumeSteps; ++step) {
        const QRect bg = volumeBackground(step);
        const QRect groove(bg.left(), bg.top() + 4, bg.width() - 1, 4);
        inset(p, groove);
        const int filled = (groove.width() - 2) * step / (VolumeSteps - 1);
        QLinearGradient g(groove.topLeft(), groove.topRight());
        g.setColorAt(0, P.accentDim);
        g.setColorAt(1, P.accent);
        p.fillRect(QRect(groove.left() + 1, groove.top() + 1, filled, groove.height() - 1), g);
    }
    panel(p, VolumeThumb.normal.adjusted(0, 0, -1, -1), P.light, P.bodyBottom, true);
    panel(p, VolumeThumb.pressed.adjusted(0, 0, -1, -1), P.accent, P.accentDim, true);
    return pixmap;
}

QPixmap makeMain(const QPixmap &text)
{
    QPixmap pixmap(MainWindowSize);
    QPainter p(&pixmap);
    panel(p, QRect(QPoint(0, 0), MainWindowSize).adjusted(0, 0, -1, -1), P.bodyTop, P.bodyBottom, true);

    inset(p, QRect(10, 22, 96, 38));   // time, state and visualizer
    p.fillRect(QRect(71, 29, 2, 2), P.accent); // colon between minutes and seconds
    p.fillRect(QRect(71, 34, 2, 2), P.accent);
    inset(p, QRect(108, 22, 158, 13)); // title marquee
    inset(p, QRect(108, 38, 158, 15)); // future: bitrate, sample rate
    inset(p, QRect(14, 70, 252, 14));  // seek bar well

    Skin label;
    label.setSheet(Sheet::Text, text);
    p.fillRect(QRect(178, 59, 88, 10), P.bodyBottom);
    label.drawText(p, {186, 61}, QStringLiteral("NEWAMP ") + QStringLiteral(NEWAMP_VERSION));
    return pixmap;
}

} // namespace

Skin DefaultSkin::create()
{
    Skin skin;
    skin.setName(QStringLiteral("NewAmp Default"));
    const QPixmap text = makeText();
    skin.setSheet(Sheet::Text, text);
    skin.setSheet(Sheet::Main, makeMain(text));
    skin.setSheet(Sheet::TitleBar, makeTitleBar(text));
    skin.setSheet(Sheet::CButtons, makeCButtons());
    skin.setSheet(Sheet::Numbers, makeNumbers());
    skin.setSheet(Sheet::PosBar, makePosBar());
    skin.setSheet(Sheet::Volume, makeVolume());
    skin.setSheet(Sheet::PlayPause, makePlayPause());
    return skin;
}

} // namespace newamp

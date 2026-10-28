#include "skin/DefaultSkin.h"

#include <QLinearGradient>
#include <QPainter>
#include <QPolygon>

#include <cstring>
#include <initializer_list>

namespace newamp {

using namespace sprites;

namespace {

struct Palette
{
    QColor bodyTop;
    QColor bodyBottom;
    QColor light;
    QColor shadow;
    QColor display;
    QColor accent;
    QColor accentDim;
    QColor accentHot;
    QColor titleTop;
    QColor titleBottom;
    QColor titleIdleTop;
    QColor titleIdleBottom;
};

struct Theme
{
    const char *id;
    const char *name;
    Palette palette;
};

// NewAmp's built-in themes. Every one is drawn by the same code below; only
// the colours differ.
const Theme Themes[] = {
    {"slate", "NewAmp Slate",
     {{0x36, 0x3b, 0x48}, {0x24, 0x27, 0x30}, {0x55, 0x5c, 0x6e}, {0x14, 0x16, 0x1b}, {0x0b, 0x0e, 0x12},
      {0x38, 0xd6, 0xc4}, {0x1d, 0x6b, 0x63}, {0x9c, 0xff, 0xf1}, {0x2a, 0x6e, 0x8a}, {0x1c, 0x45, 0x5a},
      {0x40, 0x45, 0x52}, {0x2c, 0x30, 0x3a}}},
    {"amber", "Amber Terminal",
     {{0x3a, 0x30, 0x26}, {0x24, 0x1d, 0x16}, {0x5e, 0x4e, 0x3c}, {0x12, 0x0e, 0x0a}, {0x0d, 0x09, 0x05},
      {0xff, 0xb0, 0x30}, {0x7a, 0x4e, 0x10}, {0xff, 0xe0, 0xa0}, {0x8a, 0x55, 0x14}, {0x55, 0x33, 0x0c},
      {0x45, 0x3b, 0x30}, {0x2e, 0x26, 0x1e}}},
    {"glacier", "Glacier",
     {{0x2c, 0x3e, 0x5c}, {0x1a, 0x26, 0x3d}, {0x4f, 0x67, 0x8f}, {0x0c, 0x12, 0x1e}, {0x06, 0x0c, 0x18},
      {0x7f, 0xc8, 0xff}, {0x2b, 0x5c, 0x8a}, {0xd8, 0xf0, 0xff}, {0x3a, 0x7b, 0xc8}, {0x22, 0x4c, 0x85},
      {0x38, 0x46, 0x5e}, {0x26, 0x30, 0x44}}},
    {"ember", "Ember",
     {{0x3c, 0x26, 0x24}, {0x24, 0x15, 0x14}, {0x63, 0x3c, 0x38}, {0x12, 0x08, 0x07}, {0x0e, 0x05, 0x04},
      {0xff, 0x5a, 0x36}, {0x7f, 0x26, 0x14}, {0xff, 0xc4, 0x9a}, {0xa3, 0x2e, 0x1c}, {0x62, 0x18, 0x10},
      {0x46, 0x34, 0x32}, {0x2e, 0x22, 0x21}}},
    {"synthwave", "Synthwave",
     {{0x35, 0x22, 0x50}, {0x1e, 0x12, 0x33}, {0x5b, 0x3d, 0x85}, {0x0d, 0x06, 0x18}, {0x0a, 0x03, 0x14},
      {0xff, 0x4f, 0xd8}, {0x6e, 0x23, 0x80}, {0x6f, 0xf7, 0xff}, {0x8a, 0x2b, 0xb8}, {0x4a, 0x17, 0x75},
      {0x3e, 0x31, 0x52}, {0x29, 0x1f, 0x38}}},
    {"forest", "Forest",
     {{0x2e, 0x3a, 0x2c}, {0x1b, 0x24, 0x1a}, {0x4c, 0x5e, 0x48}, {0x0b, 0x10, 0x0a}, {0x07, 0x0c, 0x06},
      {0x8e, 0xe0, 0x5c}, {0x3c, 0x6b, 0x26}, {0xd9, 0xff, 0xb8}, {0x3f, 0x7a, 0x2c}, {0x27, 0x4d, 0x1b},
      {0x3a, 0x42, 0x38}, {0x27, 0x2e, 0x26}}},
    {"paper", "Paper",
     {{0xe9, 0xe6, 0xdf}, {0xcf, 0xcb, 0xc2}, {0xff, 0xff, 0xff}, {0x8a, 0x86, 0x7d}, {0xf8, 0xf6, 0xf0},
      {0x2b, 0x5d, 0xab}, {0x9a, 0xb3, 0xd9}, {0x0e, 0x2f, 0x66}, {0xb8, 0xc8, 0xe0}, {0x93, 0xa9, 0xcc},
      {0xd8, 0xd5, 0xce}, {0xc0, 0xbc, 0xb3}}},
};

// The palette being drawn with. Skins are generated on one thread at a time.
thread_local Palette P = Themes[0].palette;

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

const Glyph *findGlyph(QChar c)
{
    const QChar upper = c.toUpper();
    for (const Glyph &glyph : Glyphs)
        if (QChar(glyph.ch) == upper)
            return &glyph;
    return nullptr;
}

// Draws text straight from the glyph table, in any colour.
void label(QPainter &p, const QPoint &at, const QString &text, const QColor &color)
{
    p.save();
    p.setPen(color);
    int x = at.x();
    for (const QChar c : text) {
        if (const Glyph *glyph = findGlyph(c)) {
            for (int y = 0; y < 5; ++y)
                for (int gx = 0; gx < 4; ++gx)
                    if (glyph->rows[y][gx] == '#')
                        p.drawPoint(x + gx, at.y() + y);
        }
        x += GlyphSize.width();
    }
    p.restore();
}

void centredLabel(QPainter &p, const QRect &r, const QString &text, const QColor &color)
{
    const int width = Skin::textWidth(text) - 1;
    label(p, {r.left() + (r.width() - width) / 2, r.top() + (r.height() - 5) / 2}, text, color);
}

QPixmap makeText()
{
    QPixmap pixmap = blank(Sheet::Text, P.display);
    QPainter p(&pixmap);
    for (const Glyph &glyph : Glyphs) {
        const QPoint cell = Skin::glyphCell(QChar(glyph.ch));
        label(p, QPoint(cell.x() * GlyphSize.width(), cell.y() * GlyphSize.height()), QString(QChar(glyph.ch)),
              P.accent);
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

// A raised or sunken button face with a text label. Toggles get an LED
// on the left that lights when they're on.
enum class Led { None, Off, On };
void textButton(QPainter &p, const QRect &rect, const QString &text, bool down, bool lit, Led led = Led::None)
{
    const QRect r = rect.adjusted(0, 0, -1, -1);
    if (down)
        panel(p, r, P.bodyBottom, P.shadow, false);
    else
        panel(p, r, P.light.darker(110), P.bodyBottom, true);
    const int shift = down ? 1 : 0;
    QRect textArea = rect.translated(shift, shift);
    if (led != Led::None) {
        const QRect light(r.left() + 3 + shift, r.top() + (r.height() - 4) / 2 + shift, 2, 4);
        p.fillRect(light, led == Led::On ? P.accentHot : P.shadow);
        textArea.setLeft(textArea.left() + 4);
    }
    const QColor color = lit ? (down ? P.accentHot : P.accent) : P.light.lighter(130);
    centredLabel(p, textArea, text, color);
}

void toggleButton(QPainter &p, const ToggleSprite &s, const QString &text)
{
    textButton(p, s.normal, text, false, false, Led::Off);
    textButton(p, s.pressed, text, true, false, Led::Off);
    textButton(p, s.active, text, false, true, Led::On);
    textButton(p, s.activePressed, text, true, true, Led::On);
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

// A title bar with grip lines and a centred caption.
void titleBar(QPainter &p, const QRect &r, const QString &caption, bool active)
{
    panel(p, r.adjusted(0, 0, -1, -1), active ? P.titleTop : P.titleIdleTop,
          active ? P.titleBottom : P.titleIdleBottom, true);
    const int captionWidth = Skin::textWidth(caption) + 3;
    const int captionX = r.left() + (r.width() - captionWidth) / 2;
    const QColor grip = active ? P.accentDim : P.light;
    for (int y = r.top() + 4; y <= r.top() + r.height() - 5; y += 2) {
        p.fillRect(QRect(r.left() + 20, y, captionX - r.left() - 24, 1), grip);
        p.fillRect(QRect(captionX + captionWidth + 4, y, r.right() - 39 - captionX - captionWidth, 1), grip);
    }
    const QRect box(captionX, r.top() + (r.height() - 8) / 2, captionWidth, 8);
    p.fillRect(box, P.display);
    label(p, box.topLeft() + QPoint(2, 2), caption, active ? P.accent : P.light.lighter(130));
}

// A 9x9 title bar button: 0 options, 1 minimize, 2 close, 3 shade (roll
// up), 4 unshade (roll down).
void smallButton(QPainter &p, const QRect &rect, int icon, bool down)
{
    const QRect r = rect.adjusted(0, 0, -1, -1);
    panel(p, r, down ? P.shadow : P.light, down ? P.bodyBottom : P.bodyTop, !down);
    const QColor c = down ? P.accentHot : P.accent;
    const QPoint o = r.topLeft() + QPoint(down ? 3 : 2, down ? 3 : 2);
    switch (icon) {
    case 0: p.fillRect(QRect(o, QSize(4, 4)), c); break;
    case 1: p.fillRect(QRect(o + QPoint(0, 3), QSize(5, 1)), c); break;
    case 2:
        p.setPen(c);
        p.drawLine(o, o + QPoint(4, 4));
        p.drawLine(o + QPoint(0, 4), o + QPoint(4, 0));
        break;
    case 3: p.fillRect(QRect(o, QSize(5, 2)), c); break;
    case 4:
        p.setPen(c);
        p.drawRect(QRect(o, QSize(4, 4)));
        p.fillRect(QRect(o, QSize(5, 2)), c);
        break;
    }
}

// Draws a small pixel pattern ('#' lit) centred in r.
void pattern(QPainter &p, const QRect &r, std::initializer_list<const char *> rows, const QColor &color)
{
    const int w = int(std::strlen(*rows.begin()));
    const int h = int(rows.size());
    const QPoint at(r.left() + (r.width() - w) / 2, r.top() + (r.height() - h) / 2);
    int y = 0;
    for (const char *row : rows) {
        for (int x = 0; x < w; ++x)
            if (row[x] == '#')
                p.fillRect(QRect(at.x() + x, at.y() + y, 1, 1), color);
        ++y;
    }
}

// A windowshade bar: a title-coloured strip with grip lines between from
// and to (in bar coordinates).
void shadeBar(QPainter &p, const QRect &r, bool active, int gripFrom, int gripTo)
{
    panel(p, r.adjusted(0, 0, -1, -1), active ? P.titleTop : P.titleIdleTop,
          active ? P.titleBottom : P.titleIdleBottom, true);
    const QColor grip = active ? P.accentDim : P.light;
    for (int y = r.top() + 4; y <= r.top() + 9; y += 2)
        p.fillRect(QRect(r.left() + gripFrom, y, gripTo - gripFrom, 1), grip);
}

void captionBox(QPainter &p, const QPoint &at, const QString &caption, bool active)
{
    const QRect box(at, QSize(Skin::textWidth(caption) + 3, 8));
    p.fillRect(box, P.display);
    label(p, box.topLeft() + QPoint(2, 2), caption, active ? P.accent : P.light.lighter(130));
}

// The O A I D V strip, and each button's lit look in its column below.
void clutterBar(QPainter &p)
{
    const char *const letters[] = {"O", "A", "I", "D", "V"};
    const QRect strip = ClutterBarBackground;
    panel(p, strip.adjusted(0, 0, -1, -1), P.bodyBottom, P.bodyTop, false);
    for (int button = 0; button < ClutterButtonCount; ++button) {
        const QRect area = clutterButtonArea(button).translated(strip.topLeft() - ClutterBar.topLeft());
        const QRect lit = clutterButtonLit(button);
        const QString letter = QString::fromLatin1(letters[button]);
        const int dy = (area.height() - 5) / 2;
        label(p, area.topLeft() + QPoint(2, dy), letter, P.light.lighter(130));
        panel(p, lit.adjusted(0, 0, -1, -1), P.shadow, P.bodyBottom, false);
        label(p, lit.topLeft() + QPoint(2, dy), letter, P.accentHot);
    }
}

QPixmap makeTitleBar()
{
    QPixmap pixmap = blank(Sheet::TitleBar, P.bodyBottom);
    QPainter p(&pixmap);
    titleBar(p, TitleBarActive, QStringLiteral("NEWAMP"), true);
    titleBar(p, TitleBarInactive, QStringLiteral("NEWAMP"), false);
    const Sprite buttons[] = {OptionsButton, MinimizeButton, CloseButton, ShadeButton, UnshadeButton};
    for (int i = 0; i < 5; ++i) {
        smallButton(p, buttons[i].normal, i, false);
        smallButton(p, buttons[i].pressed, i, true);
    }

    // Windowshade bar: caption, a well for the time, transport icons.
    for (const bool active : {true, false}) {
        const QRect bar = active ? MainShadeActive : MainShadeInactive;
        const QPoint o = bar.topLeft();
        shadeBar(p, bar, active, 60, 120);
        captionBox(p, o + QPoint(18, 3), QStringLiteral("NEWAMP"), active);
        inset(p, QRect(o + MainShadeTime.topLeft() - QPoint(2, 2), MainShadeTime.size() + QSize(3, 3)));
        const QColor icon = active ? P.accent : P.light.lighter(130);
        pattern(p, MainShadePrevious.translated(o), {"#..#", "#.##", "####", "#.##", "#..#"}, icon);
        pattern(p, MainShadePlay.translated(o), {"#...", "##..", "###.", "##..", "#..."}, icon);
        pattern(p, MainShadePause.translated(o), {"##.##", "##.##", "##.##", "##.##", "##.##"}, icon);
        pattern(p, MainShadeStop.translated(o), {"####", "####", "####", "####"}, icon);
        pattern(p, MainShadeNext.translated(o), {"#..#", "##.#", "####", "##.#", "#..#"}, icon);
        pattern(p, MainShadeEject.translated(o), {"..#..", ".###.", "#####", ".....", "#####"}, icon);
        inset(p, MainShadePosBar.translated(o).adjusted(0, 2, -1, -2));
    }
    // Its seek bar: a groove and a 3px thumb.
    p.fillRect(MainShadePosBarBackground, P.bodyBottom);
    inset(p, MainShadePosBarBackground.adjusted(0, 2, -1, -2));
    p.fillRect(MainShadePosBarThumb.normal, P.accent);
    p.fillRect(MainShadePosBarThumb.normal.adjusted(1, 1, -1, -1), P.accentHot);
    clutterBar(p);
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

// Horizontal groove with a fill; fillFrom/fillTo are fractions of the width.
void hGroove(QPainter &p, const QRect &bg, double fillFrom, double fillTo)
{
    const QRect groove(bg.left(), bg.top() + 4, bg.width() - 1, 4);
    inset(p, groove);
    const int inner = groove.width() - 2;
    const int x0 = groove.left() + 1 + qRound(fillFrom * inner);
    const int x1 = groove.left() + 1 + qRound(fillTo * inner);
    QLinearGradient g(groove.topLeft(), groove.topRight());
    g.setColorAt(0, P.accentDim);
    g.setColorAt(1, P.accent);
    p.fillRect(QRect(x0, groove.top() + 1, x1 - x0, groove.height() - 1), g);
}

void sliderThumbs(QPainter &p, const Sprite &thumb)
{
    panel(p, thumb.normal.adjusted(0, 0, -1, -1), P.light, P.bodyBottom, true);
    panel(p, thumb.pressed.adjusted(0, 0, -1, -1), P.accent, P.accentDim, true);
}

QPixmap makeVolume()
{
    QPixmap pixmap = blank(Sheet::Volume);
    QPainter p(&pixmap);
    for (int step = 0; step < VolumeSteps; ++step)
        hGroove(p, volumeBackground(step), 0, double(step) / (VolumeSteps - 1));
    sliderThumbs(p, VolumeThumb);
    return pixmap;
}

QPixmap makeBalance()
{
    QPixmap pixmap = blank(Sheet::Balance);
    QPainter p(&pixmap);
    for (int step = 0; step < VolumeSteps; ++step) {
        // Balance art is symmetric: step is the distance from centre.
        const double half = 0.5 * step / (VolumeSteps - 1);
        hGroove(p, balanceBackground(step), 0.5 - half, 0.5 + half);
    }
    sliderThumbs(p, BalanceThumb);
    return pixmap;
}

QPixmap makeShufRep()
{
    QPixmap pixmap = blank(Sheet::ShufRep);
    QPainter p(&pixmap);
    toggleButton(p, RepeatButton, QStringLiteral("REP"));
    toggleButton(p, ShuffleButton, QStringLiteral("SHUFFLE"));
    toggleButton(p, EqToggleButton, QStringLiteral("EQ"));
    toggleButton(p, PlaylistToggleButton, QStringLiteral("PL"));
    return pixmap;
}

QPixmap makeMonoSter()
{
    QPixmap pixmap = blank(Sheet::MonoSter, P.display);
    QPainter p(&pixmap);
    const QColor unlit = P.display.lighter(220);
    centredLabel(p, StereoLit, QStringLiteral("STEREO"), P.accent);
    centredLabel(p, StereoUnlit, QStringLiteral("STEREO"), unlit);
    centredLabel(p, MonoLit, QStringLiteral("MONO"), P.accent);
    centredLabel(p, MonoUnlit, QStringLiteral("MONO"), unlit);
    return pixmap;
}

QPixmap makeMain()
{
    QPixmap pixmap(MainWindowSize);
    QPainter p(&pixmap);
    panel(p, QRect(QPoint(0, 0), MainWindowSize).adjusted(0, 0, -1, -1), P.bodyTop, P.bodyBottom, true);

    inset(p, QRect(20, 22, 86, 38));           // time, state and visualizer; the clutter bar is left of it
    p.fillRect(QRect(71, 29, 2, 2), P.accent); // colon between minutes and seconds
    p.fillRect(QRect(71, 34, 2, 2), P.accent);
    inset(p, QRect(108, 22, 158, 13));         // title marquee
    inset(p, QRect(108, 38, 162, 15));         // bitrate and channels
    label(p, {128, 43}, QStringLiteral("KBPS"), P.accentDim);
    label(p, {168, 43}, QStringLiteral("KHZ"), P.accentDim);
    inset(p, QRect(14, 70, 252, 14));          // seek bar well
    return pixmap;
}

QPixmap makeEqMain()
{
    QPixmap pixmap = blank(Sheet::EqMain, P.bodyBottom);
    QPainter p(&pixmap);

    // Window body
    panel(p, EqBackground.adjusted(0, 0, -1, -1), P.bodyTop, P.bodyBottom, true);
    inset(p, QRect(EqGraphPos - QPoint(1, 1), QSize(115, 21)));
    const QRect wells[] = {QRect(EqPreampPos, EqSliderSize)};
    inset(p, wells[0].adjusted(-2, -2, 2, 2));
    inset(p, QRect(eqBandPos(0) - QPoint(3, 2), QPoint(eqBandPos(EqBandCount - 1).x() + EqSliderSize.width() + 2,
                                                       EqPreampPos.y() + EqSliderSize.height() + 1)));
    label(p, {18, 105}, QStringLiteral("PREAMP"), P.accent);
    label(p, {44, 39}, QStringLiteral("+12DB"), P.accentDim);
    label(p, {49, 67}, QStringLiteral("0DB"), P.accentDim);
    label(p, {44, 95}, QStringLiteral("-12DB"), P.accentDim);
    static const char *const Bands[EqBandCount] = {"60", "170", "310", "600", "1K", "3K", "6K", "12K", "14K", "16K"};
    for (int band = 0; band < EqBandCount; ++band) {
        const QString text = QString::fromLatin1(Bands[band]);
        label(p, {eqBandPos(band).x() + (EqSliderSize.width() - Skin::textWidth(text) + 1) / 2, 105}, text,
              P.accent);
    }

    // Title bars and close button
    titleBar(p, EqTitleBarActive, QStringLiteral("EQUALIZER"), true);
    titleBar(p, EqTitleBarInactive, QStringLiteral("EQUALIZER"), false);
    smallButton(p, EqCloseButton.normal, 2, false);
    smallButton(p, EqCloseButton.pressed, 2, true);
    for (const QRect &title : {EqTitleBarActive, EqTitleBarInactive})
        smallButton(p, QRect(title.topLeft() + EqShadeButtonPos, QSize(9, 9)), 3, false);

    // On, Auto, Presets
    toggleButton(p, EqOnButton, QStringLiteral("ON"));
    toggleButton(p, EqAutoButton, QStringLiteral("AUTO"));
    textButton(p, EqPresetsButton.normal, QStringLiteral("PRESETS"), false, true);
    textButton(p, EqPresetsButton.pressed, QStringLiteral("PRESETS"), true, true);

    // Vertical slider backgrounds, filled from the bottom.
    for (int step = 0; step < EqSliderSteps; ++step) {
        const QRect bg = eqSliderBackground(step);
        p.fillRect(bg, P.display);
        const QRect groove(bg.left() + 5, bg.top(), 4, bg.height() - 1);
        inset(p, groove);
        const int filled = (groove.height() - 2) * step / (EqSliderSteps - 1);
        QLinearGradient g(groove.bottomLeft(), groove.topLeft());
        g.setColorAt(0, P.accentDim);
        g.setColorAt(1, P.accent);
        p.fillRect(QRect(groove.left() + 1, groove.bottom() - filled, groove.width() - 1, filled), g);
    }
    sliderThumbs(p, EqSliderThumb);

    // Response graph
    p.fillRect(EqGraphBackground, P.display);
    p.fillRect(QRect(EqGraphBackground.left(), EqGraphBackground.top() + 9, EqGraphBackground.width(), 1),
               P.display.lighter(200));
    for (int y = 0; y < EqGraphLineColors.height(); ++y) {
        // Top of the graph (boost) is hot, bottom (cut) is dim.
        const double t = double(y) / (EqGraphLineColors.height() - 1);
        const QColor c = QColor::fromRgbF(P.accentHot.redF() * (1 - t) + P.accentDim.redF() * t,
                                          P.accentHot.greenF() * (1 - t) + P.accentDim.greenF() * t,
                                          P.accentHot.blueF() * (1 - t) + P.accentDim.blueF() * t);
        p.fillRect(QRect(EqGraphLineColors.left(), EqGraphLineColors.top() + y, 1, 1), c);
    }
    p.fillRect(EqPreampLine, P.light);
    return pixmap;
}

QPixmap makePlEdit()
{
    QPixmap pixmap = blank(Sheet::PlEdit, P.bodyBottom);
    QPainter p(&pixmap);

    for (const bool active : {true, false}) {
        const auto pick = [active](const ActiveSprite &s) { return active ? s.active : s.inactive; };
        const QColor top = active ? P.titleTop : P.titleIdleTop;
        const QColor bottom = active ? P.titleBottom : P.titleIdleBottom;
        const QColor grip = active ? P.accentDim : P.light;
        for (const QRect &r : {pick(PlTopLeft), pick(PlTopTile), pick(PlTopRight)}) {
            p.fillRect(r, P.bodyTop);
            panel(p, QRect(r.left(), r.top() + 2, r.width(), 13).adjusted(0, 0, 0, -1), top, bottom, true);
            for (int y = r.top() + 6; y <= r.top() + 11; y += 2)
                p.fillRect(QRect(r.left() + (r == pick(PlTopLeft) ? 8 : 0), y,
                                 r == pick(PlTopRight) ? 10 : r.width() - (r == pick(PlTopLeft) ? 8 : 0), 1),
                           grip);
            p.fillRect(QRect(r.left(), r.top() + 15, r.width(), 5), P.bodyBottom);
        }
        // Body edges of the corners
        p.fillRect(QRect(pick(PlTopLeft).left(), pick(PlTopLeft).top() + 15, 1, 5), P.light);
        p.fillRect(QRect(pick(PlTopRight).right(), pick(PlTopRight).top() + 15, 1, 5), P.shadow);
        // Shade and close buttons drawn into the top-right corner.
        const QRect corner = pick(PlTopRight);
        smallButton(p, QRect(corner.right() + 1 - PlShadeButtonInset, corner.top() + 3, 9, 9), 3, false);
        smallButton(p, QRect(corner.right() + 1 - PlCloseButtonInset, corner.top() + 3, 9, 9), 2, false);

        const QRect title = pick(PlTitle);
        p.fillRect(title, P.bodyTop);
        panel(p, QRect(title.left(), title.top() + 2, title.width(), 12), top, bottom, true);
        const QRect box(title.left() + 25, title.top() + 4, 50, 8);
        p.fillRect(box, P.display);
        centredLabel(p, box, QStringLiteral("PLAYLIST"), active ? P.accent : P.light.lighter(130));
        p.fillRect(QRect(title.left(), title.top() + 15, title.width(), 5), P.bodyBottom);
    }

    // Side tiles: left border, right border with scrollbar groove.
    p.fillRect(PlLeftTile, P.bodyBottom);
    p.fillRect(QRect(PlLeftTile.left(), PlLeftTile.top(), 1, PlLeftTile.height()), P.light);
    p.fillRect(QRect(PlLeftTile.right(), PlLeftTile.top(), 1, PlLeftTile.height()), P.shadow);
    p.fillRect(PlRightTile, P.bodyBottom);
    p.fillRect(QRect(PlRightTile.left(), PlRightTile.top(), 1, PlRightTile.height()), P.light);
    p.fillRect(QRect(PlRightTile.left() + 5, PlRightTile.top(), 8, PlRightTile.height()), P.display);
    p.fillRect(QRect(PlRightTile.right(), PlRightTile.top(), 1, PlRightTile.height()), P.shadow);

    smallButton(p, PlClosePressed, 2, true);
    smallButton(p, PlShadePressed, 3, true);
    smallButton(p, PlUnshadePressed, 4, true);
    sliderThumbs(p, PlScrollHandle);

    // Windowshade bar: a text well runs along the whole bar, ending at the
    // unshade and close buttons in the right piece.
    const auto well = [&p](const QRect &r, bool leftEnd, bool rightEnd) {
        const QRect strip(r.left(), r.top() + 2, r.width(), 10);
        p.fillRect(strip, P.display);
        p.fillRect(QRect(strip.left(), strip.top(), strip.width(), 1), P.shadow);
        p.fillRect(QRect(strip.left(), strip.bottom(), strip.width(), 1), P.light);
        if (leftEnd)
            p.fillRect(QRect(strip.left(), strip.top(), 1, strip.height()), P.shadow);
        if (rightEnd)
            p.fillRect(QRect(strip.right(), strip.top(), 1, strip.height()), P.light);
    };
    // Bevelled only along the outside, so the tiles join without seams.
    for (const QRect &r : {PlShadeLeft, PlShadeTile, PlShadeRight.active, PlShadeRight.inactive}) {
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, P.bodyTop);
        g.setColorAt(1, P.bodyBottom);
        p.fillRect(r, g);
        p.fillRect(QRect(r.left(), r.top(), r.width(), 1), P.light);
        p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), P.shadow);
    }
    p.fillRect(QRect(PlShadeLeft.left(), PlShadeLeft.top(), 1, PlShadeLeft.height()), P.light);
    for (const QRect &r : {PlShadeRight.active, PlShadeRight.inactive})
        p.fillRect(QRect(r.right(), r.top(), 1, r.height()), P.shadow);
    well(PlShadeLeft.adjusted(3, 0, 0, 0), true, false);
    well(PlShadeTile, false, false);
    for (const bool active : {true, false}) {
        const QRect r = active ? PlShadeRight.active : PlShadeRight.inactive;
        well(QRect(r.left(), r.top(), 26, r.height()), false, true);
        p.fillRect(QRect(r.left() + 27, r.top() + 4, 1, 6), active ? P.accent : P.light);
        smallButton(p, QRect(r.topLeft() + QPoint(r.width() - PlShadeButtonInset, 3), QSize(9, 9)), 4, false);
        smallButton(p, QRect(r.topLeft() + QPoint(r.width() - PlCloseButtonInset, 3), QSize(9, 9)), 2, false);
    }

    // Bottom pieces
    for (const QRect &r : {PlBottomLeft, PlBottomRight, PlBottomTile}) {
        panel(p, r.adjusted(0, 0, -1, -1), P.bodyTop, P.bodyBottom, false);
        p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), P.shadow);
    }
    p.fillRect(QRect(PlBottomLeft.left(), PlBottomLeft.top(), 1, PlBottomLeft.height()), P.light);
    p.fillRect(QRect(PlBottomRight.right(), PlBottomRight.top(), 1, PlBottomRight.height()), P.shadow);
    const char *const Buttons[] = {"ADD", "REM", "SEL", "MISC"};
    for (int i = 0; i < 4; ++i)
        textButton(p, QRect(PlBottomLeft.left() + 14 + i * 29, PlBottomLeft.top() + 8, 22, 18),
                   QString::fromLatin1(Buttons[i]), false, true);
    textButton(p, QRect(PlBottomRight.left() + 106, PlBottomRight.top() + 8, 22, 18), QStringLiteral("LIST"),
               false, true);
    inset(p, QRect(PlBottomRight.left() + 7, PlBottomRight.top() + 8, 94, 18));
    // Resize grip
    for (int i = 0; i < 3; ++i)
        p.fillRect(QRect(PlBottomRight.right() - 4 - i * 3, PlBottomRight.bottom() - 4, 2, 2), P.light);
    return pixmap;
}

QPixmap makeEqEx()
{
    QPixmap pixmap = blank(Sheet::EqEx, P.bodyBottom);
    QPainter p(&pixmap);
    for (const bool active : {true, false}) {
        const QRect bar = active ? EqShadeActive : EqShadeInactive;
        const QPoint o = bar.topLeft();
        shadeBar(p, bar, active, 232, 250);
        captionBox(p, o + QPoint(6, 3), QStringLiteral("EQ"), active);
        label(p, o + QPoint(25, 5), QStringLiteral("VOL"), active ? P.accent : P.light.lighter(130));
        label(p, o + QPoint(EqShadeBalance.right() + 4, 5), QStringLiteral("BAL"),
              active ? P.accent : P.light.lighter(130));
        inset(p, EqShadeVolume.translated(o).adjusted(0, 2, -1, -2));
        inset(p, EqShadeBalance.translated(o).adjusted(0, 2, -1, -2));
        smallButton(p, QRect(o + EqShadeButtonPos, QSize(9, 9)), 4, false);
        smallButton(p, QRect(o + EqCloseButtonPos, QSize(9, 9)), 2, false);
    }
    // Slider thumbs: left, centre and right looks, all alike here.
    for (int x : {1, 4, 7, 11, 14, 17}) {
        p.fillRect(QRect(x, 30, 3, 7), P.accent);
        p.fillRect(QRect(x + 1, 31, 1, 5), P.accentHot);
    }
    smallButton(p, EqUnshadePressed, 4, true);
    smallButton(p, EqShadePressed, 3, true);
    smallButton(p, EqShadeCloseButton.normal, 2, false);
    smallButton(p, EqShadeCloseButton.pressed, 2, true);
    return pixmap;
}

QPixmap makeGen()
{
    // The cyan key colour is only seen between the title letters, where it
    // tells the letter scanner where one letter ends and the next begins.
    QPixmap pixmap = blank(Sheet::Gen, QColor(0x00, 0xc6, 0xff));
    QPainter p(&pixmap);

    for (const bool active : {true, false}) {
        const auto pick = [active](const ActiveSprite &s) { return active ? s.active : s.inactive; };
        const QColor top = active ? P.titleTop : P.titleIdleTop;
        const QColor bottom = active ? P.titleBottom : P.titleIdleBottom;
        const QColor grip = active ? P.accentDim : P.light;
        for (const ActiveSprite *s : {&GenTopLeft, &GenTopLeftEnd, &GenTopCenterFill, &GenTopRightEnd, &GenTopFill,
                                      &GenTopRight}) {
            const QRect r = pick(*s);
            p.fillRect(r, P.bodyTop);
            panel(p, QRect(r.left(), r.top() + 2, r.width(), 13).adjusted(0, 0, 0, -1), top, bottom, true);
            p.fillRect(QRect(r.left(), r.top() + 15, r.width(), 5), P.bodyBottom);
            // Grip lines everywhere except where the title sits, stopping a
            // few pixels short of it at the end pieces.
            int from = r.left(), to = r.right();
            if (s == &GenTopCenterFill)
                continue;
            if (s == &GenTopLeft)
                from += 6;
            if (s == &GenTopLeftEnd)
                to -= 4;
            if (s == &GenTopRightEnd)
                from += 4;
            if (s == &GenTopRight)
                to -= 13;
            for (int y = r.top() + 6; y <= r.top() + 11; y += 2)
                p.fillRect(QRect(from, y, to - from + 1, 1), grip);
        }
        const QRect corner = pick(GenTopRight);
        smallButton(p, QRect(corner.topLeft() + GenCloseOffset, QSize(9, 9)), 2, false);
        // Outer edges and the top of the content well.
        p.fillRect(QRect(pick(GenTopLeft).left(), pick(GenTopLeft).top() + 15, 1, 5), P.light);
        p.fillRect(QRect(corner.right(), corner.top() + 15, 1, 5), P.shadow);
    }
    smallButton(p, GenClosePressed, 2, true);

    // Sides: an outer bevel and a dark line framing the content.
    for (const QRect &r : {GenLeftTile, GenLeftBottom}) {
        p.fillRect(r, P.bodyBottom);
        p.fillRect(QRect(r.left(), r.top(), 1, r.height()), P.light);
        p.fillRect(QRect(r.right(), r.top(), 1, r.height()), P.shadow);
    }
    for (const QRect &r : {GenRightTile, GenRightBottom}) {
        p.fillRect(r, P.bodyBottom);
        p.fillRect(QRect(r.left(), r.top(), 1, r.height()), P.shadow);
        p.fillRect(QRect(r.right(), r.top(), 1, r.height()), P.shadow.darker(150));
    }

    for (const QRect &r : {GenBottomLeft, GenBottomRight, GenBottomFill}) {
        QLinearGradient g(r.topLeft(), r.bottomLeft());
        g.setColorAt(0, P.bodyBottom);
        g.setColorAt(1, P.bodyBottom.darker(120));
        p.fillRect(r, g);
        p.fillRect(QRect(r.left(), r.top(), r.width(), 1), P.light);
        p.fillRect(QRect(r.left(), r.bottom(), r.width(), 1), P.shadow);
    }
    p.fillRect(QRect(GenBottomLeft.left(), GenBottomLeft.top(), 1, GenBottomLeft.height()), P.light);
    p.fillRect(QRect(GenBottomRight.right(), GenBottomRight.top(), 1, GenBottomRight.height()), P.shadow);
    for (int i = 0; i < 3; ++i)
        p.fillRect(QRect(GenBottomRight.right() - 4 - i * 3, GenBottomRight.bottom() - 4, 2, 2), P.light);

    // Title letters: a 6x7 box per letter, one key-coloured column apart.
    for (int i = 0; i < 26; ++i) {
        const QString letter(QChar(u'A' + i));
        for (const bool active : {true, false}) {
            const QRect box(1 + i * 7, active ? GenTextActiveY : GenTextInactiveY, 6, GenTextHeight);
            p.fillRect(box, P.display);
            const QColor dim((P.accent.red() + P.display.red()) / 2, (P.accent.green() + P.display.green()) / 2,
                             (P.accent.blue() + P.display.blue()) / 2);
            label(p, box.topLeft() + QPoint(1, 1), letter, active ? P.accent : dim);
        }
    }
    return pixmap;
}

} // namespace

QList<DefaultSkin::ThemeInfo> DefaultSkin::themes()
{
    QList<ThemeInfo> list;
    for (const Theme &theme : Themes)
        list.append({QString::fromLatin1(theme.id), QString::fromLatin1(theme.name)});
    return list;
}

Skin DefaultSkin::create(const QString &themeId)
{
    const Theme *theme = &Themes[0];
    for (const Theme &t : Themes)
        if (themeId == QLatin1String(t.id))
            theme = &t;
    P = theme->palette;

    Skin skin;
    skin.setName(QString::fromLatin1(theme->name));
    skin.setSheet(Sheet::Text, makeText());
    skin.setSheet(Sheet::Main, makeMain());
    skin.setSheet(Sheet::TitleBar, makeTitleBar());
    skin.setSheet(Sheet::CButtons, makeCButtons());
    skin.setSheet(Sheet::Numbers, makeNumbers());
    skin.setSheet(Sheet::PosBar, makePosBar());
    skin.setSheet(Sheet::Volume, makeVolume());
    skin.setSheet(Sheet::PlayPause, makePlayPause());
    skin.setSheet(Sheet::ShufRep, makeShufRep());
    skin.setSheet(Sheet::MonoSter, makeMonoSter());
    skin.setSheet(Sheet::Balance, makeBalance());
    skin.setSheet(Sheet::EqMain, makeEqMain());
    skin.setSheet(Sheet::PlEdit, makePlEdit());
    skin.setSheet(Sheet::Gen, makeGen());
    skin.setSheet(Sheet::EqEx, makeEqEx());

    PlaylistStyle style;
    style.normal = P.accent;
    style.current = P.accentHot;
    style.normalBackground = P.display;
    style.selectedBackground = P.titleBottom;
    skin.setPlaylistStyle(style);

    VisColors vis;
    vis[0] = P.display;
    vis[1] = P.display.lighter(220);
    for (int i = 0; i < 16; ++i) {
        // Spectrum: hot at the top, dim at the bottom.
        const double t = i / 15.0;
        vis[size_t(2 + i)] = QColor::fromRgbF(P.accentHot.redF() * (1 - t) + P.accentDim.redF() * t,
                                              P.accentHot.greenF() * (1 - t) + P.accentDim.greenF() * t,
                                              P.accentHot.blueF() * (1 - t) + P.accentDim.blueF() * t);
    }
    for (int i = 0; i < 5; ++i)
        vis[size_t(18 + i)] = P.accent.darker(100 + i * 20);
    vis[23] = P.accentHot;
    skin.setVisColors(vis);
    return skin;
}

} // namespace newamp

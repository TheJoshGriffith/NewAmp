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

// A 9x9 title bar button: 0 options, 1 minimize, 2 close.
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
    }
}

QPixmap makeTitleBar()
{
    QPixmap pixmap = blank(Sheet::TitleBar, P.bodyBottom);
    QPainter p(&pixmap);
    titleBar(p, TitleBarActive, QStringLiteral("NEWAMP"), true);
    titleBar(p, TitleBarInactive, QStringLiteral("NEWAMP"), false);
    const Sprite buttons[] = {OptionsButton, MinimizeButton, CloseButton};
    for (int i = 0; i < 3; ++i) {
        smallButton(p, buttons[i].normal, i, false);
        smallButton(p, buttons[i].pressed, i, true);
    }
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

    inset(p, QRect(10, 22, 96, 38));           // time, state and visualizer
    p.fillRect(QRect(71, 29, 2, 2), P.accent); // colon between minutes and seconds
    p.fillRect(QRect(71, 34, 2, 2), P.accent);
    inset(p, QRect(108, 22, 158, 13));         // title marquee
    inset(p, QRect(108, 38, 162, 15));         // bitrate and channels
    label(p, {128, 43}, QStringLiteral("KBPS"), P.accentDim);
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
        // Close button drawn into the top-right corner.
        smallButton(p, QRect(pick(PlTopRight).left() + 14, pick(PlTopRight).top() + 3, 9, 9), 2, false);

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
    sliderThumbs(p, PlScrollHandle);

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

} // namespace

Skin DefaultSkin::create()
{
    Skin skin;
    skin.setName(QStringLiteral("NewAmp Default"));
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

    PlaylistStyle style;
    style.normal = P.accent;
    style.current = P.accentHot;
    style.normalBackground = P.display;
    style.selectedBackground = P.titleBottom;
    skin.setPlaylistStyle(style);
    return skin;
}

} // namespace newamp

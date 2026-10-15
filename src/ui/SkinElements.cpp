#include "ui/SkinElements.h"

#include "skin/Skin.h"

#include <QPainter>

namespace newamp {

using namespace sprites;

SkinImage::SkinImage(SkinnedWindow *window, const QPoint &pos, Sheet sheet, const QRect &source)
    : SkinElement(window, QRect(pos, source.size()))
    , m_sheet(sheet)
    , m_source(source)
{
}

void SkinImage::setSource(const QRect &source)
{
    if (source == m_source)
        return;
    m_source = source;
    update();
}

void SkinImage::paint(QPainter &painter, const Skin &skin) const
{
    skin.drawSprite(painter, rect().topLeft(), m_sheet, m_source);
}

SkinButton::SkinButton(SkinnedWindow *window, const QPoint &pos, Sheet sheet, const Sprite &sprite,
                       std::function<void()> onClick)
    : SkinElement(window, QRect(pos, sprite.normal.size()))
    , m_sheet(sheet)
    , m_sprite(sprite)
    , m_onClick(std::move(onClick))
{
}

void SkinButton::paint(QPainter &painter, const Skin &skin) const
{
    skin.drawSprite(painter, rect().topLeft(), m_sheet, m_down ? m_sprite.pressed : m_sprite.normal);
}

void SkinButton::mousePress(const QPoint &)
{
    m_pressed = true;
    setDown(true);
}

void SkinButton::mouseMove(const QPoint &pos)
{
    if (m_pressed)
        setDown(rect().contains(pos));
}

void SkinButton::mouseRelease(const QPoint &pos)
{
    const bool fire = m_pressed && rect().contains(pos);
    m_pressed = false;
    setDown(false);
    if (fire && m_onClick)
        m_onClick();
}

void SkinButton::setDown(bool down)
{
    if (down == m_down)
        return;
    m_down = down;
    update();
}

SkinSlider::SkinSlider(SkinnedWindow *window, const QRect &rect, Sheet sheet, BackgroundFn background,
                       const Sprite &thumb, int thumbY)
    : SkinElement(window, rect)
    , m_sheet(sheet)
    , m_background(std::move(background))
    , m_thumb(thumb)
    , m_thumbY(thumbY)
{
}

void SkinSlider::setValue(double value)
{
    value = qBound(0.0, value, 1.0);
    if (m_dragging || qFuzzyCompare(value + 1.0, m_value + 1.0))
        return;
    const int oldX = thumbX();
    m_value = value;
    // Background may change with value (volume), so repaint the whole slider
    // unless only the thumb moved by less than a pixel.
    if (thumbX() != oldX || m_background)
        update();
}

int SkinSlider::thumbX() const
{
    return rect().left() + qRound(m_value * (rect().width() - m_thumb.normal.width()));
}

double SkinSlider::valueAt(const QPoint &pos) const
{
    const int span = rect().width() - m_thumb.normal.width();
    if (span <= 0)
        return 0.0;
    return qBound(0.0, double(pos.x() - m_grabOffset - rect().left()) / span, 1.0);
}

void SkinSlider::paint(QPainter &painter, const Skin &skin) const
{
    if (m_background)
        skin.drawSprite(painter, rect().topLeft(), m_sheet, m_background(m_value));
    skin.drawSprite(painter, {thumbX(), rect().top() + m_thumbY}, m_sheet,
                    m_dragging ? m_thumb.pressed : m_thumb.normal);
}

void SkinSlider::mousePress(const QPoint &pos)
{
    const int x = thumbX();
    const int w = m_thumb.normal.width();
    // Grabbing the thumb keeps its offset; clicking the track centres it.
    m_grabOffset = (pos.x() >= x && pos.x() < x + w) ? pos.x() - x : w / 2;
    m_dragging = true;
    m_value = valueAt(pos);
    update();
    if (onMoved)
        onMoved(m_value);
}

void SkinSlider::mouseMove(const QPoint &pos)
{
    if (!m_dragging)
        return;
    m_value = valueAt(pos);
    update();
    if (onMoved)
        onMoved(m_value);
}

void SkinSlider::mouseRelease(const QPoint &pos)
{
    if (!m_dragging)
        return;
    m_value = valueAt(pos);
    m_dragging = false;
    update();
    if (onReleased)
        onReleased(m_value);
}

SkinTimeDisplay::SkinTimeDisplay(SkinnedWindow *window, const QRect &rect)
    : SkinElement(window, rect)
{
}

void SkinTimeDisplay::setSeconds(qint64 seconds)
{
    if (seconds == m_seconds)
        return;
    m_seconds = seconds;
    update();
}

void SkinTimeDisplay::paint(QPainter &painter, const Skin &skin) const
{
    if (m_seconds < 0)
        return;
    const qint64 minutes = qMin<qint64>(m_seconds / 60, 99);
    const int digits[4] = {int(minutes / 10), int(minutes % 10), int(m_seconds % 60 / 10),
                           int(m_seconds % 10)};
    for (int i = 0; i < 4; ++i) {
        const QRect source(QPoint(digits[i] * DigitSize.width(), 0), DigitSize);
        skin.drawSprite(painter, rect().topLeft() + QPoint(TimeDigitOffsets[i], 0), Sheet::Numbers, source);
    }
}

SkinMarquee::SkinMarquee(SkinnedWindow *window, const QRect &rect)
    : SkinElement(window, rect)
{
}

void SkinMarquee::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    m_offset = 0;
    update();
}

void SkinMarquee::tick()
{
    if (Skin::textWidth(m_text) <= rect().width())
        return;
    // Loop the text with a separator, like the classic player.
    const int loop = Skin::textWidth(m_text + QStringLiteral("  ***  "));
    m_offset = (m_offset + GlyphSize.width()) % loop;
    update();
}

void SkinMarquee::paint(QPainter &painter, const Skin &skin) const
{
    painter.save();
    painter.setClipRect(rect());
    if (Skin::textWidth(m_text) <= rect().width()) {
        skin.drawText(painter, rect().topLeft(), m_text);
    } else {
        const QString looped = m_text + QStringLiteral("  ***  ");
        const int loop = Skin::textWidth(looped);
        const QPoint start = rect().topLeft() - QPoint(m_offset, 0);
        skin.drawText(painter, start, looped);
        skin.drawText(painter, start + QPoint(loop, 0), looped);
    }
    painter.restore();
}

} // namespace newamp

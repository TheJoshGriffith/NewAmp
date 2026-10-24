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
    if (fire)
        clicked();
}

void SkinButton::clicked()
{
    if (m_onClick)
        m_onClick();
}

void SkinButton::setDown(bool down)
{
    if (down == m_down)
        return;
    m_down = down;
    update();
}

SkinToggleButton::SkinToggleButton(SkinnedWindow *window, const QPoint &pos, Sheet sheet,
                                   const ToggleSprite &sprite, std::function<void(bool)> onToggled)
    : SkinButton(window, pos, sheet, {sprite.normal, sprite.pressed}, {})
    , m_toggle(sprite)
    , m_onToggled(std::move(onToggled))
{
}

void SkinToggleButton::setChecked(bool checked)
{
    if (checked == m_checked)
        return;
    m_checked = checked;
    update();
}

void SkinToggleButton::paint(QPainter &painter, const Skin &skin) const
{
    const QRect source = m_checked ? (isDown() ? m_toggle.activePressed : m_toggle.active)
                                   : (isDown() ? m_toggle.pressed : m_toggle.normal);
    skin.drawSprite(painter, rect().topLeft(), sheet(), source);
}

void SkinToggleButton::clicked()
{
    setChecked(!m_checked);
    if (m_onToggled)
        m_onToggled(m_checked);
}

SkinHotspot::SkinHotspot(SkinnedWindow *window, const QRect &rect, std::function<void()> onClick)
    : SkinElement(window, rect)
    , m_onClick(std::move(onClick))
{
}

void SkinHotspot::mouseRelease(const QPoint &pos)
{
    if (rect().contains(pos) && m_onClick)
        m_onClick();
}

SkinSlider::SkinSlider(SkinnedWindow *window, const QRect &rect, Sheet sheet, BackgroundFn background,
                       const Sprite &thumb, Qt::Orientation orientation, const QPoint &thumbOffset)
    : SkinElement(window, rect)
    , m_sheet(sheet)
    , m_background(std::move(background))
    , m_thumb(thumb)
    , m_orientation(orientation)
    , m_thumbOffset(thumbOffset)
{
}

int SkinSlider::span() const
{
    return m_orientation == Qt::Horizontal ? rect().width() - m_thumb.normal.width()
                                           : rect().height() - m_thumb.normal.height();
}

int SkinSlider::thumbOffset() const
{
    return qRound(m_value * span());
}

int SkinSlider::along(const QPoint &pos) const
{
    return m_orientation == Qt::Horizontal ? pos.x() - rect().left() : pos.y() - rect().top();
}

void SkinSlider::setValue(double value)
{
    value = qBound(0.0, value, 1.0);
    if (m_dragging || qFuzzyCompare(value + 1.0, m_value + 1.0))
        return;
    const int old = thumbOffset();
    m_value = value;
    // A value-dependent background (volume) changes even when the thumb doesn't.
    if (thumbOffset() != old || m_background)
        update();
}

double SkinSlider::valueAt(const QPoint &pos) const
{
    if (span() <= 0)
        return 0.0;
    return qBound(0.0, double(along(pos) - m_grabOffset) / span(), 1.0);
}

void SkinSlider::paint(QPainter &painter, const Skin &skin) const
{
    if (m_background)
        skin.drawSprite(painter, rect().topLeft(), m_sheet, m_background(m_value));
    const QPoint thumbPos = rect().topLeft() + m_thumbOffset
        + (m_orientation == Qt::Horizontal ? QPoint(thumbOffset(), 0) : QPoint(0, thumbOffset()));
    skin.drawSprite(painter, thumbPos, m_sheet, m_dragging ? m_thumb.pressed : m_thumb.normal);
}

void SkinSlider::mousePress(const QPoint &pos)
{
    const int start = thumbOffset();
    const int length = m_orientation == Qt::Horizontal ? m_thumb.normal.width() : m_thumb.normal.height();
    const int at = along(pos);
    // Grabbing the thumb keeps its offset; clicking the track centres it.
    m_grabOffset = (at >= start && at < start + length) ? at - start : length / 2;
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

bool SkinSlider::wheel(int angleDeltaY)
{
    if (m_dragging)
        return true;
    const double notches = angleDeltaY / 120.0;
    if (onWheel) {
        onWheel(notches);
        return true;
    }
    if (!onMoved)
        return false;
    // Up means more: right on a horizontal slider, the top of a vertical one.
    constexpr double Step = 0.05;
    const double step = m_orientation == Qt::Horizontal ? notches * Step : -notches * Step;
    setValue(m_value + step);
    onMoved(m_value);
    return true;
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

SkinText::SkinText(SkinnedWindow *window, const QRect &rect, Qt::Alignment alignment)
    : SkinElement(window, rect)
    , m_alignment(alignment)
{
}

void SkinText::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    update();
}

void SkinText::paint(QPainter &painter, const Skin &skin) const
{
    int x = rect().left();
    if (m_alignment & Qt::AlignRight)
        x = rect().right() + 1 - Skin::textWidth(m_text);
    else if (m_alignment & Qt::AlignHCenter)
        x = rect().left() + (rect().width() - Skin::textWidth(m_text)) / 2;
    painter.save();
    painter.setClipRect(rect());
    skin.drawText(painter, {x, rect().top()}, m_text);
    painter.restore();
}

SkinMarquee::SkinMarquee(SkinnedWindow *window, const QRect &rect)
    : SkinElement(window, rect)
{
}

namespace {
// Long text loops with a separator, like the classic player.
const QString MarqueeSeparator = QStringLiteral("  ***  ");
} // namespace

void SkinMarquee::setText(const QString &text)
{
    if (text == m_text)
        return;
    m_text = text;
    m_offset = 0;
    update();
}

void SkinMarquee::showMessage(const QString &message)
{
    if (message == m_message)
        return;
    m_message = message;
    update();
}

void SkinMarquee::clearMessage()
{
    if (m_message.isEmpty())
        return;
    m_message.clear();
    update();
}

int SkinMarquee::loopWidth() const
{
    return Skin::textWidth(m_text + MarqueeSeparator);
}

void SkinMarquee::tick()
{
    if (!m_autoScroll || m_dragX >= 0 || !m_message.isEmpty() || Skin::textWidth(m_text) <= rect().width())
        return;
    m_offset = (m_offset + GlyphSize.width()) % loopWidth();
    update();
}

void SkinMarquee::mousePress(const QPoint &pos)
{
    m_dragX = pos.x();
    m_dragOffset = m_offset;
}

void SkinMarquee::mouseMove(const QPoint &pos)
{
    if (m_dragX < 0 || Skin::textWidth(m_text) <= rect().width())
        return;
    const int loop = loopWidth();
    m_offset = ((m_dragOffset - (pos.x() - m_dragX)) % loop + loop) % loop;
    update();
}

void SkinMarquee::mouseRelease(const QPoint &)
{
    m_dragX = -1;
}

void SkinMarquee::paint(QPainter &painter, const Skin &skin) const
{
    painter.save();
    painter.setClipRect(rect());
    if (!m_message.isEmpty()) {
        skin.drawText(painter, rect().topLeft(), m_message);
    } else if (Skin::textWidth(m_text) <= rect().width()) {
        skin.drawText(painter, rect().topLeft(), m_text);
    } else {
        const QString looped = m_text + MarqueeSeparator;
        const int loop = Skin::textWidth(looped);
        const QPoint start = rect().topLeft() - QPoint(m_offset, 0);
        skin.drawText(painter, start, looped);
        skin.drawText(painter, start + QPoint(loop, 0), looped);
    }
    painter.restore();
}

} // namespace newamp

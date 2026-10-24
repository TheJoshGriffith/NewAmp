#include "ui/SkinElements.h"

#include "skin/Skin.h"
#include "ui/SkinnedWindow.h"

#include <QClipboard>
#include <QFontMetrics>
#include <QGuiApplication>
#include <QKeyEvent>
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

void drawTextButton(QPainter &painter, const QRect &rect, const QString &text, const PlaylistStyle &style, bool down,
                    bool enabled)
{
    QColor edge = style.normal;
    edge.setAlphaF(0.5);
    QColor ink = down ? style.current : style.normal;
    if (!enabled)
        ink.setAlphaF(0.4);
    painter.fillRect(rect, down ? style.normalBackground : style.selectedBackground);
    painter.setPen(edge);
    painter.drawRect(rect.adjusted(0, 0, -1, -1));
    painter.setPen(ink);
    painter.drawText(rect, Qt::AlignCenter, text);
}

SkinTextButton::SkinTextButton(SkinnedWindow *window, const QString &text, std::function<void()> onClick)
    : SkinElement(window, QRect())
    , m_text(text)
    , m_onClick(std::move(onClick))
{
}

void SkinTextButton::setText(const QString &text)
{
    m_text = text;
    update();
}

void SkinTextButton::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    update();
}

void SkinTextButton::paint(QPainter &painter, const Skin &skin) const
{
    painter.save();
    painter.setFont(skin.playlistStyle().font());
    drawTextButton(painter, rect(), m_text, skin.playlistStyle(), m_down, m_enabled);
    painter.restore();
}

void SkinTextButton::mousePress(const QPoint &)
{
    m_pressed = m_enabled;
    m_down = m_pressed;
    update();
}

void SkinTextButton::mouseMove(const QPoint &pos)
{
    const bool down = m_pressed && rect().contains(pos);
    if (down != m_down) {
        m_down = down;
        update();
    }
}

void SkinTextButton::mouseRelease(const QPoint &pos)
{
    const bool fire = m_pressed && rect().contains(pos);
    m_pressed = m_down = false;
    update();
    if (fire && m_onClick)
        m_onClick();
}

SkinTextField::SkinTextField(SkinnedWindow *window)
    : SkinElement(window, QRect())
{
}

void SkinTextField::setText(const QString &text)
{
    m_text = text;
    m_cursor = int(text.size());
    m_allSelected = false;
    m_scroll = 0;
    update();
}

void SkinTextField::setReadOnly(bool readOnly)
{
    m_readOnly = readOnly;
    update();
}

void SkinTextField::setFocus(bool focus)
{
    if (focus == m_focus)
        return;
    m_focus = focus;
    m_allSelected = focus && !m_text.isEmpty();
    if (focus)
        m_cursor = int(m_text.size());
    update();
}

QRect SkinTextField::textRect() const
{
    return rect().adjusted(2, 0, -2, 0);
}

int SkinTextField::cursorAt(int x) const
{
    const QFontMetrics metrics(window()->skin().playlistStyle().font());
    const int target = x - textRect().left() + m_scroll;
    // The boundary nearest the click.
    int best = 0;
    for (int i = 1; i <= m_text.size(); ++i) {
        if (qAbs(metrics.horizontalAdvance(m_text.left(i)) - target)
            < qAbs(metrics.horizontalAdvance(m_text.left(best)) - target))
            best = i;
    }
    return best;
}

void SkinTextField::insert(const QString &text)
{
    if (m_allSelected) {
        m_text.clear();
        m_cursor = 0;
        m_allSelected = false;
    }
    QString clean = text;
    clean.replace(QLatin1Char('\n'), QLatin1Char(' ')).remove(QLatin1Char('\r'));
    m_text.insert(m_cursor, clean);
    m_cursor += int(clean.size());
    edited();
}

void SkinTextField::edited()
{
    update();
    if (onEdited)
        onEdited();
}

bool SkinTextField::keyPress(QKeyEvent *event)
{
    if (event->matches(QKeySequence::SelectAll)) {
        m_allSelected = !m_text.isEmpty();
        update();
        return true;
    }
    if (event->matches(QKeySequence::Copy)) {
        QGuiApplication::clipboard()->setText(m_text); // only all-or-nothing selection
        return true;
    }
    if (m_readOnly) {
        // Moving around a read-only field does nothing visible; leave keys
        // to the window.
        return false;
    }
    if (event->matches(QKeySequence::Cut)) {
        QGuiApplication::clipboard()->setText(m_text);
        m_text.clear();
        m_cursor = 0;
        m_allSelected = false;
        edited();
        return true;
    }
    if (event->matches(QKeySequence::Paste)) {
        insert(QGuiApplication::clipboard()->text());
        return true;
    }

    const bool wasAllSelected = m_allSelected;
    m_allSelected = false;
    switch (event->key()) {
    case Qt::Key_Backspace:
        if (wasAllSelected) {
            m_text.clear();
            m_cursor = 0;
        } else if (m_cursor > 0) {
            m_text.remove(--m_cursor, 1);
        }
        edited();
        return true;
    case Qt::Key_Delete:
        if (wasAllSelected) {
            m_text.clear();
            m_cursor = 0;
        } else if (m_cursor < m_text.size()) {
            m_text.remove(m_cursor, 1);
        }
        edited();
        return true;
    case Qt::Key_Left: m_cursor = wasAllSelected ? 0 : qMax(0, m_cursor - 1); break;
    case Qt::Key_Right: m_cursor = wasAllSelected ? int(m_text.size()) : qMin(int(m_text.size()), m_cursor + 1); break;
    case Qt::Key_Home: m_cursor = 0; break;
    case Qt::Key_End: m_cursor = int(m_text.size()); break;
    default: {
        const QString typed = event->text();
        if (!typed.isEmpty() && typed.at(0).isPrint()
            && !(event->modifiers() & (Qt::ControlModifier | Qt::MetaModifier))) {
            m_allSelected = wasAllSelected;
            insert(typed);
            return true;
        }
        m_allSelected = wasAllSelected;
        return false;
    }
    }
    update();
    return true;
}

void SkinTextField::paint(QPainter &painter, const Skin &skin) const
{
    const PlaylistStyle &style = skin.playlistStyle();
    QColor edge = style.normal;
    edge.setAlphaF(m_focus ? 0.8 : 0.35);
    painter.save();
    painter.fillRect(rect(), style.normalBackground);
    painter.setPen(edge);
    painter.drawRect(rect().adjusted(0, 0, -1, -1));

    const QRect area = textRect();
    painter.setClipRect(area);
    painter.setFont(style.font());
    const QFontMetrics metrics(painter.font());
    // Scroll just enough to keep the cursor inside the field.
    const int cursorX = metrics.horizontalAdvance(m_text.left(m_cursor));
    if (cursorX - m_scroll > area.width() - 1)
        m_scroll = cursorX - area.width() + 1;
    else if (cursorX < m_scroll)
        m_scroll = cursorX;
    m_scroll = qMax(0, qMin(m_scroll, qMax(0, metrics.horizontalAdvance(m_text) - area.width() + 1)));

    if (m_focus && m_allSelected)
        painter.fillRect(QRect(area.left() - m_scroll, area.top() + 1, metrics.horizontalAdvance(m_text), area.height() - 2),
                         style.selectedBackground);
    QColor ink = m_focus ? style.current : style.normal;
    if (m_readOnly)
        ink.setAlphaF(0.6);
    painter.setPen(ink);
    painter.drawText(area.translated(-m_scroll, 0), Qt::AlignLeft | Qt::AlignVCenter | Qt::TextSingleLine, m_text);
    if (m_focus && !m_readOnly && !m_allSelected) {
        const int x = area.left() + cursorX - m_scroll;
        painter.fillRect(QRect(x, area.top() + 2, 1, area.height() - 4), style.current);
    }
    painter.restore();
}

void SkinTextField::mousePress(const QPoint &pos)
{
    if (onClicked)
        onClicked();
    m_focus = true;
    m_allSelected = false;
    m_cursor = cursorAt(pos.x());
    update();
}

void SkinTextField::mouseDoubleClick(const QPoint &pos)
{
    mousePress(pos);
    m_allSelected = !m_text.isEmpty();
    update();
}

} // namespace newamp

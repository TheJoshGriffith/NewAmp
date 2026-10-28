#pragma once

#include "skin/SkinSprites.h"
#include "ui/SkinElement.h"

#include <QString>

#include <functional>

class QKeyEvent;

namespace newamp {

struct PlaylistStyle;

// Static sprite whose source rect can change (e.g. the play-state icon).
class SkinImage : public SkinElement
{
public:
    SkinImage(SkinnedWindow *window, const QPoint &pos, sprites::Sheet sheet, const QRect &source);

    void setSource(const QRect &source);
    void paint(QPainter &painter, const Skin &skin) const override;

private:
    sprites::Sheet m_sheet;
    QRect m_source;
};

// Push button with normal and pressed sprites. Fires on release inside. An
// empty normal sprite draws nothing, for buttons that are part of the art.
class SkinButton : public SkinElement
{
public:
    SkinButton(SkinnedWindow *window, const QPoint &pos, sprites::Sheet sheet,
               const sprites::Sprite &sprite, std::function<void()> onClick);

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseMove(const QPoint &pos) override;
    void mouseRelease(const QPoint &pos) override;

protected:
    bool isDown() const { return m_down; }
    sprites::Sheet sheet() const { return m_sheet; }
    virtual void clicked();

private:
    void setDown(bool down);

    sprites::Sheet m_sheet;
    sprites::Sprite m_sprite;
    std::function<void()> m_onClick;
    bool m_pressed = false;
    bool m_down = false;
};

// Button that stays on or off (shuffle, repeat, EQ on, window toggles).
class SkinToggleButton : public SkinButton
{
public:
    SkinToggleButton(SkinnedWindow *window, const QPoint &pos, sprites::Sheet sheet,
                     const sprites::ToggleSprite &sprite, std::function<void(bool)> onToggled);

    bool isChecked() const { return m_checked; }
    // Updates the look without firing onToggled.
    void setChecked(bool checked);
    void setOnToggled(std::function<void(bool)> onToggled) { m_onToggled = std::move(onToggled); }

    void paint(QPainter &painter, const Skin &skin) const override;

protected:
    void clicked() override;

private:
    sprites::ToggleSprite m_toggle;
    std::function<void(bool)> m_onToggled;
    bool m_checked = false;
};

// Invisible clickable area; the skin bitmap underneath already shows it.
class SkinHotspot : public SkinElement
{
public:
    SkinHotspot(SkinnedWindow *window, const QRect &rect, std::function<void()> onClick);

    void paint(QPainter &, const Skin &) const override {}
    bool acceptsMouse() const override { return true; }
    void mouseRelease(const QPoint &pos) override;

private:
    std::function<void()> m_onClick;
};

// Slider with a background that may vary with the value, and a thumb.
// Value runs 0..1, left to right or top to bottom.
class SkinSlider : public SkinElement
{
public:
    using BackgroundFn = std::function<QRect(double value)>;

    SkinSlider(SkinnedWindow *window, const QRect &rect, sprites::Sheet sheet, BackgroundFn background,
               const sprites::Sprite &thumb, Qt::Orientation orientation = Qt::Horizontal,
               const QPoint &thumbOffset = {});

    double value() const { return m_value; }
    // Ignored while the user is dragging so playback updates don't fight them.
    void setValue(double value);
    bool isDragging() const { return m_dragging; }

    std::function<void(double)> onMoved;    // while dragging
    std::function<void(double)> onReleased; // drag finished
    // Wheel turns, in notches (fractional on touchpads), positive away from
    // the user. Without it the wheel steps the value and calls onMoved.
    std::function<void(double notches)> onWheel;

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseMove(const QPoint &pos) override;
    void mouseRelease(const QPoint &pos) override;
    bool wheel(int angleDeltaY) override;

private:
    int span() const;
    int thumbOffset() const;
    int along(const QPoint &pos) const;
    double valueAt(const QPoint &pos) const;

    sprites::Sheet m_sheet;
    BackgroundFn m_background;
    sprites::Sprite m_thumb;
    Qt::Orientation m_orientation;
    QPoint m_thumbOffset;
    double m_value = 0.0;
    int m_grabOffset = 0;
    bool m_dragging = false;
};

// mm:ss in the skin's digit font, or the small text font of shade bars,
// with a minus while counting down. Clicking it calls onClicked.
class SkinTimeDisplay : public SkinElement
{
public:
    enum class Font { Numbers, Text };

    SkinTimeDisplay(SkinnedWindow *window, const QRect &rect, Font font = Font::Numbers);

    // Negative seconds blank the display.
    void setSeconds(qint64 seconds, bool countingDown = false);

    std::function<void()> onClicked;

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return bool(onClicked); }
    void mouseRelease(const QPoint &pos) override;

private:
    Font m_font;
    qint64 m_seconds = -1;
    bool m_countingDown = false;
};

// Fixed text in the skin's bitmap font, clipped to its rect.
class SkinText : public SkinElement
{
public:
    SkinText(SkinnedWindow *window, const QRect &rect, Qt::Alignment alignment = Qt::AlignLeft);

    void setText(const QString &text);
    void paint(QPainter &painter, const Skin &skin) const override;

private:
    QString m_text;
    Qt::Alignment m_alignment;
};

// Text in the skin's bitmap font that scrolls when it doesn't fit. It can
// be dragged sideways to scroll it by hand, as in the classic player.
class SkinMarquee : public SkinElement
{
public:
    SkinMarquee(SkinnedWindow *window, const QRect &rect);

    QString text() const { return m_text; }
    void setText(const QString &text);
    // Shows a short status ("VOLUME: 50%") in place of the text, unscrolled,
    // until clearMessage().
    void showMessage(const QString &message);
    void clearMessage();
    bool hasMessage() const { return !m_message.isEmpty(); }
    // Whether tick() scrolls long text; off, it stays where it was left.
    void setAutoScroll(bool on) { m_autoScroll = on; }

    // Advance the scroll by one step; call from the window's animation timer.
    void tick();
    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseMove(const QPoint &pos) override;
    void mouseRelease(const QPoint &pos) override;

private:
    int loopWidth() const;

    QString m_text;
    QString m_message;
    int m_offset = 0;
    int m_dragX = -1; // where a drag started, -1 when not dragging
    int m_dragOffset = 0;
    bool m_autoScroll = true;
};

// A flat text button in the playlist colours, for windows built on gen.bmp
// that have no sprites for their buttons.
void drawTextButton(QPainter &painter, const QRect &rect, const QString &text, const PlaylistStyle &style,
                    bool down = false, bool enabled = true);

// A clickable drawTextButton(). Fires on release inside, when enabled.
class SkinTextButton : public SkinElement
{
public:
    SkinTextButton(SkinnedWindow *window, const QString &text, std::function<void()> onClick);

    void setText(const QString &text);
    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseMove(const QPoint &pos) override;
    void mouseRelease(const QPoint &pos) override;

private:
    QString m_text;
    std::function<void()> m_onClick;
    bool m_enabled = true;
    bool m_pressed = false;
    bool m_down = false;
};

// One line of editable text in the playlist font and colours. Elements get
// no key events of their own, so the window passes keys to the focused
// field through keyPress().
class SkinTextField : public SkinElement
{
public:
    explicit SkinTextField(SkinnedWindow *window);

    QString text() const { return m_text; }
    // Puts the cursor at the end.
    void setText(const QString &text);
    bool isReadOnly() const { return m_readOnly; }
    void setReadOnly(bool readOnly);
    bool hasFocus() const { return m_focus; }
    // Gaining focus selects the whole text, as tabbing into a field does.
    void setFocus(bool focus);

    // Handles typing, cursor keys, select all, copy, cut and paste. Returns
    // false for keys it leaves to the window, such as Tab and Enter.
    bool keyPress(QKeyEvent *event);

    std::function<void()> onClicked; // the window moves focus here
    std::function<void()> onEdited;  // the text changed by typing

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseDoubleClick(const QPoint &pos) override;

private:
    QRect textRect() const;
    int cursorAt(int x) const;
    void insert(const QString &text);
    void edited();

    QString m_text;
    int m_cursor = 0;
    bool m_allSelected = false;
    bool m_focus = false;
    bool m_readOnly = false;
    mutable int m_scroll = 0; // pixels scrolled so the cursor stays in view
};

} // namespace newamp

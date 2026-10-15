#pragma once

#include "skin/SkinSprites.h"
#include "ui/SkinElement.h"

#include <QString>

#include <functional>

namespace newamp {

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

// Push button with normal and pressed sprites. Fires on release inside.
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

private:
    void setDown(bool down);

    sprites::Sheet m_sheet;
    sprites::Sprite m_sprite;
    std::function<void()> m_onClick;
    bool m_pressed = false;
    bool m_down = false;
};

// Horizontal slider: a background that may vary with the value, and a thumb.
class SkinSlider : public SkinElement
{
public:
    using BackgroundFn = std::function<QRect(double value)>;

    SkinSlider(SkinnedWindow *window, const QRect &rect, sprites::Sheet sheet,
               BackgroundFn background, const sprites::Sprite &thumb, int thumbY = 0);

    double value() const { return m_value; }
    // Ignored while the user is dragging so playback updates don't fight them.
    void setValue(double value);
    bool isDragging() const { return m_dragging; }

    std::function<void(double)> onMoved;    // while dragging
    std::function<void(double)> onReleased; // drag finished

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &pos) override;
    void mouseMove(const QPoint &pos) override;
    void mouseRelease(const QPoint &pos) override;

private:
    double valueAt(const QPoint &pos) const;
    int thumbX() const;

    sprites::Sheet m_sheet;
    BackgroundFn m_background;
    sprites::Sprite m_thumb;
    int m_thumbY;
    double m_value = 0.0;
    int m_grabOffset = 0;
    bool m_dragging = false;
};

// mm:ss in the skin's digit font.
class SkinTimeDisplay : public SkinElement
{
public:
    SkinTimeDisplay(SkinnedWindow *window, const QRect &rect);

    void setSeconds(qint64 seconds);
    void paint(QPainter &painter, const Skin &skin) const override;

private:
    qint64 m_seconds = -1; // -1 = blank
};

// Text in the skin's bitmap font that scrolls when it doesn't fit.
class SkinMarquee : public SkinElement
{
public:
    SkinMarquee(SkinnedWindow *window, const QRect &rect);

    void setText(const QString &text);
    // Advance the scroll by one step; call from the window's animation timer.
    void tick();
    void paint(QPainter &painter, const Skin &skin) const override;

private:
    QString m_text;
    int m_offset = 0;
};

} // namespace newamp

#pragma once

#include <QRect>
#include <QString>

class QPainter;

namespace newamp {

class Skin;
class SkinnedWindow;

// A lightweight drawable, clickable region of a skinned window. Elements are
// plain objects, not QWidgets: the window paints them all in one pass and
// routes mouse input to them, in unscaled skin coordinates.
class SkinElement
{
public:
    SkinElement(SkinnedWindow *window, const QRect &rect);
    virtual ~SkinElement() = default;

    SkinElement(const SkinElement &) = delete;
    SkinElement &operator=(const SkinElement &) = delete;

    QRect rect() const { return m_rect; }
    void setRect(const QRect &rect);

    bool isVisible() const { return m_visible; }
    void setVisible(bool visible);

    // The skin cursor shown over this element ("close", "volbal", ...);
    // empty uses the window's cursor for the area.
    QString cursorName() const { return m_cursorName; }
    void setCursorName(const QString &name) { m_cursorName = name; }

    virtual void paint(QPainter &painter, const Skin &skin) const = 0;

    // Positions are relative to the window, in skin pixels.
    virtual bool acceptsMouse() const { return false; }
    virtual void mousePress(const QPoint &) {}
    virtual void mouseMove(const QPoint &) {}
    virtual void mouseRelease(const QPoint &) {}
    virtual void mouseDoubleClick(const QPoint &pos) { mousePress(pos); }
    // Return true if the wheel event was used.
    virtual bool wheel(int /*angleDeltaY*/) { return false; }

protected:
    void update();
    SkinnedWindow *window() const { return m_window; }

private:
    SkinnedWindow *m_window;
    QRect m_rect;
    QString m_cursorName;
    bool m_visible = true;
};

} // namespace newamp

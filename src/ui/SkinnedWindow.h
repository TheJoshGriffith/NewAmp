#pragma once

#include "ui/SkinElement.h"

#include <QWidget>

#include <memory>
#include <vector>

namespace newamp {

class Skin;
class WindowManager;

// Base class for every NewAmp window (main, playlist, equalizer, ...).
//
// It owns the frameless top-level widget, the skin elements on it, scaling
// ("double size"), painting, mouse routing, dragging by the title bar
// (snapping through the WindowManager) and optional resizing in fixed steps.
// Subclasses only describe their layout: which elements sit where, what the
// background is, and which areas drag or resize the window.
//
// Windows that support windowshade mode describe a second, rolled-up layout
// the same way: shade elements, a shade background, and a bar ShadeHeight
// pixels tall. Double-clicking the title bar or pressing Ctrl+W switches.
class SkinnedWindow : public QWidget
{
    Q_OBJECT

public:
    SkinnedWindow(const Skin *skin, const QSize &skinSize, QWidget *parent = nullptr,
                  Qt::WindowFlags extraFlags = {});
    ~SkinnedWindow() override;

    const Skin &skin() const { return *m_skin; }
    void setSkin(const Skin *skin);

    // Whole-window zoom; fractional values such as 1.5 are allowed.
    qreal scale() const { return m_scale; }
    void setScale(qreal scale);
    static constexpr qreal MinScale = 1.0;
    static constexpr qreal MaxScale = 4.0;

    // How far one arrow key press or wheel notch seeks, or moves the volume.
    static constexpr qint64 SeekStepMs = 5000;
    static constexpr float VolumeStep = 0.05f;

    // The full window's size, also while it is shaded.
    QSize skinSize() const { return m_skinSize; }
    // For resizable windows; the size is constrained by constrainSize().
    void setSkinSize(const QSize &size);
    // What is on screen: the full size, or the shade bar.
    QSize displayedSkinSize() const;

    // Windowshade mode: the window rolls up to its title bar.
    virtual bool canShade() const { return false; }
    bool isShaded() const { return m_shaded; }
    void setShaded(bool shaded);
    void toggleShaded() { setShaded(!m_shaded); }

    // Above other apps' windows; WindowManager sets it for all windows.
    void setStaysOnTop(bool on);
    bool staysOnTop() const { return windowFlags().testFlag(Qt::WindowStaysOnTopHint); }

    void setWindowManager(WindowManager *manager) { m_manager = manager; }
    WindowManager *windowManager() const { return m_manager; }

    // Called by elements; rect is in skin coordinates.
    void updateSkinRect(const QRect &rect);

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void scaleChanged(qreal scale);
    void shadedChanged(bool shaded);

protected:
    template<typename T, typename... Args>
    T *addElement(Args &&...args)
    {
        auto element = std::make_unique<T>(this, std::forward<Args>(args)...);
        T *raw = element.get();
        m_elements.push_back(std::move(element));
        return raw;
    }
    // Elements of the windowshade layout, shown only while shaded.
    template<typename T, typename... Args>
    T *addShadeElement(Args &&...args)
    {
        auto element = std::make_unique<T>(this, std::forward<Args>(args)...);
        T *raw = element.get();
        m_shadeElements.push_back(std::move(element));
        return raw;
    }

    // The region.txt section that shapes this window ("normal",
    // "equalizer"); empty keeps it rectangular.
    virtual QString regionSection() const { return {}; }
    // Re-applies the skin's window shape, e.g. after regionSection() changes.
    void updateShape();
    // Skin cursors for the window body, the drag area and the resize area.
    // Elements can name their own with SkinElement::setCursorName().
    void setCursorNames(const QString &normal, const QString &title, const QString &resize = {});

    // Draw everything under the elements: window body, title bar, labels.
    virtual void paintBackground(QPainter &painter, const Skin &skin) = 0;
    // The same for the shade bar, for windows that can shade.
    virtual void paintShadeBackground(QPainter &, const Skin &) {}
    // Called after shading or unshading, before the repaint.
    virtual void shadeChanged() {}

    // Areas that move the window when dragged. Defaults to the title bar.
    virtual bool isDragArea(const QPoint &skinPos) const;
    // Double-clicking an empty spot here shades or unshades the window.
    virtual int titleBarHeight() const;
    // Areas that resize the window. Defaults to none.
    virtual bool isResizeArea(const QPoint &) const { return false; }
    virtual QSize constrainSize(const QSize &size) const { return size; }
    // Called after the skin size changes so subclasses can move elements.
    virtual void layoutElements() {}
    // Whether a wheel turn that no element uses changes the volume.
    virtual bool wheelChangesVolume() const { return false; }

    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void wheelEvent(QWheelEvent *event) override;
    // The player keys every window shares; subclasses handle their own keys
    // first and pass the rest here.
    void keyPressEvent(QKeyEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void hideEvent(QHideEvent *event) override;

    // Widget coordinates to skin coordinates, undoing the UI scale.
    QPoint toSkin(const QPointF &pos) const;

private:
    enum class Gesture { None, Element, Move, Resize };

    QSize scaledSize() const;
    const std::vector<std::unique_ptr<SkinElement>> &activeElements() const;
    SkinElement *elementAt(const QPoint &skinPos) const;
    void updateCursor(const QPoint &skinPos);

    const Skin *m_skin;
    WindowManager *m_manager = nullptr;
    QSize m_skinSize;
    qreal m_scale = 1.0;
    std::vector<std::unique_ptr<SkinElement>> m_elements;
    std::vector<std::unique_ptr<SkinElement>> m_shadeElements;
    bool m_shaded = false;
    QString m_normalCursor;
    QString m_titleCursor;
    QString m_resizeCursor;
    QString m_shownCursor; // what's set now, so moves don't reset it

    Gesture m_gesture = Gesture::None;
    SkinElement *m_mouseGrabber = nullptr;
    QPoint m_pressGlobal;
    QPoint m_pressWindowPos;
    QSize m_pressSkinSize;
};

} // namespace newamp

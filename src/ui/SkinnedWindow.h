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

    QSize skinSize() const { return m_skinSize; }
    // For resizable windows; the size is constrained by constrainSize().
    void setSkinSize(const QSize &size);

    void setWindowManager(WindowManager *manager) { m_manager = manager; }
    WindowManager *windowManager() const { return m_manager; }

    // Called by elements; rect is in skin coordinates.
    void updateSkinRect(const QRect &rect);

Q_SIGNALS:
    void visibilityChanged(bool visible);
    void scaleChanged(qreal scale);

protected:
    template<typename T, typename... Args>
    T *addElement(Args &&...args)
    {
        auto element = std::make_unique<T>(this, std::forward<Args>(args)...);
        T *raw = element.get();
        m_elements.push_back(std::move(element));
        return raw;
    }

    // Draw everything under the elements: window body, title bar, labels.
    virtual void paintBackground(QPainter &painter, const Skin &skin) = 0;

    // Areas that move the window when dragged. Defaults to the title bar.
    virtual bool isDragArea(const QPoint &skinPos) const;
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
    SkinElement *elementAt(const QPoint &skinPos) const;

    const Skin *m_skin;
    WindowManager *m_manager = nullptr;
    QSize m_skinSize;
    qreal m_scale = 1.0;
    std::vector<std::unique_ptr<SkinElement>> m_elements;

    Gesture m_gesture = Gesture::None;
    SkinElement *m_mouseGrabber = nullptr;
    QPoint m_pressGlobal;
    QPoint m_pressWindowPos;
    QSize m_pressSkinSize;
};

} // namespace newamp

#pragma once

#include "ui/SkinElement.h"

#include <QWidget>

#include <memory>
#include <vector>

namespace newamp {

class Skin;

// Base class for every NewAmp window (main, playlist, equalizer, ...).
//
// It owns the frameless top-level widget, the skin elements on it, scaling
// ("double size"), painting, mouse routing and dragging by the title bar.
// Subclasses only describe their layout: which elements sit where, what the
// background is, and which areas drag the window.
class SkinnedWindow : public QWidget
{
    Q_OBJECT

public:
    SkinnedWindow(const Skin *skin, const QSize &skinSize, QWidget *parent = nullptr);
    ~SkinnedWindow() override;

    const Skin &skin() const { return *m_skin; }
    void setSkin(const Skin *skin);

    int scale() const { return m_scale; }
    void setScale(int scale);

    // Called by elements; rect is in skin coordinates.
    void updateSkinRect(const QRect &rect);

protected:
    template<typename T, typename... Args>
    T *addElement(Args &&...args)
    {
        auto element = std::make_unique<T>(this, std::forward<Args>(args)...);
        T *raw = element.get();
        m_elements.push_back(std::move(element));
        return raw;
    }

    void setSkinSize(const QSize &size);
    QSize skinSize() const { return m_skinSize; }

    // Draw everything under the elements: window body, title bar, labels.
    virtual void paintBackground(QPainter &painter, const Skin &skin) = 0;

    // Areas that move the window when dragged. Defaults to the title bar.
    virtual bool isDragArea(const QPoint &skinPos) const;

    void paintEvent(QPaintEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseMoveEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    QPoint toSkin(const QPointF &pos) const;
    SkinElement *elementAt(const QPoint &skinPos) const;

    const Skin *m_skin;
    QSize m_skinSize;
    int m_scale = 1;
    std::vector<std::unique_ptr<SkinElement>> m_elements;
    SkinElement *m_mouseGrabber = nullptr;
};

} // namespace newamp

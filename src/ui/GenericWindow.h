#pragma once

#include "skin/SkinSprites.h"
#include "ui/SkinnedWindow.h"

namespace newamp {

class SkinButton;

// Base for every window beyond the classic three (main, equalizer,
// playlist): a resizable frame from the skin's gen.bmp with a title and a
// close button. Subclasses fill contentRect() with their own elements and
// painting, and get the frame, dragging, snapping and resizing for free.
class GenericWindow : public SkinnedWindow
{
    Q_OBJECT

public:
    GenericWindow(const Skin *skin, const QString &title, QWidget *parent = nullptr,
                  const QSize &skinSize = sprites::GenMinimumSize);

    QString title() const { return m_title; }
    void setTitle(const QString &title);

protected:
    // The area inside the frame, in skin coordinates.
    QRect contentRect() const;

    // Draw under the subclass's elements, inside contentRect().
    virtual void paintContent(QPainter &painter, const Skin &skin, const QRect &content) = 0;
    // Position elements after a resize; content is contentRect().
    virtual void layoutContent(const QRect &content) { Q_UNUSED(content) }

    void paintBackground(QPainter &painter, const Skin &skin) final;
    bool isDragArea(const QPoint &skinPos) const override;
    bool isResizeArea(const QPoint &skinPos) const override;
    QSize constrainSize(const QSize &size) const override;
    void layoutElements() final;

private:
    QString m_title;
    SkinButton *m_close = nullptr;
};

} // namespace newamp

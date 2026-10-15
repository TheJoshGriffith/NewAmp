#include "ui/SkinnedWindow.h"

#include "skin/Skin.h"
#include "skin/SkinSprites.h"

#include <QMouseEvent>
#include <QPainter>
#include <QWindow>

namespace newamp {

SkinnedWindow::SkinnedWindow(const Skin *skin, const QSize &skinSize, QWidget *parent)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint)
    , m_skin(skin)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setSkinSize(skinSize);
}

SkinnedWindow::~SkinnedWindow() = default;

void SkinnedWindow::setSkin(const Skin *skin)
{
    m_skin = skin;
    update();
}

void SkinnedWindow::setScale(int scale)
{
    scale = qBound(1, scale, 4);
    if (scale == m_scale)
        return;
    m_scale = scale;
    setFixedSize(m_skinSize * m_scale);
    update();
}

void SkinnedWindow::setSkinSize(const QSize &size)
{
    m_skinSize = size;
    setFixedSize(m_skinSize * m_scale);
}

void SkinnedWindow::updateSkinRect(const QRect &rect)
{
    update(QRect(rect.topLeft() * m_scale, rect.size() * m_scale));
}

bool SkinnedWindow::isDragArea(const QPoint &skinPos) const
{
    return skinPos.y() < sprites::TitleBarHeight;
}

QPoint SkinnedWindow::toSkin(const QPointF &pos) const
{
    return QPoint(int(pos.x()) / m_scale, int(pos.y()) / m_scale);
}

SkinElement *SkinnedWindow::elementAt(const QPoint &skinPos) const
{
    // Last added is on top.
    for (auto it = m_elements.rbegin(); it != m_elements.rend(); ++it) {
        SkinElement *e = it->get();
        if (e->isVisible() && e->acceptsMouse() && e->rect().contains(skinPos))
            return e;
    }
    return nullptr;
}

void SkinnedWindow::paintEvent(QPaintEvent *)
{
    QPainter painter(this);
    painter.scale(m_scale, m_scale);
    paintBackground(painter, *m_skin);
    for (const auto &element : m_elements)
        if (element->isVisible())
            element->paint(painter, *m_skin);
}

void SkinnedWindow::mousePressEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }
    const QPoint pos = toSkin(event->position());
    if (SkinElement *element = elementAt(pos)) {
        m_mouseGrabber = element;
        element->mousePress(pos);
    } else if (isDragArea(pos) && windowHandle()) {
        // Let the window system move us: native feel and multi-monitor aware.
        windowHandle()->startSystemMove();
    }
}

void SkinnedWindow::mouseMoveEvent(QMouseEvent *event)
{
    if (m_mouseGrabber)
        m_mouseGrabber->mouseMove(toSkin(event->position()));
}

void SkinnedWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (m_mouseGrabber && event->button() == Qt::LeftButton) {
        SkinElement *element = m_mouseGrabber;
        m_mouseGrabber = nullptr;
        element->mouseRelease(toSkin(event->position()));
    }
}

void SkinnedWindow::changeEvent(QEvent *event)
{
    // Title bars switch between active and inactive sprites.
    if (event->type() == QEvent::ActivationChange)
        update();
    QWidget::changeEvent(event);
}

} // namespace newamp

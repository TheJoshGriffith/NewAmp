#include "ui/SkinnedWindow.h"

#include "core/PlayerController.h"
#include "skin/Skin.h"
#include "skin/SkinSprites.h"
#include "ui/WindowManager.h"

#include <QCursor>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QRegion>

namespace newamp {

SkinnedWindow::SkinnedWindow(const Skin *skin, const QSize &skinSize, QWidget *parent,
                             Qt::WindowFlags extraFlags)
    : QWidget(parent, Qt::Window | Qt::FramelessWindowHint | extraFlags)
    , m_skin(skin)
{
    setAttribute(Qt::WA_OpaquePaintEvent);
    setFocusPolicy(Qt::StrongFocus); // for the shared player keys
    setMouseTracking(true);          // for the skin's cursors
    setSkinSize(skinSize);
}

SkinnedWindow::~SkinnedWindow() = default;

void SkinnedWindow::setSkin(const Skin *skin)
{
    m_skin = skin;
    m_shownCursor = QStringLiteral("?"); // the new skin's cursors differ
    updateShape();
    updateCursor(toSkin(mapFromGlobal(QCursor::pos())));
    update();
}

void SkinnedWindow::updateShape()
{
    const QString section = regionSection();
    const QList<QPolygon> polygons = section.isEmpty() ? QList<QPolygon>() : m_skin->regions().value(section);
    if (polygons.isEmpty()) {
        clearMask();
        return;
    }
    // Scaled as polygons rather than as a region, so fractional scales keep
    // diagonal edges instead of stair-stepping each scan line.
    QRegion region;
    for (const QPolygon &polygon : polygons) {
        QPolygon scaled;
        for (const QPoint &p : polygon)
            scaled << QPoint(qRound(p.x() * m_scale), qRound(p.y() * m_scale));
        region += QRegion(scaled, Qt::OddEvenFill);
    }
    setMask(region);
}

void SkinnedWindow::setCursorNames(const QString &normal, const QString &title, const QString &resize)
{
    m_normalCursor = normal;
    m_titleCursor = title;
    m_resizeCursor = resize;
}

void SkinnedWindow::updateCursor(const QPoint &skinPos)
{
    QString name = m_normalCursor;
    if (SkinElement *element = elementAt(skinPos); element && !element->cursorName().isEmpty())
        name = element->cursorName();
    else if (!element && isResizeArea(skinPos) && !m_resizeCursor.isEmpty())
        name = m_resizeCursor;
    else if (!element && isDragArea(skinPos) && !m_titleCursor.isEmpty())
        name = m_titleCursor;
    if (name == m_shownCursor)
        return;
    m_shownCursor = name;
    if (const QCursor *cursor = m_skin->cursor(name))
        setCursor(*cursor);
    else
        unsetCursor();
}

void SkinnedWindow::setScale(qreal scale)
{
    scale = qBound(MinScale, scale, MaxScale);
    if (qFuzzyCompare(scale, m_scale))
        return;
    m_scale = scale;
    setFixedSize(scaledSize());
    updateShape();
    update();
    Q_EMIT scaleChanged(m_scale);
}

void SkinnedWindow::setSkinSize(const QSize &size)
{
    const QSize constrained = constrainSize(size);
    if (constrained == m_skinSize)
        return;
    m_skinSize = constrained;
    setFixedSize(scaledSize());
    updateShape();
    layoutElements();
    update();
}

QSize SkinnedWindow::scaledSize() const
{
    return {qRound(m_skinSize.width() * m_scale), qRound(m_skinSize.height() * m_scale)};
}

void SkinnedWindow::updateSkinRect(const QRect &rect)
{
    // One extra pixel each side covers rounding at fractional scales.
    const QRectF scaled(rect.x() * m_scale, rect.y() * m_scale, rect.width() * m_scale, rect.height() * m_scale);
    update(scaled.toAlignedRect().adjusted(-1, -1, 1, 1));
}

bool SkinnedWindow::isDragArea(const QPoint &skinPos) const
{
    return skinPos.y() < sprites::TitleBarHeight;
}

QPoint SkinnedWindow::toSkin(const QPointF &pos) const
{
    return QPoint(int(pos.x() / m_scale), int(pos.y() / m_scale));
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
    m_pressGlobal = event->globalPosition().toPoint();
    m_pressWindowPos = this->pos();
    m_pressSkinSize = m_skinSize;

    if (SkinElement *element = elementAt(pos)) {
        m_gesture = Gesture::Element;
        m_mouseGrabber = element;
        element->mousePress(pos);
    } else if (isResizeArea(pos)) {
        m_gesture = Gesture::Resize;
    } else if (isDragArea(pos)) {
        // Moved by hand rather than by the window system, so windows can
        // snap to each other and carry docked windows along.
        m_gesture = Gesture::Move;
        if (m_manager)
            m_manager->beginMove(this);
    }
}

void SkinnedWindow::mouseMoveEvent(QMouseEvent *event)
{
    const QPoint delta = event->globalPosition().toPoint() - m_pressGlobal;
    switch (m_gesture) {
    case Gesture::Element:
        m_mouseGrabber->mouseMove(toSkin(event->position()));
        break;
    case Gesture::Move:
        if (m_manager)
            m_manager->moveWindow(this, m_pressWindowPos + delta);
        else
            move(m_pressWindowPos + delta);
        break;
    case Gesture::Resize:
        setSkinSize(m_pressSkinSize + QSize(qRound(delta.x() / m_scale), qRound(delta.y() / m_scale)));
        break;
    case Gesture::None:
        updateCursor(toSkin(event->position()));
        break;
    }
}

void SkinnedWindow::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const Gesture gesture = m_gesture;
    m_gesture = Gesture::None;
    updateCursor(toSkin(event->position()));
    if (gesture == Gesture::Element) {
        SkinElement *element = m_mouseGrabber;
        m_mouseGrabber = nullptr;
        element->mouseRelease(toSkin(event->position()));
    } else if (gesture == Gesture::Move && m_manager) {
        m_manager->endMove(this);
    }
}

void SkinnedWindow::mouseDoubleClickEvent(QMouseEvent *event)
{
    if (event->button() != Qt::LeftButton)
        return;
    const QPoint pos = toSkin(event->position());
    if (SkinElement *element = elementAt(pos)) {
        m_gesture = Gesture::Element;
        m_mouseGrabber = element;
        element->mouseDoubleClick(pos);
    }
}

void SkinnedWindow::wheelEvent(QWheelEvent *event)
{
    const QPoint pos = toSkin(event->position());
    for (auto it = m_elements.rbegin(); it != m_elements.rend(); ++it) {
        SkinElement *e = it->get();
        if (e->isVisible() && e->rect().contains(pos) && e->wheel(event->angleDelta().y())) {
            event->accept();
            return;
        }
    }
    PlayerController *controller = m_manager ? m_manager->controller() : nullptr;
    if (controller && wheelChangesVolume()) {
        controller->changeVolume(VolumeStep * event->angleDelta().y() / 120.0f);
        event->accept();
        return;
    }
    QWidget::wheelEvent(event);
}

void SkinnedWindow::keyPressEvent(QKeyEvent *event)
{
    PlayerController *controller = m_manager ? m_manager->controller() : nullptr;
    // Shortcuts with Ctrl, Alt or Cmd belong to menus, not to these keys.
    const Qt::KeyboardModifiers modifiers = event->modifiers() & ~(Qt::KeypadModifier | Qt::ShiftModifier);
    if (!controller || modifiers != Qt::NoModifier) {
        QWidget::keyPressEvent(event);
        return;
    }
    // The classic Z X C V B transport row, space, and the arrow keys.
    switch (event->key()) {
    case Qt::Key_Z: controller->previous(); break;
    case Qt::Key_X: controller->play(); break;
    case Qt::Key_C: controller->pause(); break;
    case Qt::Key_V: controller->stop(); break;
    case Qt::Key_B: controller->next(); break;
    case Qt::Key_Space: controller->playPause(); break;
    case Qt::Key_Left: controller->seekBy(-SeekStepMs); break;
    case Qt::Key_Right: controller->seekBy(SeekStepMs); break;
    case Qt::Key_Up: controller->changeVolume(VolumeStep); break;
    case Qt::Key_Down: controller->changeVolume(-VolumeStep); break;
    default: QWidget::keyPressEvent(event); return;
    }
    event->accept();
}

void SkinnedWindow::changeEvent(QEvent *event)
{
    // Title bars switch between active and inactive sprites.
    if (event->type() == QEvent::ActivationChange)
        update();
    QWidget::changeEvent(event);
}

void SkinnedWindow::showEvent(QShowEvent *event)
{
    QWidget::showEvent(event);
    // Spontaneous events come from minimizing and restoring, not from us.
    if (!event->spontaneous())
        Q_EMIT visibilityChanged(true);
}

void SkinnedWindow::hideEvent(QHideEvent *event)
{
    QWidget::hideEvent(event);
    if (!event->spontaneous())
        Q_EMIT visibilityChanged(false);
}

} // namespace newamp

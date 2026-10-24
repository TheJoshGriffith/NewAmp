#include "ui/GenericWindow.h"

#include "skin/Skin.h"
#include "ui/SkinElements.h"

#include <QPainter>

namespace newamp {

using namespace sprites;

GenericWindow::GenericWindow(const Skin *skin, const QString &title, QWidget *parent, const QSize &skinSize)
    : SkinnedWindow(skin, skinSize, parent, Qt::Tool)
    , m_title(title)
{
    setWindowTitle(title);
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

    // Classic skins have no cursors for these windows; they share the
    // playlist's, which they resemble.
    setCursorNames(QStringLiteral("pnormal"), QStringLiteral("ptbar"), QStringLiteral("psize"));
    // The close button's normal look is part of the top-right corner art.
    const QRect closeNormal(GenTopRight.active.topLeft() + GenCloseOffset, QSize(9, 9));
    m_close = addElement<SkinButton>(QPoint(), Sheet::Gen, Sprite{closeNormal, GenClosePressed}, [this] { hide(); });
    m_close->setCursorName(QStringLiteral("pclose"));
    layoutElements();
}

void GenericWindow::setTitle(const QString &title)
{
    m_title = title;
    setWindowTitle(title);
    update();
}

QRect GenericWindow::contentRect() const
{
    const QSize size = skinSize();
    return {GenLeftWidth, GenTopHeight, size.width() - GenLeftWidth - GenRightWidth,
            size.height() - GenTopHeight - GenBottomHeight};
}

void GenericWindow::paintBackground(QPainter &painter, const Skin &skin)
{
    const bool active = isActiveWindow();
    const auto pick = [active](const ActiveSprite &s) { return active ? s.active : s.inactive; };
    const int w = skinSize().width();
    const int h = skinSize().height();
    const int tile = GenTopFill.active.width();

    // Title bar: the title sits on centre tiles between the two end pieces,
    // with fill tiles out to the corners on both sides.
    const int textWidth = skin.genTextWidth(m_title);
    const int centreTiles = qMax(1, (textWidth + tile - 1) / tile);
    const int centreWidth = centreTiles * tile;
    const int centreX = (w - centreWidth) / 2;
    painter.save();
    painter.setClipRect(QRect(0, 0, w, GenTopHeight));
    for (int x = 0; x < w; x += tile)
        skin.drawSprite(painter, {x, 0}, Sheet::Gen, pick(GenTopFill));
    for (int i = 0; i < centreTiles; ++i)
        skin.drawSprite(painter, {centreX + i * tile, 0}, Sheet::Gen, pick(GenTopCenterFill));
    skin.drawSprite(painter, {centreX - tile, 0}, Sheet::Gen, pick(GenTopLeftEnd));
    skin.drawSprite(painter, {centreX + centreWidth, 0}, Sheet::Gen, pick(GenTopRightEnd));
    skin.drawSprite(painter, {0, 0}, Sheet::Gen, pick(GenTopLeft));
    skin.drawSprite(painter, {w - tile, 0}, Sheet::Gen, pick(GenTopRight));
    skin.drawGenText(painter, {centreX + (centreWidth - textWidth) / 2, 4}, m_title, active);
    painter.restore();

    // Sides, with their bottom pieces sitting on the bottom bar.
    const int sideBottom = h - GenBottomHeight - GenLeftBottom.height();
    painter.save();
    painter.setClipRect(QRect(0, GenTopHeight, w, sideBottom - GenTopHeight));
    for (int y = GenTopHeight; y < sideBottom; y += GenLeftTile.height()) {
        skin.drawSprite(painter, {0, y}, Sheet::Gen, GenLeftTile);
        skin.drawSprite(painter, {w - GenRightWidth, y}, Sheet::Gen, GenRightTile);
    }
    painter.restore();
    skin.drawSprite(painter, {0, sideBottom}, Sheet::Gen, GenLeftBottom);
    skin.drawSprite(painter, {w - GenRightWidth, sideBottom}, Sheet::Gen, GenRightBottom);

    const int bottom = h - GenBottomHeight;
    for (int x = GenBottomLeft.width(); x < w - GenBottomRight.width(); x += GenBottomFill.width())
        skin.drawSprite(painter, {x, bottom}, Sheet::Gen, GenBottomFill);
    skin.drawSprite(painter, {0, bottom}, Sheet::Gen, GenBottomLeft);
    skin.drawSprite(painter, {w - GenBottomRight.width(), bottom}, Sheet::Gen, GenBottomRight);

    painter.save();
    const QRect content = contentRect();
    painter.setClipRect(content);
    paintContent(painter, skin, content);
    painter.restore();
}

bool GenericWindow::isDragArea(const QPoint &skinPos) const
{
    return skinPos.y() < GenTopHeight;
}

bool GenericWindow::isResizeArea(const QPoint &skinPos) const
{
    return skinPos.x() >= skinSize().width() - 20 && skinPos.y() >= skinSize().height() - GenBottomHeight;
}

QSize GenericWindow::constrainSize(const QSize &size) const
{
    // Grow in whole tiles from the minimum size, like the playlist.
    const auto steps = [](int value, int minimum, int step) {
        return minimum + qMax(0, (value - minimum + step / 2) / step) * step;
    };
    return {steps(size.width(), GenMinimumSize.width(), GenSizeStep.width()),
            steps(size.height(), GenMinimumSize.height(), GenSizeStep.height())};
}

void GenericWindow::layoutElements()
{
    if (!m_close)
        return; // called from the base constructor
    m_close->setRect(QRect(QPoint(skinSize().width() - GenTopRight.active.width() + GenCloseOffset.x(),
                                  GenCloseOffset.y()),
                           QSize(9, 9)));
    layoutContent(contentRect());
}

} // namespace newamp

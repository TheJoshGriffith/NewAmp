#include "ui/SkinElement.h"

#include "ui/SkinnedWindow.h"

namespace newamp {

SkinElement::SkinElement(SkinnedWindow *window, const QRect &rect)
    : m_window(window)
    , m_rect(rect)
{
}

void SkinElement::setRect(const QRect &rect)
{
    if (rect == m_rect)
        return;
    update();
    m_rect = rect;
    update();
}

void SkinElement::setVisible(bool visible)
{
    if (visible == m_visible)
        return;
    m_visible = visible;
    update();
}

void SkinElement::update()
{
    m_window->updateSkinRect(m_rect);
}

} // namespace newamp

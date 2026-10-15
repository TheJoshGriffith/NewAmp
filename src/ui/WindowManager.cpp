#include "ui/WindowManager.h"

#include "skin/DefaultSkin.h"
#include "skin/SkinLoader.h"
#include "skin/SkinSprites.h"
#include "ui/SkinnedWindow.h"

#include <QGuiApplication>
#include <QScreen>
#include <QSettings>

#include <cmath>

namespace newamp {

namespace {

bool overlapsRange(int a0, int a1, int b0, int b1, int slack)
{
    return a0 <= b1 + slack && b0 <= a1 + slack;
}

// Best single-axis snap: the candidate with the smallest |delta| <= distance.
void consider(int delta, int distance, std::optional<int> &best)
{
    if (qAbs(delta) <= distance && (!best || qAbs(delta) < qAbs(*best)))
        best = delta;
}

bool touching(const QRect &a, const QRect &b)
{
    const bool sideBySide = (a.right() + 1 == b.left() || b.right() + 1 == a.left())
        && overlapsRange(a.top(), a.bottom(), b.top(), b.bottom(), 0);
    const bool stacked = (a.bottom() + 1 == b.top() || b.bottom() + 1 == a.top())
        && overlapsRange(a.left(), a.right(), b.left(), b.right(), 0);
    return sideBySide || stacked;
}

} // namespace

WindowManager::WindowManager(QObject *parent)
    : QObject(parent)
    , m_default(DefaultSkin::create())
{
}

void WindowManager::addWindow(SkinnedWindow *window, bool shownByDefault)
{
    m_windows << window;
    if (!shownByDefault)
        m_hiddenByDefault << window;
    window->setWindowManager(this);
    window->setSkin(&skin());
}

bool WindowManager::loadSkin(const QString &path, QString *error)
{
    std::optional<Skin> loaded;
    if (path.startsWith(QLatin1String("theme:")))
        loaded = DefaultSkin::create(path.mid(6));
    else
        loaded = SkinLoader::load(path, m_default, error);
    if (!loaded)
        return false;
    m_loaded = std::move(loaded);
    m_skinPath = path;
    applySkin();
    return true;
}

void WindowManager::useDefaultSkin()
{
    m_loaded.reset();
    m_skinPath.clear();
    applySkin();
}

void WindowManager::applySkin()
{
    for (SkinnedWindow *window : m_windows)
        window->setSkin(&skin());
    Q_EMIT skinChanged();
}

void WindowManager::setScale(qreal scale)
{
    // Keep docked windows docked: scale positions relative to the leader.
    if (m_windows.isEmpty())
        return;
    const qreal old = m_windows.first()->scale();
    const QPoint origin = m_windows.first()->pos();
    for (SkinnedWindow *window : m_windows) {
        const QPoint offset = window->pos() - origin;
        window->setScale(scale);
        window->move(origin + (QPointF(offset) * window->scale() / old).toPoint());
    }
}

qreal WindowManager::scale() const
{
    return m_windows.isEmpty() ? 1.0 : m_windows.first()->scale();
}

qreal WindowManager::suggestedScale()
{
    // Roughly 1x at 720 logical pixels tall, in half steps: 1080p -> 1.5x,
    // 1440p -> 2x, 2160p -> 3x.
    QScreen *screen = QGuiApplication::primaryScreen();
    if (!screen)
        return 1.0;
    const qreal steps = std::round(screen->availableGeometry().height() / 700.0 * 2) / 2;
    return qBound(SkinnedWindow::MinScale, steps, SkinnedWindow::MaxScale);
}

void WindowManager::beginMove(SkinnedWindow *window)
{
    m_followers.clear();
    m_followerOffsets.clear();
    if (m_windows.isEmpty() || window != m_windows.first())
        return;

    QList<QRect> rects;
    QList<SkinnedWindow *> visible;
    for (SkinnedWindow *w : m_windows) {
        if (w->isVisible()) {
            rects << w->frameGeometry();
            visible << w;
        }
    }
    const QSet<int> group = dockedGroup(int(visible.indexOf(window)), rects);
    for (int i : group) {
        if (visible.at(i) == window)
            continue;
        m_followers << visible.at(i);
        m_followerOffsets << visible.at(i)->pos() - window->pos();
    }
}

void WindowManager::moveWindow(SkinnedWindow *window, const QPoint &desiredPos)
{
    QList<QRect> others;
    for (SkinnedWindow *w : m_windows)
        if (w != window && w->isVisible() && !m_followers.contains(w))
            others << w->frameGeometry();
    QList<QRect> screens;
    for (QScreen *screen : QGuiApplication::screens())
        screens << screen->availableGeometry();

    const QRect moving(desiredPos, window->frameGeometry().size());
    const QPoint pos = desiredPos + snapOffset(moving, others, screens, sprites::SnapDistance);
    window->move(pos);
    for (int i = 0; i < m_followers.size(); ++i)
        m_followers.at(i)->move(pos + m_followerOffsets.at(i));
}

void WindowManager::endMove(SkinnedWindow *)
{
    m_followers.clear();
    m_followerOffsets.clear();
}

QPoint WindowManager::snapOffset(const QRect &m, const QList<QRect> &windows, const QList<QRect> &screens,
                                 int distance)
{
    std::optional<int> dx;
    std::optional<int> dy;
    for (const QRect &t : windows) {
        if (overlapsRange(m.top(), m.bottom(), t.top(), t.bottom(), distance)) {
            consider(t.right() + 1 - m.left(), distance, dx); // dock to its right
            consider(t.left() - (m.right() + 1), distance, dx); // dock to its left
            consider(t.left() - m.left(), distance, dx);        // align left edges
            consider(t.right() - m.right(), distance, dx);      // align right edges
        }
        if (overlapsRange(m.left(), m.right(), t.left(), t.right(), distance)) {
            consider(t.bottom() + 1 - m.top(), distance, dy);
            consider(t.top() - (m.bottom() + 1), distance, dy);
            consider(t.top() - m.top(), distance, dy);
            consider(t.bottom() - m.bottom(), distance, dy);
        }
    }
    for (const QRect &s : screens) {
        if (!s.intersects(m.adjusted(-distance, -distance, distance, distance)))
            continue;
        consider(s.left() - m.left(), distance, dx);
        consider(s.right() - m.right(), distance, dx);
        consider(s.top() - m.top(), distance, dy);
        consider(s.bottom() - m.bottom(), distance, dy);
    }
    return {dx.value_or(0), dy.value_or(0)};
}

QSet<int> WindowManager::dockedGroup(int anchor, const QList<QRect> &rects)
{
    QSet<int> group;
    if (anchor < 0 || anchor >= rects.size())
        return group;
    QList<int> queue{anchor};
    group.insert(anchor);
    while (!queue.isEmpty()) {
        const int current = queue.takeFirst();
        for (int i = 0; i < rects.size(); ++i) {
            if (!group.contains(i) && touching(rects.at(current), rects.at(i))) {
                group.insert(i);
                queue << i;
            }
        }
    }
    return group;
}

void WindowManager::defaultLayout()
{
    if (m_windows.isEmpty())
        return;
    QScreen *screen = QGuiApplication::primaryScreen();
    const QRect area = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 800);
    const SkinnedWindow *main = m_windows.first();
    QPoint at(area.left() + area.width() / 2 - main->width() / 2, area.top() + 80);
    QPoint beside(at.x() + main->width(), at.y());
    for (SkinnedWindow *window : m_windows) {
        QPoint &next = m_hiddenByDefault.contains(window) ? beside : at;
        window->move(next);
        next.ry() += window->height();
    }
}

void WindowManager::saveLayout(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("windows"));
    settings.setValue(QStringLiteral("scale"), scale());
    for (const SkinnedWindow *window : m_windows) {
        settings.beginGroup(window->objectName());
        settings.setValue(QStringLiteral("pos"), window->pos());
        settings.setValue(QStringLiteral("size"), window->skinSize());
        settings.setValue(QStringLiteral("visible"), window->isVisible());
        settings.endGroup();
    }
    settings.endGroup();
    settings.setValue(QStringLiteral("skin/path"), m_skinPath);
}

void WindowManager::restoreLayout(QSettings &settings)
{
    const QString skinPath = settings.value(QStringLiteral("skin/path")).toString();
    if (!skinPath.isEmpty())
        loadSkin(skinPath);

    settings.beginGroup(QStringLiteral("windows"));
    const bool saved = settings.contains(QStringLiteral("scale"));
    if (!saved) {
        settings.endGroup();
        for (SkinnedWindow *window : m_windows)
            window->setScale(suggestedScale());
        defaultLayout();
        for (SkinnedWindow *window : m_windows)
            if (!m_hiddenByDefault.contains(window))
                window->show();
        return;
    }
    for (SkinnedWindow *window : m_windows)
        window->setScale(settings.value(QStringLiteral("scale"), 1.0).toDouble());

    // Only accept positions that are still on some screen.
    QRect desktop;
    for (QScreen *screen : QGuiApplication::screens())
        desktop |= screen->availableGeometry();
    for (int i = 0; i < m_windows.size(); ++i) {
        SkinnedWindow *window = m_windows.at(i);
        settings.beginGroup(window->objectName());
        window->setSkinSize(settings.value(QStringLiteral("size"), window->skinSize()).toSize());
        const QPoint pos = settings.value(QStringLiteral("pos")).toPoint();
        if (settings.contains(QStringLiteral("pos")) && desktop.intersects(QRect(pos, window->size())))
            window->move(pos);
        else if (i > 0) // new since the layout was saved: beside the main window
            window->move(m_windows.first()->pos() + QPoint(m_windows.first()->width(), 0));
        // The main window is always shown.
        const bool shownByDefault = !m_hiddenByDefault.contains(window);
        if (i == 0 || settings.value(QStringLiteral("visible"), shownByDefault).toBool())
            window->show();
        settings.endGroup();
    }
    settings.endGroup();
}

} // namespace newamp

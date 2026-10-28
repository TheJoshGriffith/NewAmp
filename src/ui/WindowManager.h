#pragma once

#include "skin/Skin.h"

#include <QList>
#include <QObject>
#include <QPoint>
#include <QRect>
#include <QSet>

#include <optional>

class QSettings;

namespace newamp {

class PlayerController;
class SkinnedWindow;

// Looks after all skinned windows together: the shared skin, snapping
// windows to each other and to screen edges, docked windows following the
// main window, and saving the layout.
class WindowManager : public QObject
{
    Q_OBJECT

public:
    explicit WindowManager(QObject *parent = nullptr);

    // The first window added leads: windows docked to it move with it.
    // Windows that aren't shown by default (library, visualizer) start
    // hidden, beside the main window.
    void addWindow(SkinnedWindow *window, bool shownByDefault = true);
    const QList<SkinnedWindow *> &windows() const { return m_windows; }

    // What transport keys and the volume wheel drive, in every window.
    void setController(PlayerController *controller) { m_controller = controller; }
    PlayerController *controller() const { return m_controller; }

    const Skin &skin() const { return m_loaded ? *m_loaded : m_default; }
    // A .wsz/.zip path, a skin folder, or "theme:<id>" for a built-in theme.
    bool loadSkin(const QString &path, QString *error = nullptr);
    void useDefaultSkin();
    QString skinPath() const { return m_skinPath; }
    static QString themePath(const QString &themeId) { return QStringLiteral("theme:") + themeId; }

    void setScale(qreal scale);
    qreal scale() const;

    // Keeps every window above other apps' windows.
    void setAlwaysOnTop(bool on);
    bool alwaysOnTop() const { return m_alwaysOnTop; }
    // A comfortable starting scale for the primary screen.
    static qreal suggestedScale();

    void beginMove(SkinnedWindow *window);
    void moveWindow(SkinnedWindow *window, const QPoint &desiredPos);
    void endMove(SkinnedWindow *window);
    // A window changed height (windowshade): windows docked under it move
    // with its bottom edge so the stack stays together.
    void windowResized(SkinnedWindow *window, const QRect &before);

    // Which windows are open is taken from just before quitting: by the time
    // the app quits, macOS has already closed every window.
    void saveLayout(QSettings &settings) const;
    void restoreLayout(QSettings &settings);
    // Classic stack: main, equalizer below it, playlist below that.
    void defaultLayout();

    // Offset that snaps `moving` to the nearest edge within `distance`:
    // other windows' outer edges (to dock) and aligned edges, and screens'
    // inner edges. Pure, for testing.
    static QPoint snapOffset(const QRect &moving, const QList<QRect> &windows, const QList<QRect> &screens,
                             int distance);
    // Indexes of rects connected to `anchor` through shared edges.
    static QSet<int> dockedGroup(int anchor, const QList<QRect> &rects);
    // Indexes of rects hanging under `anchor`: docked to it, or to each
    // other, through edges that all lie below its bottom edge.
    static QSet<int> dockedBelow(int anchor, const QList<QRect> &rects);

    // Notes which windows are open when the app is asked to quit.
    bool eventFilter(QObject *watched, QEvent *event) override;

Q_SIGNALS:
    void skinChanged();
    void alwaysOnTopChanged(bool on);

private:
    void applySkin();

    QList<SkinnedWindow *> m_windows;
    PlayerController *m_controller = nullptr;
    QList<SkinnedWindow *> m_hiddenByDefault;
    QList<SkinnedWindow *> m_followers;
    QList<QPoint> m_followerOffsets;
    Skin m_default;
    std::optional<Skin> m_loaded;
    QString m_skinPath;
    bool m_alwaysOnTop = false;
    std::optional<QSet<const SkinnedWindow *>> m_openAtQuit;
    bool m_restoring = false;
};

} // namespace newamp

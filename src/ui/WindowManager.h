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

    const Skin &skin() const { return m_loaded ? *m_loaded : m_default; }
    // A .wsz/.zip path, a skin folder, or "theme:<id>" for a built-in theme.
    bool loadSkin(const QString &path, QString *error = nullptr);
    void useDefaultSkin();
    QString skinPath() const { return m_skinPath; }
    static QString themePath(const QString &themeId) { return QStringLiteral("theme:") + themeId; }

    void setScale(qreal scale);
    qreal scale() const;
    // A comfortable starting scale for the primary screen.
    static qreal suggestedScale();

    void beginMove(SkinnedWindow *window);
    void moveWindow(SkinnedWindow *window, const QPoint &desiredPos);
    void endMove(SkinnedWindow *window);

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

Q_SIGNALS:
    void skinChanged();

private:
    void applySkin();

    QList<SkinnedWindow *> m_windows;
    QList<SkinnedWindow *> m_hiddenByDefault;
    QList<SkinnedWindow *> m_followers;
    QList<QPoint> m_followerOffsets;
    Skin m_default;
    std::optional<Skin> m_loaded;
    QString m_skinPath;
};

} // namespace newamp

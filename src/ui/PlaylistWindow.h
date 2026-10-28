#pragma once

#include "skin/SkinSprites.h"
#include "ui/SkinnedWindow.h"

#include <QList>
#include <QPair>

#include <functional>

namespace newamp {

class PlayerController;
class SkinButton;
class SkinHotspot;
class SkinListView;
class SkinSlider;
class SkinText;

// The classic resizable playlist editor.
class PlaylistWindow : public SkinnedWindow
{
    Q_OBJECT

public:
    PlaylistWindow(const Skin *skin, PlayerController &controller, QWidget *parent = nullptr);

    bool canShade() const override { return true; }

    QList<int> selectedRows() const;

Q_SIGNALS:
    // File info... in the context menu, for this playlist row.
    void fileInfoRequested(int row);

protected:
    void paintBackground(QPainter &painter, const Skin &skin) override;
    void paintShadeBackground(QPainter &painter, const Skin &skin) override;
    bool isDragArea(const QPoint &skinPos) const override;
    int titleBarHeight() const override { return sprites::PlTopHeight; }
    bool isResizeArea(const QPoint &skinPos) const override;
    QSize constrainSize(const QSize &size) const override;
    void layoutElements() override;

    void keyPressEvent(QKeyEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void addFiles();
    void addFolder();
    void addUrl();
    // Replace the list with a saved one, or save it, as .nap, M3U or PLS.
    void loadList();
    void saveList();
    void showMenu(const QList<QPair<QString, std::function<void()>>> &items);
    void updateScrollBar();
    void updateInfo();
    void updateShadeText();

    PlayerController &m_controller;
    SkinListView *m_view = nullptr;
    SkinSlider *m_scrollBar = nullptr;
    SkinButton *m_close = nullptr;
    SkinButton *m_shade = nullptr;
    SkinButton *m_unshade = nullptr;
    SkinButton *m_shadeClose = nullptr;
    SkinText *m_shadeTitle = nullptr;
    SkinText *m_shadeTime = nullptr;
    SkinHotspot *m_menus[5] = {};
    SkinText *m_info = nullptr;
};

} // namespace newamp

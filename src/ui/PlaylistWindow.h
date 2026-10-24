#pragma once

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

protected:
    void paintBackground(QPainter &painter, const Skin &skin) override;
    bool isDragArea(const QPoint &skinPos) const override;
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

    PlayerController &m_controller;
    SkinListView *m_view = nullptr;
    SkinSlider *m_scrollBar = nullptr;
    SkinButton *m_close = nullptr;
    SkinHotspot *m_menus[5] = {};
    SkinText *m_info = nullptr;
};

} // namespace newamp

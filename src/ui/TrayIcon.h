#pragma once

#include <QMenu>
#include <QObject>

class QAction;
class QSystemTrayIcon;
class QWidget;

namespace newamp {

class PlayerController;

// The player's menu outside its windows: the Dock menu on macOS, and a
// notification area (tray) icon elsewhere, which can be turned off.
class TrayIcon : public QObject
{
    Q_OBJECT

public:
    // `mainWindow` is shown, hidden and raised from the tray.
    TrayIcon(PlayerController &controller, QWidget *mainWindow, QObject *parent = nullptr);
    ~TrayIcon() override;

    // Whether this platform has a tray icon to turn on and off.
    static bool hasTray();
    void setVisible(bool visible);
    bool isVisible() const;
    // The Options menu's checkable "Show tray icon"; null without a tray.
    QAction *toggleAction() const { return m_toggle; }

    QMenu *menu() { return &m_menu; }

private:
    void updateMenu();
    void showMainWindow();

    PlayerController &m_controller;
    QWidget *m_mainWindow;
    QMenu m_menu;
    QAction *m_nowPlaying;
    QSystemTrayIcon *m_tray = nullptr;
    QAction *m_toggle = nullptr;
};

} // namespace newamp

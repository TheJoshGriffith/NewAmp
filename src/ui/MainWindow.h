#pragma once

#include "core/PlaybackEngine.h"
#include "ui/SkinnedWindow.h"

class QMenu;
class QTimer;

namespace newamp {

class PlayerController;
class PluginManager;
class SkinImage;
class SkinMarquee;
class SkinSlider;
class SkinText;
class SkinTimeDisplay;
class SkinToggleButton;
class SkinVisualizer;

// The classic main player window: transport, seek, volume, time and title.
class MainWindow : public SkinnedWindow
{
    Q_OBJECT

public:
    MainWindow(const Skin *skin, PlayerController &controller, QWidget *parent = nullptr);

    void openFiles();
    // Wires the EQ and PL buttons to the windows they show and hide.
    void setCompanionWindows(SkinnedWindow *equalizer, SkinnedWindow *playlist);
    // Lists another window in the menu with a show/hide toggle and shortcut.
    void addWindowToggle(const QString &name, SkinnedWindow *window, const QKeySequence &shortcut);
    // Adds the plugin list and each loaded plugin's actions to the menu.
    void setPluginManager(const PluginManager *plugins) { m_plugins = plugins; }

    bool canShade() const override { return true; }

    // Whether the time display counts down to the end of the track.
    bool showsTimeRemaining() const { return m_timeRemaining; }
    void setShowsTimeRemaining(bool remaining);

protected:
    void paintBackground(QPainter &painter, const Skin &skin) override;
    void paintShadeBackground(QPainter &painter, const Skin &skin) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    bool wheelChangesVolume() const override { return true; }
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void updateTitle();
    void updateState(PlaybackEngine::State state);
    void updatePosition(qint64 positionMs);
    void updateTime();
    void showMenuAtOptionsButton();
    void toggleDoubleSize();
    void setGlobalScale(qreal scale);
    void stepScale(int direction);
    void populateSkinsMenu(QMenu *menu);
    void addPluginMenus(QMenu *menu);
    void loadSkin(const QString &path);

    PlayerController &m_controller;
    const PluginManager *m_plugins = nullptr;
    QList<QAction *> m_windowToggles;
    SkinImage *m_stateIcon;
    SkinTimeDisplay *m_time;
    SkinTimeDisplay *m_shadeTime;
    bool m_timeRemaining = false;
    SkinMarquee *m_marquee;
    SkinSlider *m_seek;
    SkinSlider *m_shadeSeek;
    SkinSlider *m_volume;
    SkinSlider *m_balance;
    SkinText *m_kbps;
    SkinText *m_khz;
    SkinImage *m_mono;
    SkinImage *m_stereo;
    SkinVisualizer *m_visualizer;
    QTimer *m_visTimer;
    SkinToggleButton *m_shuffle;
    SkinToggleButton *m_repeat;
    SkinToggleButton *m_eqToggle;
    SkinToggleButton *m_plToggle;
    QTimer *m_marqueeTimer;
    qreal m_scaleBeforeDouble = 0; // set while "double size" is on
};

} // namespace newamp

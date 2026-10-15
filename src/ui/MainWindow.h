#pragma once

#include "core/PlaybackEngine.h"
#include "ui/SkinnedWindow.h"

class QMenu;
class QTimer;

namespace newamp {

class PlayerController;
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

protected:
    void paintBackground(QPainter &painter, const Skin &skin) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    void keyPressEvent(QKeyEvent *event) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dropEvent(QDropEvent *event) override;

private:
    void updateTitle();
    void updateState(PlaybackEngine::State state);
    void updatePosition(qint64 positionMs);
    void toggleDoubleSize();
    void setGlobalScale(qreal scale);
    void stepScale(int direction);
    void populateSkinsMenu(QMenu *menu);
    void loadSkin(const QString &path);

    PlayerController &m_controller;
    SkinImage *m_stateIcon;
    SkinTimeDisplay *m_time;
    SkinMarquee *m_marquee;
    SkinSlider *m_seek;
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

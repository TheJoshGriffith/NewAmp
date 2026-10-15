#pragma once

#include "core/PlaybackEngine.h"
#include "ui/SkinnedWindow.h"

class QTimer;

namespace newamp {

class PlayerController;
class SkinImage;
class SkinMarquee;
class SkinSlider;
class SkinTimeDisplay;

// The classic main player window: transport, seek, volume, time and title.
class MainWindow : public SkinnedWindow
{
    Q_OBJECT

public:
    MainWindow(const Skin *skin, PlayerController &controller, QWidget *parent = nullptr);

    void openFiles();

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

    PlayerController &m_controller;
    SkinImage *m_stateIcon;
    SkinTimeDisplay *m_time;
    SkinMarquee *m_marquee;
    SkinSlider *m_seek;
    SkinSlider *m_volume;
    QTimer *m_marqueeTimer;
};

} // namespace newamp

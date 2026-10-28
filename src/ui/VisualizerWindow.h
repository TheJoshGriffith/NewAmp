#pragma once

#include "ui/GenericWindow.h"
#include "ui/vis/VisEffect.h"

#include <QElapsedTimer>
#include <QImage>

#include <functional>
#include <memory>
#include <vector>

class QTimer;

namespace newamp {

class Visualization;

// A resizable window of full-size, music-driven visualizations: feedback
// trails, a kaleidoscope, a tunnel, fireworks, and big classic bars and
// scope. Click to change effect; right-click for the list and options.
// It only animates while it is shown and music is playing.
class VisualizerWindow : public GenericWindow
{
    Q_OBJECT

public:
    using SampleSource = std::function<int(float *out, int count)>;
    static constexpr QSize DefaultSize{275 + 4 * 25, 116 + 5 * 29};

    VisualizerWindow(const Skin *skin, SampleSource source, QWidget *parent = nullptr);
    ~VisualizerWindow() override;

    // Lists a plugin's visualization after the built-in effects. Not owned.
    void addVisualization(Visualization *visualization);

    int effectCount() const { return int(m_effects.size()); }
    QString effectName(int index) const;
    int currentEffect() const { return m_current; }
    void setEffect(int index);
    void nextEffect();

    // Change to a random effect on each new track.
    bool randomOnTrackChange() const { return m_randomOnTrack; }
    void setRandomOnTrackChange(bool on);
    // Move on to the next effect every so many seconds; 0 turns it off.
    int autoCycleSeconds() const { return m_cycleSeconds; }
    void setAutoCycleSeconds(int seconds);

    void setPlaying(bool playing);
    void trackChanged();

    // Pulls samples and renders one frame; the timer calls it ~30 times a second.
    void tick();
    const QImage &frame() const { return m_image; }

protected:
    void paintContent(QPainter &painter, const Skin &skin, const QRect &content) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;

private:
    void updateTimer();
    void render(const VisFrame &frame);
    void deactivate();
    bool inContent(const QPointF &pos) const;

    SampleSource m_source;
    std::vector<std::unique_ptr<VisEffect>> m_effects;
    VisAnalyzer m_analyzer;
    QImage m_image;
    QTimer *m_timer;
    QElapsedTimer m_frameClock; // time between frames
    QElapsedTimer m_nameClock;  // since the effect changed
    int m_current = 0;
    int m_active = -1;   // the effect that was last activated, if any
    QString m_savedName; // the effect to restore, which a plugin may add later
    bool m_playing = false;
    bool m_randomOnTrack = false;
    int m_cycleSeconds = 0;
    float m_sinceChange = 0; // seconds on the current effect
};

} // namespace newamp

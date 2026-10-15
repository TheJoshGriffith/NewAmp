#pragma once

#include "ui/SkinElement.h"

#include <array>
#include <functional>

namespace newamp {

// The main window's spectrum analyser / oscilloscope. Click to cycle modes.
class SkinVisualizer : public SkinElement
{
public:
    enum class Mode { Spectrum, Oscilloscope, Off };
    using SampleSource = std::function<int(float *out, int count)>;

    SkinVisualizer(SkinnedWindow *window, const QRect &rect, SampleSource source);

    Mode mode() const { return m_mode; }
    void setMode(Mode mode);

    // Pull new samples and decay the bars; call ~30 times a second.
    void tick();

    void paint(QPainter &painter, const Skin &skin) const override;
    bool acceptsMouse() const override { return true; }
    void mousePress(const QPoint &) override {}
    void mouseRelease(const QPoint &pos) override;

private:
    static constexpr int BarCount = 19; // 3px bars + 1px gaps across 76px

    SampleSource m_source;
    Mode m_mode = Mode::Spectrum;
    std::array<float, BarCount> m_bars{};
    std::array<float, BarCount> m_peaks{};
    std::array<float, 76> m_wave{};
    bool m_active = false;
};

} // namespace newamp

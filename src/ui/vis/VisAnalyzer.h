#pragma once

#include "audio/Spectrum.h"
#include "skin/Skin.h"

#include <array>

namespace newamp {

// Everything a visualizer effect knows about the music for one frame.
struct VisFrame
{
    static constexpr int WaveSize = Spectrum::FftSize;
    static constexpr int Bands = 32;

    std::array<float, WaveSize> wave{};  // mono samples, roughly -1..1, oldest first
    std::array<float, Bands> spectrum{}; // log-spaced 40 Hz .. 16 kHz, 0..1, falling gently
    float bass = 0;                      // 0..1 levels, smoothed
    float mid = 0;
    float treble = 0;
    bool beat = false;      // a beat starts on this frame
    float beatStrength = 0; // 0..1, how hard the latest beat hit
    float pulse = 0;        // beatStrength on a beat, then decays to 0
    double time = 0;        // seconds of audio seen so far
    float dt = 0;           // seconds since the previous frame
    bool silent = true;
    const VisColors *palette = nullptr; // the skin's viscolor.txt, for classic effects
};

// Turns blocks of recent samples into VisFrames: spectrum bands, bass, mid
// and treble levels, and a beat detector that compares bass energy with its
// own running average.
class VisAnalyzer
{
public:
    // samples holds VisFrame::WaveSize values, or is null when there is no
    // audio. dt is the time since the previous call, in seconds.
    const VisFrame &process(const float *samples, float dt);
    const VisFrame &frame() const { return m_frame; }
    void reset();

    // Bass energy must exceed its running average by this factor to count.
    static constexpr float BeatRatio = 1.5f;
    // Beats closer together than this are ignored (300 BPM).
    static constexpr float MinBeatInterval = 0.2f;

private:
    VisFrame m_frame;
    float m_bassAverage = 0;
    float m_sinceBeat = 1e9f;
    bool m_armed = true;
};

} // namespace newamp

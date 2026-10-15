#include "ui/vis/VisAnalyzer.h"

#include <algorithm>
#include <cmath>

namespace newamp {

namespace {

// Band ranges: bass up to ~260 Hz, mid up to ~3.6 kHz, treble above.
constexpr int BassEnd = 10;
constexpr int MidEnd = 24;

float average(const std::vector<float> &bars, int from, int to)
{
    float sum = 0;
    for (int i = from; i < to; ++i)
        sum += bars[size_t(i)];
    return sum / float(to - from);
}

// Bars are dB mapped onto 0..1 and music mostly sits in the upper half;
// higher bands are quieter in most music, so they start lower.
float level(float bar, float floor)
{
    return std::clamp((bar - floor) / 0.5f, 0.0f, 1.0f);
}

// Moves value towards target, quickly when rising and slowly when falling.
float follow(float value, float target, float dt, float rise, float fall)
{
    const float tau = target > value ? rise : fall;
    return value + (target - value) * (1.0f - std::exp(-dt / tau));
}

} // namespace

void VisAnalyzer::reset()
{
    const VisColors *palette = m_frame.palette;
    m_frame = VisFrame();
    m_frame.palette = palette;
    m_bassAverage = 0;
    m_sinceBeat = 1e9f;
    m_armed = true;
}

const VisFrame &VisAnalyzer::process(const float *samples, float dt)
{
    VisFrame &f = m_frame;
    dt = std::clamp(dt, 0.001f, 0.25f);
    f.dt = dt;
    f.time += double(dt);
    f.silent = samples == nullptr;
    f.beat = false;
    m_sinceBeat += dt;
    f.pulse *= std::exp(-dt / 0.18f);

    std::vector<float> bars(VisFrame::Bands, 0.0f);
    if (samples) {
        std::copy(samples, samples + VisFrame::WaveSize, f.wave.begin());
        bars = Spectrum::bars(samples, VisFrame::Bands);
    } else {
        f.wave.fill(0);
    }

    // Spectrum bands rise at once and fall smoothly, like the classic bars.
    const float fall = dt * 1.8f;
    for (int i = 0; i < VisFrame::Bands; ++i)
        f.spectrum[size_t(i)] = std::max(bars[size_t(i)], f.spectrum[size_t(i)] - fall);

    f.bass = follow(f.bass, level(average(bars, 0, BassEnd), 0.35f), dt, 0.03f, 0.25f);
    f.mid = follow(f.mid, level(average(bars, BassEnd, MidEnd), 0.3f), dt, 0.04f, 0.3f);
    f.treble = follow(f.treble, level(average(bars, MidEnd, VisFrame::Bands), 0.2f), dt, 0.04f, 0.3f);

    // Beat detection on linear bass energy (bars are -60..0 dB on 0..1).
    float energy = 0;
    for (int i = 0; i < BassEnd; ++i) {
        const float amplitude = std::pow(10.0f, (bars[size_t(i)] - 1.0f) * 3.0f);
        energy += amplitude * amplitude;
    }
    energy /= float(BassEnd);

    constexpr float Floor = 1e-4f; // about -40 dB: quieter bass never beats
    const float ratio = energy / std::max(m_bassAverage, Floor);
    if (m_armed && energy > Floor && ratio > BeatRatio && m_sinceBeat >= MinBeatInterval) {
        f.beat = true;
        f.beatStrength = std::clamp((ratio - 1.0f) / 4.0f, 0.25f, 1.0f);
        f.pulse = std::max(f.pulse, f.beatStrength);
        m_sinceBeat = 0;
        m_armed = false;
    } else if (ratio < 1.2f) {
        m_armed = true; // the next beat needs the energy to drop first
    }
    // The average follows about the last second of bass.
    m_bassAverage += (energy - m_bassAverage) * (1.0f - std::exp(-dt / 0.8f));
    return f;
}

} // namespace newamp

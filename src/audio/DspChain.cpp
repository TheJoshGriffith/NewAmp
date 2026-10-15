#include "audio/DspChain.h"

#include <cmath>

namespace newamp {

namespace {

constexpr double Pi = 3.14159265358979323846;
// Bandwidth of each peaking filter. Wide enough that neighbouring bands blend
// smoothly, like the classic equalizer.
constexpr double BandQ = 1.2;

double dbToGain(double db)
{
    return std::pow(10.0, db / 20.0);
}

} // namespace

void DspChain::configure(int sampleRate, int channels)
{
    m_sampleRate = qMax(1, sampleRate);
    m_channels = qMax(1, channels);
    updateFilters();
}

void DspChain::setParams(const DspParams &params)
{
    const bool filtersChanged = params.eqEnabled != m_params.eqEnabled || params.bandsDb != m_params.bandsDb
        || params.preampDb != m_params.preampDb;
    m_params = params;
    if (filtersChanged)
        updateFilters();
}

void DspChain::updateFilters()
{
    // Keep history for filters that survive so slider moves don't click.
    const std::vector<State> oldState = m_state;
    const size_t oldCount = m_filters.size();
    m_filters.clear();
    m_preampGain = 1.0;

    if (m_params.eqEnabled) {
        m_preampGain = dbToGain(m_params.preampDb);
        for (int band = 0; band < DspParams::BandCount; ++band) {
            const double freq = DspParams::BandFrequencies[band];
            const double gainDb = m_params.bandsDb[band];
            // Bands near or above Nyquist can't be represented; skip flat ones too.
            if (freq >= m_sampleRate * 0.45 || std::abs(gainDb) < 0.01)
                continue;
            // RBJ audio EQ cookbook peaking filter.
            const double a = std::pow(10.0, gainDb / 40.0);
            const double w0 = 2 * Pi * freq / m_sampleRate;
            const double alpha = std::sin(w0) / (2 * BandQ);
            const double cosw = std::cos(w0);
            const double a0 = 1 + alpha / a;
            Biquad f;
            f.b0 = (1 + alpha * a) / a0;
            f.b1 = (-2 * cosw) / a0;
            f.b2 = (1 - alpha * a) / a0;
            f.a1 = (-2 * cosw) / a0;
            f.a2 = (1 - alpha / a) / a0;
            m_filters.push_back(f);
        }
    }
    m_state.assign(m_filters.size() * size_t(m_channels), State{});
    if (m_filters.size() == oldCount && oldState.size() == m_state.size())
        m_state = oldState;
}

void DspChain::reset()
{
    std::fill(m_state.begin(), m_state.end(), State{});
}

void DspChain::process(float *samples, qint64 frames)
{
    const double volume = qBound(0.0, m_params.volume, 1.0);
    // Constant-power-ish balance: only the far side is attenuated.
    const double balance = qBound(-1.0, m_params.balance, 1.0);
    const double leftGain = balance > 0 ? 1.0 - balance : 1.0;
    const double rightGain = balance < 0 ? 1.0 + balance : 1.0;
    const size_t filterCount = m_filters.size();

    for (qint64 frame = 0; frame < frames; ++frame) {
        float *f = samples + frame * m_channels;
        for (int ch = 0; ch < m_channels; ++ch) {
            double x = f[ch] * m_preampGain;
            for (size_t i = 0; i < filterCount; ++i) {
                const Biquad &q = m_filters[i];
                State &s = m_state[i * size_t(m_channels) + size_t(ch)];
                const double y = q.b0 * x + q.b1 * s.x1 + q.b2 * s.x2 - q.a1 * s.y1 - q.a2 * s.y2;
                s.x2 = s.x1;
                s.x1 = x;
                s.y2 = s.y1;
                s.y1 = y;
                x = y;
            }
            double gain = volume;
            if (m_channels == 2)
                gain *= ch == 0 ? leftGain : rightGain;
            f[ch] = float(qBound(-1.0, x * gain, 1.0));
        }
    }
}

} // namespace newamp

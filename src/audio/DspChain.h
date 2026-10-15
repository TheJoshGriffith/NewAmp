#pragma once

#include <QtGlobal>

#include <array>
#include <vector>

namespace newamp {

// Parameters for one pass over the audio. Plain values so they can be copied
// across threads in one go.
struct DspParams
{
    static constexpr int BandCount = 10;
    // Centre frequencies of the classic ten-band equalizer, in Hz.
    static constexpr std::array<double, BandCount> BandFrequencies = {60,   170,  310,  600,   1000,
                                                                      3000, 6000, 12000, 14000, 16000};

    bool eqEnabled = false;
    double preampDb = 0.0;
    std::array<double, BandCount> bandsDb{};
    double balance = 0.0; // -1 left .. 0 centre .. +1 right
    double volume = 1.0;  // 0 .. 1, linear
};

// Equalizer, preamp, balance and volume over interleaved float frames.
// Not thread-safe: the owner serialises process() and setParams().
class DspChain
{
public:
    void configure(int sampleRate, int channels);
    void setParams(const DspParams &params);
    const DspParams &params() const { return m_params; }

    void process(float *samples, qint64 frames);
    void reset(); // clears filter history, e.g. after a seek

private:
    struct Biquad
    {
        double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0;
    };
    struct State
    {
        double x1 = 0, x2 = 0, y1 = 0, y2 = 0;
    };

    void updateFilters();

    DspParams m_params;
    int m_sampleRate = 44100;
    int m_channels = 2;
    std::vector<Biquad> m_filters;       // active bands only
    std::vector<State> m_state;          // per filter per channel
    double m_preampGain = 1.0;
};

} // namespace newamp

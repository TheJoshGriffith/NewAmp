#pragma once

#include <vector>

namespace newamp {

// Turns a block of mono samples into a few log-spaced bar heights for the
// classic spectrum display.
class Spectrum
{
public:
    static constexpr int FftSize = 512;

    // samples must hold FftSize values. Returns `bars` values in 0..1.
    static std::vector<float> bars(const float *samples, int bars, int sampleRate = 44100);
};

} // namespace newamp

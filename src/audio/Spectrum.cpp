#include "audio/Spectrum.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace newamp {

namespace {

constexpr double Pi = 3.14159265358979323846;

// In-place iterative radix-2 FFT.
void fft(std::vector<std::complex<float>> &a)
{
    const size_t n = a.size();
    for (size_t i = 1, j = 0; i < n; ++i) {
        size_t bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(a[i], a[j]);
    }
    for (size_t len = 2; len <= n; len <<= 1) {
        const double angle = -2 * Pi / double(len);
        const std::complex<float> wlen(float(std::cos(angle)), float(std::sin(angle)));
        for (size_t i = 0; i < n; i += len) {
            std::complex<float> w(1);
            for (size_t j = 0; j < len / 2; ++j) {
                const std::complex<float> u = a[i + j];
                const std::complex<float> v = a[i + j + len / 2] * w;
                a[i + j] = u + v;
                a[i + j + len / 2] = u - v;
                w *= wlen;
            }
        }
    }
}

} // namespace

std::vector<float> Spectrum::bars(const float *samples, int bars, int sampleRate)
{
    std::vector<std::complex<float>> data(FftSize);
    for (int i = 0; i < FftSize; ++i) {
        // Hann window to keep leakage from smearing the bars.
        const float w = float(0.5 - 0.5 * std::cos(2 * Pi * i / (FftSize - 1)));
        data[size_t(i)] = samples[i] * w;
    }
    fft(data);

    // Bars span 40 Hz .. 16 kHz on a log scale.
    std::vector<float> out(size_t(bars), 0.0f);
    const double lo = std::log(40.0);
    const double hi = std::log(16000.0);
    const double binHz = double(sampleRate) / FftSize;
    for (int b = 0; b < bars; ++b) {
        const double f0 = std::exp(lo + (hi - lo) * b / bars);
        const double f1 = std::exp(lo + (hi - lo) * (b + 1) / bars);
        int k0 = int(f0 / binHz);
        int k1 = int(f1 / binHz);
        k0 = std::max(1, std::min(k0, FftSize / 2 - 1));
        k1 = std::max(k0, std::min(k1, FftSize / 2 - 1));
        float peak = 0;
        for (int k = k0; k <= k1; ++k)
            peak = std::max(peak, std::abs(data[size_t(k)]));
        // Magnitude to dB, mapped from -60..0 dB onto 0..1.
        const float db = 20.0f * std::log10(peak / (FftSize / 4.0f) + 1e-9f);
        out[size_t(b)] = std::clamp((db + 60.0f) / 60.0f, 0.0f, 1.0f);
    }
    return out;
}

} // namespace newamp

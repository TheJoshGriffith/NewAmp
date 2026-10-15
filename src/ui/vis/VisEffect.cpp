#include "ui/vis/VisEffect.h"

#include <array>
#include <cmath>

namespace newamp {

// Each effect lives in its own file and is only reachable from here.
std::unique_ptr<VisEffect> createFeedbackEffect();
std::unique_ptr<VisEffect> createKaleidoscopeEffect();
std::unique_ptr<VisEffect> createTunnelEffect();
std::unique_ptr<VisEffect> createFireworksEffect();
std::unique_ptr<VisEffect> createSpectrumEffect();
std::unique_ptr<VisEffect> createOscilloscopeEffect();

std::vector<std::unique_ptr<VisEffect>> createVisEffects()
{
    std::vector<std::unique_ptr<VisEffect>> effects;
    effects.push_back(createFeedbackEffect());
    effects.push_back(createKaleidoscopeEffect());
    effects.push_back(createTunnelEffect());
    effects.push_back(createFireworksEffect());
    effects.push_back(createSpectrumEffect());
    effects.push_back(createOscilloscopeEffect());
    return effects;
}

QSize VisEffect::frameSize(const QSize &content) const
{
    constexpr double Budget = 256.0 * 192.0;
    const double area = double(qMax(1, content.width())) * qMax(1, content.height());
    const double f = std::min(1.0, std::sqrt(Budget / area));
    return {qMax(1, int(content.width() * f)), qMax(1, int(content.height() * f))};
}

namespace vis {

QRgb hsv(float h, float s, float v)
{
    h = (h - std::floor(h)) * 6.0f;
    const int i = int(h) % 6;
    const float f = h - float(int(h));
    const float p = v * (1 - s);
    const float q = v * (1 - s * f);
    const float t = v * (1 - s * (1 - f));
    float r = v, g = t, b = p;
    switch (i) {
    case 1: r = q, g = v, b = p; break;
    case 2: r = p, g = v, b = t; break;
    case 3: r = p, g = q, b = v; break;
    case 4: r = t, g = p, b = v; break;
    case 5: r = v, g = p, b = q; break;
    default: break;
    }
    const auto c = [](float x) { return int(std::clamp(x, 0.0f, 1.0f) * 255.0f + 0.5f); };
    return qRgb(c(r), c(g), c(b));
}

float fastSin(float x)
{
    constexpr int Size = 4096;
    static const std::array<float, Size> table = [] {
        std::array<float, Size> t{};
        for (int i = 0; i < Size; ++i)
            t[size_t(i)] = float(std::sin(2 * 3.14159265358979 * i / Size));
        return t;
    }();
    // Masking wraps negative angles too (two's complement).
    return table[size_t(int(x * (Size / 6.2831853f)) & (Size - 1))];
}

void fade(QImage &image, unsigned f)
{
    const int count = image.width() * image.height();
    auto *p = reinterpret_cast<QRgb *>(image.bits());
    for (int i = 0; i < count; ++i)
        p[i] = scaled(p[i], f);
}

void plot(QImage &image, int x, int y, QRgb color)
{
    if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
        return;
    QRgb *p = reinterpret_cast<QRgb *>(image.scanLine(y)) + x;
    *p = added(*p, color);
}

void glow(QImage &image, float x, float y, QRgb color)
{
    const int ix = int(std::floor(x));
    const int iy = int(std::floor(y));
    const QRgb dim = scaled(color, 64);
    plot(image, ix, iy, color);
    plot(image, ix - 1, iy, dim);
    plot(image, ix + 1, iy, dim);
    plot(image, ix, iy - 1, dim);
    plot(image, ix, iy + 1, dim);
}

void line(QImage &image, float x0, float y0, float x1, float y1, QRgb color)
{
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const bool steep = std::abs(dy) > std::abs(dx);
    const int steps = int(std::max(std::abs(dx), std::abs(dy))) + 1;
    for (int i = 0; i <= steps; ++i) {
        const float t = float(i) / float(steps);
        const float x = x0 + dx * t;
        const float y = y0 + dy * t;
        // Full colour on the nearest pixel, split across its neighbours on
        // the minor axis, so the line is about two pixels wide and smooth.
        const float minor = steep ? x : y;
        const int m = int(std::floor(minor));
        const unsigned fr = unsigned((minor - float(m)) * 256.0f);
        const int major = int(steep ? y : x);
        for (const auto &[offset, weight] : {std::pair{-1, 256 - fr}, std::pair{0, 256u}, std::pair{1, fr}}) {
            const QRgb c = scaled(color, weight);
            if (steep)
                plot(image, m + offset, major, c);
            else
                plot(image, major, m + offset, c);
        }
    }
}

} // namespace vis

} // namespace newamp

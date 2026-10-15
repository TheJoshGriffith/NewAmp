#include "ui/vis/VisEffect.h"

#include <cmath>
#include <cstring>

namespace newamp {

namespace {

// MilkDrop-style feedback: every frame zooms, turns and ripples the previous
// one, fades it with slowly drifting colour, and draws the waveform on top.
// Bass pushes the zoom; beats kick the rotation and now and then change the
// waveform's shape.
class FeedbackEffect : public VisEffect
{
public:
    QString name() const override { return QStringLiteral("Feedback"); }

    void render(QImage &image, const VisFrame &f) override
    {
        if (m_previous.size() != image.size())
            m_previous = QImage(image.size(), QImage::Format_RGB32);
        std::memcpy(m_previous.bits(), image.constBits(), size_t(image.sizeInBytes()));

        const float t = float(f.time);
        if (f.beat) {
            m_spinKick += (m_spinDirection = -m_spinDirection) * 0.035f * f.beatStrength;
            m_hue += 0.08f + 0.1f * f.beatStrength;
            if (++m_beats % 12 == 0)
                m_shape = (m_shape + 1) % ShapeCount;
        }
        m_spinKick *= std::exp(-f.dt / 0.4f);
        m_hue += f.dt * (0.03f + 0.1f * f.treble);
        warp(image, f, t);
        drawWave(image, f, t);
    }

private:
    static constexpr int ShapeCount = 3;

    void warp(QImage &image, const VisFrame &f, float t)
    {
        const int w = image.width();
        const int h = image.height();
        const float zoom = 1.012f + 0.025f * f.bass + 0.06f * f.pulse;
        const float angle = 0.008f * vis::fastSin(t * 0.23f) + m_spinKick;
        const float c = std::cos(angle) / zoom;
        const float s = std::sin(angle) / zoom;
        const float cx = (w - 1) * 0.5f;
        const float cy = (h - 1) * 0.5f;
        const float ripple = 0.6f + 1.6f * f.mid;
        const float scale = 6.2831853f / float(std::max(24, std::min(w, h) / 2));

        // Each channel fades at its own slowly changing rate, so trails
        // drift through colours as they shrink.
        const auto rate = [&](float phase) {
            return unsigned(236.0f + 14.0f * vis::fastSin(t * 0.17f + phase));
        };
        const unsigned fr = rate(0.0f), fg = rate(2.1f), fb = rate(4.2f);

        m_column.resize(size_t(w));
        for (int x = 0; x < w; ++x)
            m_column[size_t(x)] = ripple * vis::fastSin(float(x) * scale * 1.3f - t * 1.1f);

        const auto *src = reinterpret_cast<const QRgb *>(m_previous.constBits());
        for (int y = 0; y < h; ++y) {
            auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
            const float dy = float(y) - cy;
            const float rowRipple = ripple * vis::fastSin(dy * scale + t * 1.3f);
            for (int x = 0; x < w; ++x) {
                const float dx = float(x) - cx;
                const float sx = cx + dx * c - dy * s + rowRipple;
                const float sy = cy + dx * s + dy * c + m_column[size_t(x)];
                const int ix = int(std::floor(sx));
                const int iy = int(std::floor(sy));
                if (ix < 0 || iy < 0 || ix >= w - 1 || iy >= h - 1) {
                    out[x] = 0xff000000u;
                    continue;
                }
                // Bilinear sample in 8-bit fixed point.
                const unsigned ax = unsigned((sx - float(ix)) * 256.0f);
                const unsigned ay = unsigned((sy - float(iy)) * 256.0f);
                const QRgb *p = src + iy * w + ix;
                const auto channel = [&](int shift) {
                    const unsigned a = (p[0] >> shift) & 0xff, b = (p[1] >> shift) & 0xff;
                    const unsigned d = (p[w] >> shift) & 0xff, e = (p[w + 1] >> shift) & 0xff;
                    const unsigned top = a * (256 - ax) + b * ax;
                    const unsigned bottom = d * (256 - ax) + e * ax;
                    return (top * (256 - ay) + bottom * ay) >> 16;
                };
                out[x] = qRgb(int(channel(16) * fr >> 8), int(channel(8) * fg >> 8), int(channel(0) * fb >> 8));
            }
        }
    }

    void drawWave(QImage &image, const VisFrame &f, float t)
    {
        const float w = float(image.width());
        const float h = float(image.height());
        const float cx = w * 0.5f;
        const float cy = h * 0.5f;
        const QRgb color = vis::hsv(m_hue, 0.75f, 0.55f + 0.45f * std::max(f.pulse, f.bass));
        constexpr int Points = 96;
        const int step = VisFrame::WaveSize / Points;
        // Each point averages a few samples, which keeps hi-hats from
        // turning the line into noise.
        const auto sample = [&](int i) {
            float sum = 0;
            for (int k = 0; k < step; ++k)
                sum += f.wave[size_t((i + k) % VisFrame::WaveSize)];
            return std::clamp(sum / float(step) * 2.5f, -1.0f, 1.0f);
        };

        float px = 0, py = 0;
        for (int i = 0; i <= Points; ++i) {
            const int k = (i % Points) * step;
            float x = 0, y = 0;
            switch (m_shape) {
            case 0: { // a ring that breathes with the bass
                const float a = 6.2831853f * float(i) / Points + t * 0.3f;
                const float r = std::min(w, h) * (0.22f + 0.08f * f.bass + 0.14f * sample(k));
                x = cx + r * vis::fastCos(a);
                y = cy + r * vis::fastSin(a);
                break;
            }
            case 1: // a line across the middle
                if (i == Points)
                    return;
                x = w * float(i) / float(Points - 1);
                y = cy + h * 0.3f * sample(k);
                break;
            default: // a Lissajous figure from the wave against itself
                x = cx + w * 0.35f * sample(k);
                y = cy + h * 0.35f * sample((k + VisFrame::WaveSize / 4) % VisFrame::WaveSize);
                break;
            }
            if (i > 0)
                vis::line(image, px, py, x, y, color);
            px = x;
            py = y;
        }
    }

    QImage m_previous;
    std::vector<float> m_column;
    float m_hue = 0.55f;
    float m_spinKick = 0;
    float m_spinDirection = 1;
    int m_beats = 0;
    int m_shape = 0;
};

} // namespace

std::unique_ptr<VisEffect> createFeedbackEffect()
{
    return std::make_unique<FeedbackEffect>();
}

} // namespace newamp

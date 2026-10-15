#include "ui/vis/VisEffect.h"

#include <array>
#include <cmath>

namespace newamp {

namespace {

// A flowing plasma folded into mirrored wedges, in the spirit of Windows
// Media Player's "Alchemy". The spectrum feeds rings that ripple outwards
// from the centre, bass brightens the whole field, and beats change the
// number of mirrors and give the rotation a push.
class KaleidoscopeEffect : public VisEffect
{
public:
    QString name() const override { return QStringLiteral("Kaleidoscope"); }

    void render(QImage &image, const VisFrame &f) override
    {
        const int w = image.width();
        const int h = image.height();
        if (m_size != image.size())
            buildTables(image.size());

        const float t = float(f.time);
        if (f.beat) {
            m_spinSpeed += 0.6f * f.beatStrength;
            if (++m_beats % 8 == 0)
                m_mirrors = Mirrors[size_t(m_beats / 8) % Mirrors.size()];
        }
        m_spinSpeed += (0.12f + 0.3f * f.mid - m_spinSpeed) * (1.0f - std::exp(-f.dt / 0.8f));
        m_spin += f.dt * m_spinSpeed;

        // A cosine palette whose phases drift, so the colours keep cycling.
        std::array<QRgb, 256> palette;
        const float light = std::min(1.0f, 0.55f + 0.35f * f.bass + 0.35f * f.pulse);
        for (int i = 0; i < 256; ++i) {
            const float s = float(i) / 256.0f * 6.2831853f;
            const auto channel = [&](float phase) {
                return int(255.0f * light * (0.5f + 0.5f * vis::fastCos(s + phase)));
            };
            palette[size_t(i)] = qRgb(channel(t * 0.31f), channel(2.1f + t * 0.23f), channel(4.2f - t * 0.17f));
        }

        const unsigned wedge = 65536u / unsigned(m_mirrors);
        const unsigned spin = unsigned(int(m_spin * (65536.0f / 6.2831853f))) & 0xffffu;
        const float k1 = 5.0f + 2.0f * vis::fastSin(t * 0.11f);
        const float k2 = 6.0f + 2.0f * vis::fastSin(t * 0.07f + 1.0f);
        const float k3 = 4.0f + 1.5f * vis::fastSin(t * 0.13f + 2.0f);
        const float bandScale = float(VisFrame::Bands) * 0.7f;
        const float flow = t * 2.0f + f.bass * 2.0f;
        const float hueShift = t * 40.0f;

        for (int y = 0; y < h; ++y) {
            auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
            const Polar *polar = m_polar.data() + y * w;
            for (int x = 0; x < w; ++x) {
                const float r = polar[x].radius;
                // Fold the angle into one wedge, mirrored at its middle.
                unsigned a = ((polar[x].angle + spin) & 0xffffu) % wedge;
                if (a > wedge / 2)
                    a = wedge - a;
                const float angle = float(a) * (6.2831853f / 65536.0f);
                const float px = r * vis::fastCos(angle);
                const float py = r * vis::fastSin(angle);
                float v = vis::fastSin(px * k1 + t * 0.9f) + vis::fastSin(py * k2 - t * 1.1f)
                    + vis::fastSin((px + py) * k3 + t * 0.5f) + vis::fastSin(r * 7.0f - flow);
                const int band = std::min(VisFrame::Bands - 1, int(r * bandScale));
                v += 2.0f * f.spectrum[size_t(band)];
                out[x] = palette[size_t(int(v * 32.0f + hueShift) & 255)];
            }
        }
    }

private:
    static constexpr std::array<int, 4> Mirrors{6, 8, 12, 10};

    struct Polar
    {
        float radius;   // 1 at the nearer edge
        unsigned angle; // 0..65535 for a full turn
    };

    void buildTables(const QSize &size)
    {
        m_size = size;
        m_polar.resize(size_t(size.width()) * size_t(size.height()));
        const float cx = (size.width() - 1) * 0.5f;
        const float cy = (size.height() - 1) * 0.5f;
        const float unit = 2.0f / float(std::max(1, std::min(size.width(), size.height())));
        for (int y = 0; y < size.height(); ++y)
            for (int x = 0; x < size.width(); ++x) {
                const float dx = (float(x) - cx) * unit;
                const float dy = (float(y) - cy) * unit;
                const float a = std::atan2(dy, dx);
                m_polar[size_t(y * size.width() + x)] = {
                    std::sqrt(dx * dx + dy * dy),
                    unsigned(int(a * (65536.0f / 6.2831853f))) & 0xffffu};
            }
    }

    QSize m_size;
    std::vector<Polar> m_polar;
    float m_spin = 0;
    float m_spinSpeed = 0.12f;
    int m_mirrors = 6;
    int m_beats = 0;
};

} // namespace

std::unique_ptr<VisEffect> createKaleidoscopeEffect()
{
    return std::make_unique<KaleidoscopeEffect>();
}

} // namespace newamp

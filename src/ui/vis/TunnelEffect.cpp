#include "ui/vis/VisEffect.h"

#include <array>
#include <cmath>

namespace newamp {

namespace {

// Flying down a tunnel of chequered rings, in the spirit of Windows Media
// Player's "Battery". The bass sets the speed and lights the rings: each
// frame's energy starts deep in the tunnel and rushes out towards the
// viewer, so beats arrive as expanding waves of light. The tunnel's mouth
// wanders around the screen.
class TunnelEffect : public VisEffect
{
public:
    QString name() const override { return QStringLiteral("Tunnel"); }

    void render(QImage &image, const VisFrame &f) override
    {
        const int w = image.width();
        const int h = image.height();
        if (m_size != image.size())
            buildTables(image.size());

        const float t = float(f.time);
        m_travel += f.dt * (1.2f + 4.0f * f.bass + 6.0f * f.pulse);
        m_travel = std::fmod(m_travel, 64.0f); // the texture repeats every 256 rings
        m_spin += f.dt * (0.08f + 0.25f * f.mid) * m_spinDirection;
        m_spin -= std::floor(m_spin); // keeps texture coordinates positive
        if (f.beat && ++m_beats % 16 == 0)
            m_spinDirection = -m_spinDirection;
        m_head = (m_head + 1) % History;
        m_history[size_t(m_head)] = std::min(1.0f, 0.15f + 0.6f * f.bass + 0.7f * f.pulse);

        // Rings take their hue from a rainbow that turns over time.
        std::array<QRgb, 256> hues;
        const float hueBase = t * 0.05f;
        for (int i = 0; i < 256; ++i)
            hues[size_t(i)] = vis::hsv(hueBase + float(i) / 256.0f, 0.8f - 0.4f * f.treble, 1.0f);
        // Energy by depth: the newest value is deepest.
        std::array<unsigned, 64> energy;
        for (int d = 0; d < 64; ++d) {
            const int age = std::clamp((60 - d) * 2 / 3, 0, History - 1);
            energy[size_t(d)] = unsigned(256.0f * m_history[size_t((m_head - age + History) % History)]);
        }

        // The table is twice the image size; a moving window into it moves
        // the tunnel's centre.
        const int ox = int(float(w) * (0.5f + 0.22f * vis::fastSin(t * 0.37f)));
        const int oy = int(float(h) * (0.5f + 0.22f * vis::fastSin(t * 0.29f + 1.0f)));
        const int stride = w * 2;
        const float spin = m_spin * float(Segments);
        for (int y = 0; y < h; ++y) {
            auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
            const Cell *cells = m_cells.data() + (y + oy) * stride + ox;
            for (int x = 0; x < w; ++x) {
                const Cell &cell = cells[x];
                if (cell.fog == 0) {
                    out[x] = 0xff000000u;
                    continue;
                }
                const float u = cell.depth * 4.0f + m_travel * 4.0f;
                const float v = cell.angle * Segments + spin + cell.depth * 0.6f;
                const unsigned ring = unsigned(u);
                const float within = u - float(ring);
                const bool light = ((ring + unsigned(v)) & 1u) != 0;
                const unsigned e = energy[size_t(std::min(63, int(cell.depth * 6.0f)))];
                // Dark and light tiles, with a bright edge on every ring.
                unsigned level = (light ? 80u + e * 3 / 4 : 30u + e / 4);
                if (within < 0.14f)
                    level += e / 2 + 40;
                level = std::min(256u, level) * cell.fog >> 8;
                out[x] = vis::scaled(hues[size_t(ring * 13 & 255)], level);
            }
        }
    }

private:
    static constexpr int History = 48;
    static constexpr int Segments = 12;

    struct Cell
    {
        float depth;  // 1 / distance from the centre; larger is deeper
        float angle;  // 0..1 around the tunnel
        unsigned fog; // 0..256: the far end fades to black
    };

    void buildTables(const QSize &size)
    {
        m_size = size;
        const int w = size.width() * 2;
        const int h = size.height() * 2;
        m_cells.resize(size_t(w) * size_t(h));
        const float unit = 2.0f / float(std::max(1, std::min(size.width(), size.height())));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float dx = (float(x - size.width()) + 0.5f) * unit;
                const float dy = (float(y - size.height()) + 0.5f) * unit;
                const float r = std::sqrt(dx * dx + dy * dy);
                const float fog = std::clamp((r - 0.06f) * 4.0f, 0.0f, 1.0f);
                m_cells[size_t(y * w + x)] = {1.0f / std::max(r, 0.01f),
                                              std::atan2(dy, dx) / 6.2831853f + 0.5f,
                                              unsigned(fog * fog * 256.0f)};
            }
    }

    QSize m_size;
    std::vector<Cell> m_cells;
    std::array<float, History> m_history{};
    int m_head = 0;
    float m_travel = 0;
    float m_spin = 0;
    float m_spinDirection = 1;
    int m_beats = 0;
};

} // namespace

std::unique_ptr<VisEffect> createTunnelEffect()
{
    return std::make_unique<TunnelEffect>();
}

} // namespace newamp

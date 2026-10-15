#include "ui/vis/VisEffect.h"

#include <array>
#include <cmath>
#include <vector>

namespace newamp {

namespace {

// An endless dive into the Mandelbrot set. The camera zooms in, faster with
// the bass, turning slowly with the mids; the colour bands cycle with the
// music and flare on every beat. It steers towards the most detailed part
// of the view (the slowest-escaping points sit on the set's edge), so it
// never settles on flat colour or the set's black inside. When double
// precision runs out, it starts again from the next of a few famous spots.
class MandelbrotEffect : public VisEffect
{
public:
    QString name() const override { return QStringLiteral("Mandelbrot"); }

    // Each pixel can take hundreds of iterations, so render fewer of them.
    QSize frameSize(const QSize &content) const override
    {
        constexpr double Budget = 160.0 * 120.0;
        const double area = double(qMax(1, content.width())) * qMax(1, content.height());
        const double f = std::min(1.0, std::sqrt(Budget / area));
        return {qMax(1, int(content.width() * f)), qMax(1, int(content.height() * f))};
    }

    void render(QImage &image, const VisFrame &f) override
    {
        const int w = image.width();
        const int h = image.height();

        // Zoom in: by half every 2.5 seconds or so when quiet, much faster on loud bass.
        m_depth += double(f.dt) * (0.28 + 0.9 * double(f.bass) + 1.2 * double(f.pulse));
        if (m_depth > MaxDepth || !m_started) {
            if (m_started)
                m_start = (m_start + 1) % int(Starts.size());
            m_started = true;
            m_depth = 0;
            m_re = Starts[size_t(m_start)].re;
            m_im = Starts[size_t(m_start)].im;
        }
        m_angle += f.dt * (0.05f + 0.3f * f.mid);
        m_hue += f.dt * (0.03f + 0.25f * f.treble) + 0.08f * float(f.beat);

        const double scale = StartScale * std::exp(-m_depth); // half the view's height, in set units
        const double unit = 2.0 * scale / double(std::max(1, h));
        const double ca = std::cos(double(m_angle)) * unit;
        const double sa = std::sin(double(m_angle)) * unit;
        // More detail needs more iterations the deeper we go.
        const int maxIterations = std::min(200, 64 + int(m_depth * 5.0));
        // Cycle detection tolerance: well under a pixel, so edges stay sharp.
        const double cycleEpsilon = std::min(1e-10, unit * 1e-3);

        // One colour per iteration band, brighter while a beat rings.
        const float value = 0.75f + 0.25f * f.pulse;
        for (int i = 0; i < PaletteSize; ++i) {
            const float band = float(i) / float(PaletteSize);
            m_palette[size_t(i)] = vis::hsv(m_hue + band * 3.0f, 0.85f - 0.3f * f.pulse,
                                            value * (0.55f + 0.45f * vis::fastSin(band * 37.699f)));
        }

        // Render, and find the most detailed point near the middle to head for.
        double bestScore = -1e300;
        double bestX = 0;
        double bestY = 0;
        for (int y = 0; y < h; ++y) {
            auto *out = reinterpret_cast<QRgb *>(image.scanLine(y));
            const double dy = double(y) - double(h) * 0.5 + 0.5;
            for (int x = 0; x < w; ++x) {
                const double dx = double(x) - double(w) * 0.5 + 0.5;
                const double n = escape(m_re + dx * ca - dy * sa, m_im + dx * sa + dy * ca, maxIterations,
                                        cycleEpsilon);
                if (n < 0) {
                    out[x] = 0xff000000u;
                    continue;
                }
                out[x] = m_palette[size_t(int(n * 6.0) & (PaletteSize - 1))];
                const double score = n - 0.15 * std::sqrt(dx * dx + dy * dy);
                if (score > bestScore) {
                    bestScore = score;
                    bestX = dx;
                    bestY = dy;
                }
            }
        }
        if (bestScore == -1e300) {
            m_depth = std::max(0.0, m_depth - double(f.dt) * 3.0); // all inside: back out
            return;
        }
        // Drift towards it gently, so the view glides rather than jumps.
        const double k = std::min(1.0, double(f.dt) * 1.5);
        m_re += (bestX * ca - bestY * sa) * k;
        m_im += (bestX * sa + bestY * ca) * k;
    }

private:
    struct Point
    {
        double re;
        double im;
    };
    // Where each dive starts: points with detail all the way down.
    static constexpr std::array<Point, 4> Starts = {{
        {-0.743643887037151, 0.131825904205330}, // seahorse valley
        {-0.101096363845622, 0.956286510809142}, // a spiral on the top bulb
        {0.001643721971153, -0.822467633298876}, // a dendrite below
        {-1.768778833, -0.001738996},            // a mini-set on the needle
    }};
    static constexpr double StartScale = 1.6;
    static constexpr double MaxDepth = 27.0; // about 1e-12: double precision's limit
    static constexpr int PaletteSize = 256;

    // A smooth escape count, so colour bands blend instead of stepping; -1
    // for points inside the set.
    static double escape(double cr, double ci, int maxIterations, double cycleEpsilon)
    {
        // The main cardioid and the period-2 bulb are inside: no need to iterate.
        const double q = (cr - 0.25) * (cr - 0.25) + ci * ci;
        if (q * (q + (cr - 0.25)) <= 0.25 * ci * ci || (cr + 1.0) * (cr + 1.0) + ci * ci <= 0.0625)
            return -1;
        double zr = 0;
        double zi = 0;
        double zr2 = 0;
        double zi2 = 0;
        // Points that fall into a cycle are inside too; checking against a
        // saved z every so often catches most of them early.
        double savedR = 0;
        double savedI = 0;
        int n = 0;
        while (zr2 + zi2 <= 256.0) {
            if (++n > maxIterations)
                return -1;
            zi = 2.0 * zr * zi + ci;
            zr = zr2 - zi2 + cr;
            zr2 = zr * zr;
            zi2 = zi * zi;
            if (std::abs(zr - savedR) < cycleEpsilon && std::abs(zi - savedI) < cycleEpsilon)
                return -1;
            if ((n & 15) == 0) {
                savedR = zr;
                savedI = zi;
            }
        }
        return double(n) + 1.0 - std::log2(std::log2(zr2 + zi2) * 0.5);
    }

    std::array<QRgb, PaletteSize> m_palette{};
    bool m_started = false;
    int m_start = 0;
    double m_re = 0;
    double m_im = 0;
    double m_depth = 0;
    float m_angle = 0;
    float m_hue = 0;
};

} // namespace

std::unique_ptr<VisEffect> createMandelbrotEffect()
{
    return std::make_unique<MandelbrotEffect>();
}

} // namespace newamp

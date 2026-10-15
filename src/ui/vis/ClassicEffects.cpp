#include "ui/vis/VisEffect.h"

#include <cmath>

namespace newamp {

namespace {

// The classic analyser and oscilloscope, blown up to window size. They use
// the skin's viscolor.txt palette and chunky pixels so classic skins look
// at home.
class ClassicEffect : public VisEffect
{
public:
    QSize frameSize(const QSize &content) const override
    {
        return {qMax(1, content.width() / 2), qMax(1, content.height() / 2)};
    }
    bool smoothScaling() const override { return false; }

protected:
    static const VisColors &colors(const VisFrame &f)
    {
        if (f.palette)
            return *f.palette;
        // Without a skin: the classic default colours.
        static const VisColors fallback = [] {
            VisColors c;
            c[0] = QColor(0, 0, 0);
            c[1] = QColor(24, 33, 41);
            for (int i = 0; i < 16; ++i)
                c[size_t(2 + i)] = QColor::fromHsv(i * 120 / 15, 220, 230); // red at the top to green
            for (int i = 0; i < 5; ++i)
                c[size_t(18 + i)] = QColor(255 - i * 25, 255 - i * 25, 255 - i * 25);
            c[23] = QColor(150, 150, 150);
            return c;
        }();
        return fallback;
    }

    // Background with grid dots on every other pixel, as in the main window.
    static void paintGrid(QImage &image, const VisColors &c)
    {
        image.fill(c[0]);
        const QRgb dot = c[1].rgb();
        for (int y = 1; y < image.height(); y += 2) {
            auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
            for (int x = 0; x < image.width(); x += 2)
                row[x] = dot;
        }
    }
};

class SpectrumEffect : public ClassicEffect
{
public:
    QString name() const override { return QStringLiteral("Spectrum"); }

    void render(QImage &image, const VisFrame &f) override
    {
        const VisColors &c = colors(f);
        paintGrid(image, c);
        const int w = image.width();
        const int h = image.height();
        const int slot = std::max(1, w / VisFrame::Bands);
        const int barWidth = slot >= 3 ? slot - 1 : slot;
        const int left = std::max(0, (w - slot * VisFrame::Bands) / 2);
        for (int i = 0; i < VisFrame::Bands; ++i) {
            const float value = f.spectrum[size_t(i)];
            float &peak = m_peaks[size_t(i)];
            peak = value >= peak ? value : std::max(0.0f, peak - f.dt * 0.35f);

            const int x = left + i * slot;
            if (x >= w)
                break; // too narrow for every band
            const int height = int(value * float(h) + 0.5f);
            for (int y = h - height; y < h; ++y) {
                // Colour by absolute height: colour 2 is at the very top.
                const QRgb color = c[size_t(2 + y * 16 / h)].rgb();
                auto *row = reinterpret_cast<QRgb *>(image.scanLine(y));
                for (int dx = 0; dx < barWidth && x + dx < w; ++dx)
                    row[x + dx] = color;
            }
            const int peakY = h - int(peak * float(h) + 0.5f);
            if (peak > 0 && peakY >= 0 && peakY < h) {
                auto *row = reinterpret_cast<QRgb *>(image.scanLine(peakY));
                for (int dx = 0; dx < barWidth && x + dx < w; ++dx)
                    row[x + dx] = c[23].rgb();
            }
        }
    }

private:
    std::array<float, VisFrame::Bands> m_peaks{};
};

class OscilloscopeEffect : public ClassicEffect
{
public:
    QString name() const override { return QStringLiteral("Oscilloscope"); }

    void render(QImage &image, const VisFrame &f) override
    {
        const VisColors &c = colors(f);
        paintGrid(image, c);
        const int w = image.width();
        const int h = image.height();
        const int mid = h / 2;
        int previous = -1;
        for (int x = 0; x < w; ++x) {
            const float v = std::clamp(f.wave[size_t(x * VisFrame::WaveSize / w)] * 2.0f, -1.0f, 1.0f);
            const int y = std::clamp(int((1.0f - v) * 0.5f * float(h - 1) + 0.5f), 0, h - 1);
            // Join each sample to the last one so steep edges stay solid.
            const int from = previous < 0 ? y : std::min(previous, y);
            const int to = previous < 0 ? y : std::max(previous, y);
            for (int yy = from; yy <= to; ++yy) {
                const int band = std::min(4, std::abs(yy - mid) * 5 / (mid + 1));
                reinterpret_cast<QRgb *>(image.scanLine(yy))[x] = c[size_t(18 + band)].rgb();
            }
            previous = y;
        }
    }
};

} // namespace

std::unique_ptr<VisEffect> createSpectrumEffect()
{
    return std::make_unique<SpectrumEffect>();
}

std::unique_ptr<VisEffect> createOscilloscopeEffect()
{
    return std::make_unique<OscilloscopeEffect>();
}

} // namespace newamp

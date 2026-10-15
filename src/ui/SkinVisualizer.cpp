#include "ui/SkinVisualizer.h"

#include "audio/Spectrum.h"
#include "skin/Skin.h"

#include <QPainter>

#include <cmath>

namespace newamp {

SkinVisualizer::SkinVisualizer(SkinnedWindow *window, const QRect &rect, SampleSource source)
    : SkinElement(window, rect)
    , m_source(std::move(source))
{
}

void SkinVisualizer::setMode(Mode mode)
{
    m_mode = mode;
    m_bars.fill(0);
    m_peaks.fill(0);
    m_wave.fill(0);
    update();
}

void SkinVisualizer::mouseRelease(const QPoint &pos)
{
    if (rect().contains(pos))
        setMode(Mode((int(m_mode) + 1) % 3));
}

void SkinVisualizer::tick()
{
    if (m_mode == Mode::Off)
        return;
    float samples[Spectrum::FftSize];
    const int got = m_source ? m_source(samples, Spectrum::FftSize) : 0;
    const bool active = got == Spectrum::FftSize;

    if (m_mode == Mode::Spectrum) {
        const std::vector<float> fresh = active ? Spectrum::bars(samples, BarCount) : std::vector<float>(BarCount, 0);
        for (int i = 0; i < BarCount; ++i) {
            // Rise instantly, fall gently; peaks hang then drop slowly.
            m_bars[i] = std::max(fresh[size_t(i)], m_bars[i] - 0.06f);
            m_peaks[i] = m_bars[i] >= m_peaks[i] ? m_bars[i] : std::max(0.0f, m_peaks[i] - 0.015f);
        }
    } else if (active) {
        const int step = Spectrum::FftSize / int(m_wave.size());
        for (size_t x = 0; x < m_wave.size(); ++x)
            m_wave[x] = samples[x * size_t(step)];
    } else {
        m_wave.fill(0);
    }
    if (active || m_active)
        update();
    m_active = active;
}

void SkinVisualizer::paint(QPainter &painter, const Skin &skin) const
{
    const VisColors &c = skin.visColors();
    const QRect r = rect();
    painter.fillRect(r, c[0]);
    // Grid dots on every other pixel, as in the classic display.
    for (int y = r.top() + 1; y <= r.bottom(); y += 2)
        for (int x = r.left(); x <= r.right(); x += 2)
            painter.fillRect(QRect(x, y, 1, 1), c[1]);

    const int h = r.height();
    if (m_mode == Mode::Spectrum) {
        for (int i = 0; i < BarCount; ++i) {
            const int x = r.left() + i * 4;
            const int height = qRound(m_bars[size_t(i)] * h);
            for (int y = 0; y < height; ++y) {
                // Colour by absolute height: row 0 at the top uses colour 2.
                const int row = h - 1 - y;
                painter.fillRect(QRect(x, r.top() + row, 3, 1), c[size_t(2 + row * 16 / h)]);
            }
            const int peak = qRound(m_peaks[size_t(i)] * h);
            if (peak > 0)
                painter.fillRect(QRect(x, r.top() + h - peak, 3, 1), c[23]);
        }
    } else if (m_mode == Mode::Oscilloscope) {
        for (int x = 0; x < int(m_wave.size()); ++x) {
            const float v = qBound(-1.0f, m_wave[size_t(x)] * 2.0f, 1.0f);
            const int y = qBound(0, qRound((1 - v) * 0.5f * (h - 1)), h - 1);
            const int band = qMin(4, std::abs(y - h / 2) * 5 / (h / 2 + 1));
            painter.fillRect(QRect(r.left() + x, r.top() + y, 1, 1), c[size_t(18 + band)]);
        }
    }
}

} // namespace newamp

#include "SoundEffects.h"

#include <QAction>
#include <QActionGroup>
#include <QCoreApplication>

#include <cmath>

namespace newamp::effects {

namespace {

constexpr float Pi = 3.14159265f;

// Coefficient of a one-pole low-pass at cutoff Hz.
float lowPass(float cutoff, int sampleRate)
{
    return 1.0f - std::exp(-2.0f * Pi * cutoff / float(qMax(1, sampleRate)));
}

QString tr(const char *text)
{
    return QCoreApplication::translate("SoundEffects", text);
}

} // namespace

QString VocalRemover::name() const
{
    return tr("Vocal remover");
}

void VocalRemover::configure(int sampleRate, int channels)
{
    m_channels = channels;
    m_lowCoeff = lowPass(150, sampleRate);
}

void VocalRemover::reset()
{
    m_low1 = m_low2 = 0;
}

void VocalRemover::process(float *samples, qint64 frames)
{
    if (m_channels != 2)
        return; // nothing to cancel in mono
    for (qint64 i = 0; i < frames; ++i) {
        float &l = samples[i * 2];
        float &r = samples[i * 2 + 1];
        const float mid = (l + r) * 0.5f;
        m_low1 += m_lowCoeff * (mid - m_low1);
        m_low2 += m_lowCoeff * (m_low1 - m_low2);
        const float side = (l - r) * 0.5f;
        l = side + m_low2;
        r = -side + m_low2;
    }
}

StereoWidener::StereoWidener(QObject *owner)
{
    auto *group = new QActionGroup(owner);
    const std::pair<float, const char *> Widths[] = {
        {0.5f, QT_TRANSLATE_NOOP("SoundEffects", "Narrow (50%)")},
        {1.25f, QT_TRANSLATE_NOOP("SoundEffects", "Slightly wider (125%)")},
        {1.5f, QT_TRANSLATE_NOOP("SoundEffects", "Wider (150%)")},
        {2.0f, QT_TRANSLATE_NOOP("SoundEffects", "Widest (200%)")},
    };
    for (const auto &[width, label] : Widths) {
        auto *action = new QAction(tr(label), owner);
        action->setCheckable(true);
        action->setChecked(qFuzzyCompare(width, m_width.load()));
        QObject::connect(action, &QAction::triggered, owner, [this, w = width] { setWidth(w); });
        group->addAction(action);
        m_actions << action;
    }
}

QString StereoWidener::name() const
{
    return tr("Stereo widener");
}

void StereoWidener::configure(int sampleRate, int channels)
{
    Q_UNUSED(sampleRate)
    m_channels = channels;
}

void StereoWidener::process(float *samples, qint64 frames)
{
    if (m_channels != 2)
        return;
    const float width = m_width;
    // Keep loudness roughly level as the side signal grows.
    const float gain = 2.0f / (1.0f + width);
    for (qint64 i = 0; i < frames; ++i) {
        float &l = samples[i * 2];
        float &r = samples[i * 2 + 1];
        const float mid = (l + r) * 0.5f;
        const float side = (l - r) * 0.5f * width;
        l = (mid + side) * gain;
        r = (mid - side) * gain;
    }
}

QString Crossfeed::name() const
{
    return tr("Headphone crossfeed");
}

void Crossfeed::configure(int sampleRate, int channels)
{
    m_channels = channels;
    m_lowCoeff = lowPass(700, sampleRate);
    // About the time sound takes to reach the far ear.
    const size_t delayFrames = size_t(qMax(1, sampleRate * 3 / 10000));
    m_delay.assign(delayFrames * 2, 0.0f);
}

void Crossfeed::reset()
{
    m_lowL = m_lowR = 0;
    std::fill(m_delay.begin(), m_delay.end(), 0.0f);
    m_delayAt = 0;
}

void Crossfeed::process(float *samples, qint64 frames)
{
    if (m_channels != 2 || m_delay.empty())
        return;
    constexpr float Feed = 0.35f;            // about -9 dB to the far ear
    constexpr float Gain = 1.0f / (1 + Feed); // so a centred sound stays as loud
    for (qint64 i = 0; i < frames; ++i) {
        float &l = samples[i * 2];
        float &r = samples[i * 2 + 1];
        m_lowL += m_lowCoeff * (l - m_lowL);
        m_lowR += m_lowCoeff * (r - m_lowR);
        const float farL = m_delay[m_delayAt];
        const float farR = m_delay[m_delayAt + 1];
        m_delay[m_delayAt] = m_lowL;
        m_delay[m_delayAt + 1] = m_lowR;
        m_delayAt = (m_delayAt + 2) % m_delay.size();
        l = (l + farR * Feed) * Gain;
        r = (r + farL * Feed) * Gain;
    }
}

} // namespace newamp::effects

#pragma once

#include "plugins/DspEffect.h"

#include <QObject>

#include <atomic>
#include <vector>

namespace newamp::effects {

// Cancels whatever sits in the middle of a stereo mix, usually the lead
// vocal, by subtracting one channel from the other. Bass is often centred
// too, so the low end is kept.
class VocalRemover final : public DspEffect
{
public:
    QString id() const override { return QStringLiteral("effects.vocalremover"); }
    QString name() const override;
    void configure(int sampleRate, int channels) override;
    void process(float *samples, qint64 frames) override;
    void reset() override;

private:
    int m_channels = 2;
    float m_lowCoeff = 0; // two one-pole low-passes of the mid signal
    float m_low1 = 0;
    float m_low2 = 0;
};

// Widens or narrows the stereo image by scaling the side (left minus
// right) signal against the mid.
class StereoWidener final : public DspEffect
{
public:
    explicit StereoWidener(QObject *owner);

    QString id() const override { return QStringLiteral("effects.widener"); }
    QString name() const override;
    void configure(int sampleRate, int channels) override;
    void process(float *samples, qint64 frames) override;
    QList<QAction *> actions() override { return m_actions; }

    float width() const { return m_width; }
    void setWidth(float width) { m_width = width; }

private:
    int m_channels = 2;
    std::atomic<float> m_width{1.5f}; // 1 leaves the mix alone
    QList<QAction *> m_actions;
};

// For headphones: feeds each ear a little of the other channel, delayed
// and darkened as it would arrive around the head, so hard-panned mixes
// tire the ears less.
class Crossfeed final : public DspEffect
{
public:
    QString id() const override { return QStringLiteral("effects.crossfeed"); }
    QString name() const override;
    void configure(int sampleRate, int channels) override;
    void process(float *samples, qint64 frames) override;
    void reset() override;

private:
    int m_channels = 2;
    float m_lowCoeff = 0;
    float m_lowL = 0, m_lowR = 0; // low-passed copies of each side
    std::vector<float> m_delay;   // the low-passed sides, interleaved
    size_t m_delayAt = 0;
};

} // namespace newamp::effects

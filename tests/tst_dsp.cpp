#include "audio/DspRack.h"
#include "plugins/DspEffect.h"

#ifdef NEWAMP_TEST_EFFECTS
#include "SoundEffects.h"
#endif

#include <QObject>
#include <QTest>

#include <cmath>
#include <vector>

using namespace newamp;

namespace {

// Multiplies by a factor and records what the rack asked of it.
class Gain final : public DspEffect
{
public:
    Gain(QString id, float factor)
        : m_id(std::move(id))
        , m_factor(factor)
    {
    }

    QString id() const override { return m_id; }
    QString name() const override { return m_id; }
    void configure(int sampleRate, int channels) override
    {
        configuredRate = sampleRate;
        configuredChannels = channels;
        ++configures;
    }
    void process(float *samples, qint64 frames) override
    {
        for (qint64 i = 0; i < frames * configuredChannels; ++i)
            samples[i] = samples[i] * m_factor + offset;
        processed += frames;
    }
    void reset() override { ++resets; }

    int configuredRate = 0;
    int configuredChannels = 0;
    int configures = 0;
    int resets = 0;
    qint64 processed = 0;
    float offset = 0;

private:
    QString m_id;
    float m_factor;
};

std::vector<float> stereo(qint64 frames, float left, float right)
{
    std::vector<float> samples(size_t(frames * 2));
    for (qint64 i = 0; i < frames; ++i) {
        samples[size_t(i * 2)] = left;
        samples[size_t(i * 2 + 1)] = right;
    }
    return samples;
}

#ifdef NEWAMP_TEST_EFFECTS
// A stereo sine at freq with the given channel gains, after `effect`.
std::vector<float> throughEffect(DspEffect &effect, float freq, float left, float right, int frames = 44100)
{
    std::vector<float> samples(size_t(frames * 2));
    for (int i = 0; i < frames; ++i) {
        const float s = 0.5f * std::sin(2 * float(M_PI) * freq * float(i) / 44100);
        samples[size_t(i * 2)] = s * left;
        samples[size_t(i * 2 + 1)] = s * right;
    }
    effect.configure(44100, 2);
    effect.reset();
    effect.process(samples.data(), frames);
    return samples;
}

// RMS of one channel over the second half, after the filters settle.
float rms(const std::vector<float> &samples, int channel)
{
    const size_t frames = samples.size() / 2;
    double sum = 0;
    for (size_t i = frames / 2; i < frames; ++i)
        sum += double(samples[i * 2 + size_t(channel)]) * samples[i * 2 + size_t(channel)];
    return float(std::sqrt(sum / double(frames - frames / 2)));
}
#endif

} // namespace

class TestDsp : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void runsEnabledEffectsInOrder()
    {
        DspRack rack;
        Gain doubler(QStringLiteral("double"), 2);
        Gain plusOne(QStringLiteral("plus"), 1);
        plusOne.offset = 1;
        Gain off(QStringLiteral("off"), 0);
        rack.add(&doubler, true);
        rack.add(&plusOne, true);
        rack.add(&off);
        rack.add(&doubler, false); // already there: ignored
        QCOMPARE(rack.effects().size(), 3);
        QCOMPARE(rack.enabledIds(), QStringList({QStringLiteral("double"), QStringLiteral("plus")}));

        auto samples = stereo(4, 0.25f, -0.25f);
        rack.process(samples.data(), 4, 48000, 2);
        QCOMPARE(samples[0], 1.5f); // 0.25 * 2 + 1, in that order
        QCOMPARE(samples[1], 0.5f);
        QCOMPARE(off.processed, 0);
        QCOMPARE(doubler.configuredRate, 48000);
        QCOMPARE(doubler.configuredChannels, 2);
    }

    void configuresOnFormatChangeAndReenable()
    {
        DspRack rack;
        Gain gain(QStringLiteral("gain"), 1);
        rack.add(&gain, true);
        auto samples = stereo(8, 0, 0);
        rack.process(samples.data(), 8, 44100, 2);
        rack.process(samples.data(), 8, 44100, 2);
        QCOMPARE(gain.configures, 1);
        rack.process(samples.data(), 4, 48000, 2);
        QCOMPARE(gain.configures, 2);
        QCOMPARE(gain.configuredRate, 48000);

        rack.setEnabled(&gain, false);
        QVERIFY(!rack.isEnabled(&gain));
        rack.process(samples.data(), 4, 48000, 2);
        QCOMPARE(gain.processed, 20);
        rack.setEnabled(&gain, true);
        const int resets = gain.resets;
        rack.process(samples.data(), 4, 48000, 2);
        QCOMPARE(gain.configures, 3); // switched on again: starts clean
        QCOMPARE(gain.resets, resets + 1);

        rack.reset();
        QCOMPARE(gain.resets, resets + 2);
    }

#ifdef NEWAMP_TEST_EFFECTS
    void vocalRemoverCancelsTheCentre()
    {
        effects::VocalRemover remover;
        // A centred voice at 1 kHz all but vanishes...
        const auto centred = throughEffect(remover, 1000, 1, 1);
        QVERIFY2(rms(centred, 0) < 0.02f, qPrintable(QString::number(rms(centred, 0))));
        // ...a hard-panned guitar stays...
        const auto panned = throughEffect(remover, 1000, 1, 0);
        QVERIFY(rms(panned, 0) > 0.15f);
        // ...and so does centred bass.
        const auto bass = throughEffect(remover, 50, 1, 1);
        QVERIFY2(rms(bass, 0) > 0.25f, qPrintable(QString::number(rms(bass, 0))));
    }

    void widenerScalesTheSides()
    {
        QObject owner;
        effects::StereoWidener widener(&owner);
        QCOMPARE(widener.actions().size(), 4);
        widener.setWidth(1);
        const auto same = throughEffect(widener, 440, 1, 0.5f);
        QVERIFY(std::abs(rms(same, 0) - 0.3536f) < 0.01f);
        QVERIFY(std::abs(rms(same, 1) - 0.1768f) < 0.01f);
        widener.setWidth(0);
        const auto mono = throughEffect(widener, 440, 1, 0);
        QVERIFY(std::abs(rms(mono, 0) - rms(mono, 1)) < 0.001f);
    }

    void crossfeedBlendsButKeepsCentreLevel()
    {
        effects::Crossfeed crossfeed;
        const auto left = throughEffect(crossfeed, 200, 1, 0);
        QVERIFY(rms(left, 1) > 0.05f); // the right ear hears some of the left
        QVERIFY(rms(left, 1) < rms(left, 0));
        const auto centre = throughEffect(crossfeed, 200, 1, 1);
        QVERIFY2(std::abs(rms(centre, 0) - 0.3536f) < 0.03f, qPrintable(QString::number(rms(centre, 0))));
    }
#endif
};

QTEST_MAIN(TestDsp)
#include "tst_dsp.moc"

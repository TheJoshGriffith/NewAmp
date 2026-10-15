#include "skin/DefaultSkin.h"
#include "ui/VisualizerWindow.h"
#include "ui/vis/VisEffect.h"

#include <QStandardPaths>
#include <QTest>

#include <cmath>
#include <random>

using namespace newamp;

namespace {

constexpr int Rate = 44100;
constexpr float FrameTime = 1.0f / 30.0f;

// A 55 Hz kick every `period` seconds over quiet hi-hat noise, sampled into
// the window that ends at time t.
void kicks(float *out, double t, double period, float hats = 0.02f)
{
    static std::mt19937 rng(7);
    std::uniform_real_distribution<float> noise(-1, 1);
    for (int i = 0; i < VisFrame::WaveSize; ++i) {
        const double s = t - double(VisFrame::WaveSize - i) / Rate;
        const double sinceKick = std::fmod(s, period);
        out[i] = float(0.8 * std::exp(-sinceKick / 0.08) * std::sin(2 * M_PI * 55 * s)) + hats * noise(rng);
    }
}

// Runs the analyzer for `seconds` and returns the times of detected beats.
QList<double> beatTimes(double seconds, double period, float amplitude = 1.0f)
{
    VisAnalyzer analyzer;
    QList<double> beats;
    float samples[VisFrame::WaveSize];
    for (double t = FrameTime; t < seconds; t += FrameTime) {
        kicks(samples, t, period);
        for (float &s : samples)
            s *= amplitude;
        if (analyzer.process(samples, FrameTime).beat)
            beats << t;
    }
    return beats;
}

} // namespace

class TestVisualizer : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Keep the window's settings out of the real configuration.
        QStandardPaths::setTestModeEnabled(true);
    }

    void silenceHasNoBeats()
    {
        VisAnalyzer analyzer;
        float zeros[VisFrame::WaveSize] = {};
        for (int i = 0; i < 300; ++i) {
            QVERIFY(!analyzer.process(zeros, FrameTime).beat);
            QVERIFY(!analyzer.process(nullptr, FrameTime).beat);
        }
        QCOMPARE(analyzer.frame().bass, 0.0f);
        QVERIFY(analyzer.frame().silent);
    }

    void steadyToneHasNoBeats()
    {
        // A constant bass note is loud but has no pulses after its start.
        VisAnalyzer analyzer;
        float samples[VisFrame::WaveSize];
        int beats = 0;
        for (int frame = 0; frame < 300; ++frame) {
            const double t = frame * double(FrameTime);
            for (int i = 0; i < VisFrame::WaveSize; ++i)
                samples[i] = float(0.7 * std::sin(2 * M_PI * 60 * (t + double(i) / Rate)));
            beats += analyzer.process(samples, FrameTime).beat ? 1 : 0;
        }
        QVERIFY2(beats <= 1, qPrintable(QString::number(beats)));
        QVERIFY(analyzer.frame().bass > 0.5f);
    }

    void detectsPeriodicKicks_data()
    {
        QTest::addColumn<double>("period");
        QTest::addColumn<float>("amplitude");
        QTest::newRow("120 bpm") << 0.5 << 1.0f;
        QTest::newRow("90 bpm") << 2.0 / 3.0 << 1.0f;
        QTest::newRow("150 bpm, quiet") << 0.4 << 0.1f;
    }

    void detectsPeriodicKicks()
    {
        QFETCH(double, period);
        QFETCH(float, amplitude);
        const double seconds = 10.0;
        const QList<double> beats = beatTimes(seconds, period, amplitude);
        const int expected = int(seconds / period);
        QVERIFY2(qAbs(beats.size() - expected) <= 1,
                 qPrintable(QStringLiteral("%1 beats, expected %2").arg(beats.size()).arg(expected)));
        // Each beat lands within a frame or two of a kick.
        for (const double t : beats) {
            const double late = std::fmod(t, period);
            QVERIFY2(late < 3 * FrameTime, qPrintable(QStringLiteral("beat %1 s after a kick").arg(late)));
        }
    }

    void bandsFollowTheMusic()
    {
        VisAnalyzer analyzer;
        float samples[VisFrame::WaveSize];
        for (int frame = 0; frame < 30; ++frame) {
            for (int i = 0; i < VisFrame::WaveSize; ++i) {
                const double t = double(frame * 1470 + i) / Rate;
                samples[i] = 0;
                for (const double hz : {5000.0, 7000.0, 9500.0, 12500.0, 15000.0})
                    samples[i] += float(0.15 * std::sin(2 * M_PI * hz * t));
            }
            analyzer.process(samples, FrameTime);
        }
        // High whistles are treble, not bass.
        QVERIFY(analyzer.frame().treble > 0.3f);
        QVERIFY(analyzer.frame().bass < 0.1f);
    }

    void effectsRenderAtOddSizes()
    {
        const Skin skin = DefaultSkin::create();
        auto effects = createVisEffects();
        QVERIFY(effects.size() >= 6);
        for (const QSize content : {QSize(356, 227), QSize(1, 1), QSize(3, 97), QSize(257, 13), QSize(641, 479)}) {
            for (auto &effect : effects) {
                const QSize size = effect->frameSize(content);
                QVERIFY(size.width() >= 1 && size.height() >= 1);
                QVERIFY(size.width() <= content.width() && size.height() <= content.height());
                QImage image(size, QImage::Format_RGB32);
                image.fill(Qt::black);
                VisAnalyzer analyzer;
                float samples[VisFrame::WaveSize];
                for (int frame = 0; frame < 45; ++frame) {
                    const bool loud = frame >= 15;
                    if (loud)
                        kicks(samples, frame * double(FrameTime), 0.5, 0.3f);
                    VisFrame f = analyzer.process(loud ? samples : nullptr, FrameTime);
                    f.palette = &skin.visColors();
                    effect->render(image, f);
                    QCOMPARE(image.size(), size);
                }
                // Something visible after loud input (bigger frames only:
                // a 1x1 tunnel may well be its black centre).
                if (size.width() * size.height() >= 400) {
                    bool lit = false;
                    for (int y = 0; y < size.height() && !lit; ++y)
                        for (int x = 0; x < size.width() && !lit; ++x)
                            lit = (image.pixel(x, y) & 0xffffff) != (skin.visColors()[0].rgb() & 0xffffff)
                                && (image.pixel(x, y) & 0xffffff) != 0;
                    QVERIFY2(lit, qPrintable(effect->name()));
                }
            }
        }
    }

    void windowCyclesAndRenders()
    {
        const Skin skin = DefaultSkin::create();
        int calls = 0;
        VisualizerWindow window(&skin, [&calls](float *out, int count) {
            ++calls;
            kicks(out, calls * double(FrameTime), 0.5);
            return count;
        });
        QCOMPARE(window.skinSize(), VisualizerWindow::DefaultSize);
        QCOMPARE(window.windowTitle(), QStringLiteral("Visualization"));
        for (int i = 0; i < window.effectCount(); ++i) {
            window.setEffect(i);
            QCOMPARE(window.currentEffect(), i);
            for (int frame = 0; frame < 10; ++frame)
                window.tick();
            QVERIFY(!window.frame().isNull());
        }
        window.nextEffect();
        QCOMPARE(window.currentEffect(), 0);

        // Not shown, so playing alone doesn't start the animation.
        window.setPlaying(true);
        const int before = calls;
        QTest::qWait(100);
        QCOMPARE(calls, before);

        window.setRandomOnTrackChange(true);
        window.trackChanged();
        QVERIFY(window.currentEffect() != 0);
    }
};

QTEST_MAIN(TestVisualizer)
#include "tst_visualizer.moc"

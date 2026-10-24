#include "core/EqfFile.h"
#include "core/EqualizerSettings.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "skin/DefaultSkin.h"
#include "skin/SkinSprites.h"
#include "ui/EqualizerWindow.h"
#include "ui/MainWindow.h"
#include "ui/VisualizerWindow.h"
#include "ui/WindowManager.h"

#include <QApplication>
#include <QSettings>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QTest>

using namespace newamp;

class TestWindows : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void snapsToNeighbourEdges()
    {
        const QRect main(100, 100, 275, 116); // bottom edge at y = 215
        // 6px below the main window: docks flush underneath.
        QCOMPARE(WindowManager::snapOffset(QRect(100, 222, 275, 116), {main}, {}, 10), QPoint(0, -6));
        // 4px off to the right of it, vertically overlapping: docks to its right edge.
        QCOMPARE(WindowManager::snapOffset(QRect(379, 108, 275, 116), {main}, {}, 10), QPoint(-4, -8));
        // Too far away: no snap.
        QCOMPARE(WindowManager::snapOffset(QRect(100, 260, 275, 116), {main}, {}, 10), QPoint(0, 0));
    }

    void snapsInsideScreens()
    {
        const QRect screen(0, 0, 1920, 1080);
        QCOMPARE(WindowManager::snapOffset(QRect(7, 500, 275, 116), {}, {screen}, 10), QPoint(-7, 0));
        QCOMPARE(WindowManager::snapOffset(QRect(1640, 960, 275, 116), {}, {screen}, 10), QPoint(5, 4));
    }

    void prefersTheNearestEdge()
    {
        const QRect a(0, 0, 100, 100);
        const QRect b(0, 210, 100, 100);
        // Top is 2px below a's bottom; bottom is 7px above b's top. Nearest wins.
        const QRect moving(0, 102, 100, 100);
        QCOMPARE(WindowManager::snapOffset(moving, {a, b}, {}, 10).y(), -2);
    }

    void dockedGroupFollowsSharedEdges()
    {
        const QList<QRect> rects = {
            QRect(0, 0, 275, 116),     // 0 main
            QRect(0, 116, 275, 116),   // 1 below main
            QRect(0, 232, 275, 116),   // 2 below 1 (docked through 1)
            QRect(275, 0, 275, 116),   // 3 right of main
            QRect(600, 600, 275, 116), // 4 elsewhere
            QRect(0, 349, 275, 116),   // 5 one pixel gap below 2
        };
        const QSet<int> group = WindowManager::dockedGroup(0, rects);
        QCOMPARE(group, QSet<int>({0, 1, 2, 3}));
        QVERIFY(WindowManager::dockedGroup(4, rects) == QSet<int>({4}));
        QVERIFY(WindowManager::dockedGroup(99, rects).isEmpty());
    }

    void remembersWindowsOpenWhenQuitting()
    {
        const Skin skin = DefaultSkin::create();
        auto silence = [](float *, int) { return 0; };
        VisualizerWindow first(&skin, silence);
        VisualizerWindow open(&skin, silence);
        VisualizerWindow closed(&skin, silence);
        first.setObjectName(QStringLiteral("first"));
        open.setObjectName(QStringLiteral("open"));
        closed.setObjectName(QStringLiteral("closed"));
        WindowManager manager;
        manager.addWindow(&first);
        manager.addWindow(&open, false);
        manager.addWindow(&closed, false);
        first.show();
        open.show();

        // Quitting on macOS closes every window before the layout is saved.
        QEvent quit(QEvent::Quit);
        manager.eventFilter(QCoreApplication::instance(), &quit);
        first.hide();
        open.hide();

        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("layout.ini")), QSettings::IniFormat);
        manager.saveLayout(settings);
        QVERIFY(settings.value(QStringLiteral("windows/first/visible")).toBool());
        QVERIFY(settings.value(QStringLiteral("windows/open/visible")).toBool());
        QVERIFY(!settings.value(QStringLiteral("windows/closed/visible")).toBool());

        // And they open again next time.
        VisualizerWindow first2(&skin, silence);
        VisualizerWindow open2(&skin, silence);
        VisualizerWindow closed2(&skin, silence);
        first2.setObjectName(QStringLiteral("first"));
        open2.setObjectName(QStringLiteral("open"));
        closed2.setObjectName(QStringLiteral("closed"));
        WindowManager restored;
        restored.addWindow(&first2);
        restored.addWindow(&open2, false);
        restored.addWindow(&closed2, false);
        restored.restoreLayout(settings);
        QVERIFY(first2.isVisible());
        QVERIFY(open2.isVisible());
        QVERIFY(!closed2.isVisible());
    }

    void playerKeysWorkInEveryWindow()
    {
        Playlist playlist;
        PlaybackEngine engine;
        PlayerController controller(playlist, engine);
        WindowManager manager;
        manager.setController(&controller);
        MainWindow main(&manager.skin(), controller);
        EqualizerSettings settings;
        EqualizerWindow equalizer(&manager.skin(), settings);
        manager.addWindow(&main);
        manager.addWindow(&equalizer);

        engine.setVolume(0.5f);
        QSignalSpy volumes(&engine, &PlaybackEngine::volumeChanged);
        QTest::keyClick(&main, Qt::Key_Up);
        QCOMPARE(engine.volume(), 0.55f);
        QTest::keyClick(&equalizer, Qt::Key_Down);
        QTest::keyClick(&equalizer, Qt::Key_Down);
        QCOMPARE(engine.volume(), 0.45f);
        QCOMPARE(volumes.count(), 3);
        // Menu shortcuts aren't transport keys.
        QTest::keyClick(&main, Qt::Key_Up, Qt::ControlModifier);
        QCOMPARE(engine.volume(), 0.45f);
        // Nothing to play: space and the seek keys do nothing, safely.
        QTest::keyClick(&main, Qt::Key_Space);
        QTest::keyClick(&main, Qt::Key_Right);
        QCOMPARE(engine.state(), PlaybackEngine::State::Stopped);
    }

    void wheelChangesVolumeOnTheMainWindow()
    {
        Playlist playlist;
        PlaybackEngine engine;
        PlayerController controller(playlist, engine);
        WindowManager manager;
        manager.setController(&controller);
        MainWindow main(&manager.skin(), controller);
        manager.addWindow(&main);
        main.setScale(1.0);
        engine.setVolume(0.5f);

        auto turn = [&main](const QPoint &at, int delta) {
            QWheelEvent event(QPointF(at), QPointF(main.mapToGlobal(at)), {}, QPoint(0, delta), Qt::NoButton,
                              Qt::NoModifier, Qt::NoScrollPhase, false);
            QApplication::sendEvent(&main, &event);
        };
        // Over the volume slider, and over an empty part of the window.
        turn(sprites::VolumeBar.center(), 120);
        QCOMPARE(engine.volume(), 0.55f);
        turn(QPoint(250, 100), -240);
        QCOMPARE(engine.volume(), 0.45f);
    }

    void wheelMovesEqualizerSliders()
    {
        EqualizerSettings settings;
        WindowManager manager;
        EqualizerWindow equalizer(&manager.skin(), settings);
        manager.addWindow(&equalizer);
        const QPoint at = QRect(sprites::EqPreampPos, sprites::EqSliderSize).center();
        QWheelEvent event(QPointF(at), QPointF(at), {}, QPoint(0, 120), Qt::NoButton, Qt::NoModifier,
                          Qt::NoScrollPhase, false);
        QApplication::sendEvent(&equalizer, &event);
        QVERIFY(settings.preamp() > 0);
    }

    void equalizerClampsAndPersists()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("eq.ini")), QSettings::IniFormat);
        EqualizerSettings eq;
        eq.setEnabled(true);
        eq.setPreamp(30);
        eq.setBand(3, -5.5);
        eq.setBand(99, 4); // ignored
        QCOMPARE(eq.preamp(), 12.0);
        eq.save(settings);

        EqualizerSettings restored;
        restored.load(settings);
        QVERIFY(restored.isEnabled());
        QCOMPARE(restored.preamp(), 12.0);
        QCOMPARE(restored.band(3), -5.5);
        QCOMPARE(restored.band(0), 0.0);
    }

    void presetsAreInRange()
    {
        for (const auto &preset : EqualizerSettings::builtInPresets()) {
            QVERIFY(qAbs(preset.preamp) <= EqualizerSettings::MaxGain);
            for (double band : preset.bands)
                QVERIFY(qAbs(band) <= EqualizerSettings::MaxGain);
        }
    }

    void autoLoadsPresetsPerTrack()
    {
        EqualizerSettings eq;
        eq.setBand(0, 3);
        eq.setCurrentTrack(QStringLiteral("file:///a.mp3"));
        eq.setBand(0, 9);
        eq.saveAutoPreset(); // Auto is off: kept, but nothing changes yet
        eq.setBand(0, 3);

        eq.setAuto(true); // loads the playing track's preset
        QCOMPARE(eq.band(0), 9.0);
        eq.setCurrentTrack(QStringLiteral("file:///b.mp3")); // none: back to the user's own
        QCOMPARE(eq.band(0), 3.0);
        eq.setCurrentTrack(QStringLiteral("file:///a.mp3"));
        QCOMPARE(eq.band(0), 9.0);
        eq.setAuto(false);
        QCOMPARE(eq.band(0), 3.0);

        eq.setAuto(true);
        QCOMPARE(eq.band(0), 9.0);
        eq.deleteAutoPreset();
        QCOMPARE(eq.band(0), 3.0);
        QVERIFY(!eq.hasAutoPreset(QStringLiteral("file:///a.mp3")));
    }

    void savesUserAndAutoPresets()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("eq.ini")), QSettings::IniFormat);
        EqualizerSettings eq;
        eq.setBand(2, 4);
        eq.savePreset(eq.current(QStringLiteral("Mine")));
        eq.setBand(2, 5);
        eq.savePreset(eq.current(QStringLiteral("mine"))); // same name: replaced
        QCOMPARE(eq.userPresets().size(), 1);
        eq.setAuto(true);
        eq.setCurrentTrack(QStringLiteral("plex:track/1"));
        eq.setBand(2, -6);
        eq.saveAutoPreset();
        eq.setBand(2, -7); // an edit on top of the track's preset
        eq.save(settings);

        EqualizerSettings restored;
        restored.load(settings);
        QCOMPARE(restored.userPresets().size(), 1);
        QCOMPARE(restored.userPresets().first().bands[2], 5.0);
        // The track's settings aren't saved as the user's own.
        QCOMPARE(restored.band(2), 5.0);
        restored.setCurrentTrack(QStringLiteral("plex:track/1"));
        QCOMPARE(restored.band(2), -6.0);
        restored.deletePreset(QStringLiteral("MINE"));
        QVERIFY(restored.userPresets().isEmpty());
    }

    void readsAndWritesWinampPresetFiles()
    {
        // A .q1 library with two presets, as WinAmp writes it.
        QByteArray data = QByteArrayLiteral("Winamp EQ library file v1.1\x1a!--");
        auto entry = [&data](const char *name, const QList<int> &values) {
            QByteArray n(name);
            n.resize(257, '\0');
            data += n;
            for (int v : values)
                data += char(v);
        };
        entry("Rock", {0, 63, 31, 32, 16, 47, 31, 31, 31, 31, 31});
        entry("Flat", {31, 31, 31, 31, 31, 31, 31, 31, 31, 31, 32});
        const QList<EqualizerSettings::Preset> presets = eqf::read(data);
        QCOMPARE(presets.size(), 2);
        QCOMPARE(presets[0].name, QStringLiteral("Rock"));
        QCOMPARE(presets[0].bands[0], 12.0);
        QCOMPARE(presets[0].bands[1], -12.0);
        QCOMPARE(presets[0].bands[2], 0.0);
        QCOMPARE(presets[0].bands[3], 0.0);
        QCOMPARE(presets[0].bands[4], 5.9);
        QCOMPARE(presets[1].preamp, 0.0);

        // What NewAmp writes reads back to within one step.
        const EqualizerSettings::Preset mine{QStringLiteral("Mine"), -3, {7, 6, 4, 2, 0, 0, -1, -5, -12, 12}};
        const QList<EqualizerSettings::Preset> back = eqf::read(eqf::write({mine}));
        QCOMPARE(back.size(), 1);
        QCOMPARE(back[0].name, mine.name);
        QVERIFY(qAbs(back[0].preamp - mine.preamp) < 0.4);
        for (int i = 0; i < EqualizerSettings::BandCount; ++i)
            QVERIFY(qAbs(back[0].bands[i] - mine.bands[i]) < 0.4);

        QVERIFY(eqf::read(QByteArrayLiteral("not a preset")).isEmpty());
    }
};

QTEST_MAIN(TestWindows)
#include "tst_windows.moc"

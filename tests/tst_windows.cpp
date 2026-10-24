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
#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QMouseEvent>
#include <QSettings>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QTemporaryDir>
#include <QTest>

using namespace newamp;

namespace {

void writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(data);
}

QByteArray encode(const QImage &image, const char *format)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, format);
    return data;
}

// A .cur whose one image is stored as PNG, as newer cursor files do.
QByteArray pngCursor(const QPoint &hotspot)
{
    QImage image(16, 16, QImage::Format_ARGB32);
    image.fill(Qt::white);
    const QByteArray png = encode(image, "PNG");
    QByteArray file;
    auto le16 = [&file](int v) { file.append(char(v & 0xff)).append(char(v >> 8)); };
    le16(0);
    le16(2);
    le16(1);
    file.append(char(16)).append(char(16)).append(char(0)).append(char(0));
    le16(hotspot.x());
    le16(hotspot.y());
    le16(int(png.size()));
    le16(0);
    le16(22);
    le16(0);
    return file + png;
}

void moveMouse(QWidget &widget, const QPoint &at)
{
    QMouseEvent event(QEvent::MouseMove, QPointF(at), QPointF(widget.mapToGlobal(at)), Qt::NoButton,
                      Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&widget, &event);
}

} // namespace

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

    void skinShapesWindowsAndSetsCursors()
    {
        QTemporaryDir dir;
        QImage main(sprites::MainWindowSize, QImage::Format_RGB32);
        main.fill(Qt::red);
        writeFile(dir.filePath(QStringLiteral("main.bmp")), encode(main, "BMP"));
        // A triangle: the bottom right corner is cut away.
        writeFile(dir.filePath(QStringLiteral("region.txt")), "[Normal]\nNumPoints=3\nPointList=0,0 275,0 0,116\n");
        writeFile(dir.filePath(QStringLiteral("normal.cur")), pngCursor(QPoint(1, 2)));
        writeFile(dir.filePath(QStringLiteral("titlebar.cur")), pngCursor(QPoint(3, 4)));

        Playlist playlist;
        PlaybackEngine engine;
        PlayerController controller(playlist, engine);
        WindowManager manager;
        MainWindow window(&manager.skin(), controller);
        manager.addWindow(&window);
        window.setScale(1.0);
        QVERIFY(window.mask().isEmpty());
        QVERIFY(manager.loadSkin(dir.path()));
        QVERIFY(window.mask().contains(QPoint(5, 5)));
        QVERIFY(!window.mask().contains(QPoint(270, 110)));
        window.setScale(2.0);
        QVERIFY(window.mask().contains(QPoint(400, 10)));
        QVERIFY(!window.mask().contains(QPoint(540, 220)));
        window.setScale(1.0);

        moveMouse(window, QPoint(150, 100));
        QCOMPARE(window.cursor().shape(), Qt::BitmapCursor);
        QCOMPARE(window.cursor().hotSpot(), QPoint(1, 2));
        moveMouse(window, QPoint(150, 5));
        QCOMPARE(window.cursor().hotSpot(), QPoint(3, 4));

        // Back to a skin without shapes or cursors.
        manager.useDefaultSkin();
        QVERIFY(window.mask().isEmpty());
        QCOMPARE(window.cursor().shape(), Qt::ArrowCursor);
    }

    void clutterBarTogglesDoubleSizeAndAlwaysOnTop()
    {
        Playlist playlist;
        PlaybackEngine engine;
        PlayerController controller(playlist, engine);
        WindowManager manager;
        MainWindow window(&manager.skin(), controller);
        manager.addWindow(&window);
        window.setScale(1.0);

        QTest::mouseClick(&window, Qt::LeftButton, {}, sprites::clutterButtonArea(sprites::ClutterDoubleSize).center());
        QCOMPARE(window.scale(), 2.0);
        const QPoint d = sprites::clutterButtonArea(sprites::ClutterDoubleSize).center() * 2;
        QTest::mouseClick(&window, Qt::LeftButton, {}, d);
        QCOMPARE(window.scale(), 1.0);

        QSignalSpy onTop(&window, &MainWindow::alwaysOnTopToggled);
        QSignalSpy info(&window, &MainWindow::fileInfoRequested);
        QTest::mouseClick(&window, Qt::LeftButton, {}, sprites::clutterButtonArea(sprites::ClutterAlwaysOnTop).center());
        QTest::mouseClick(&window, Qt::LeftButton, {}, sprites::clutterButtonArea(sprites::ClutterFileInfo).center());
        QCOMPARE(onTop.count(), 1);
        QCOMPARE(onTop.first().first().toBool(), true);
        QCOMPARE(info.count(), 1);
    }

    void bitrateFitsThreeGlyphs()
    {
        QCOMPARE(MainWindow::kbpsText(0), QString());
        QCOMPARE(MainWindow::kbpsText(128), QStringLiteral("128"));
        QCOMPARE(MainWindow::kbpsText(1411), QStringLiteral("1K4"));
        QCOMPARE(MainWindow::kbpsText(9216), QStringLiteral("9K2"));
        QCOMPARE(MainWindow::kbpsText(12288), QStringLiteral("12K"));
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
};

QTEST_MAIN(TestWindows)
#include "tst_windows.moc"

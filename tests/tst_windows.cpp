#include "core/EqualizerSettings.h"
#include "ui/WindowManager.h"

#include <QSettings>
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

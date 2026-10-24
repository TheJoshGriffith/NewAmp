#include "core/LocalFileSource.h"
#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"
#include "platform/MediaControls.h"
#include "platform/SingleInstance.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSignalSpy>
#include <QTest>
#include <QUuid>

using namespace newamp;

namespace {

// Records what MediaControls tells the system.
class RecordingBackend final : public MediaControlsBackend
{
public:
    struct Log
    {
        QList<NowPlaying> updates;
        QList<NowPlaying> seeks;
    };
    explicit RecordingBackend(Log &log)
        : m_log(log)
    {
    }
    void update(const NowPlaying &now) override { m_log.updates << now; }
    void seeked(const NowPlaying &now) override { m_log.seeks << now; }

private:
    Log &m_log;
};

// On Windows a rooted path gains the current drive.
QString absolute(const QString &path)
{
    return QFileInfo(path).absoluteFilePath();
}

QString uniqueKey()
{
    return QStringLiteral("newamp-test-") + QUuid::createUuid().toString(QUuid::Id128).left(12);
}

} // namespace

class TestPlatform : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void secondLaunchHandsFilesToTheFirst()
    {
        const QString key = uniqueKey();
        SingleInstance first(key);
        QVERIFY(!first.sendToRunning({}, 200)); // nothing running yet
        QVERIFY(first.listen());
        QSignalSpy opened(&first, &SingleInstance::filesOpened);

        SingleInstance second(key);
        const QStringList arguments = {QStringLiteral("/music/a song.mp3"), QStringLiteral("file:///music/b.flac")};
        QVERIFY(second.sendToRunning(arguments));
        QVERIFY(opened.wait());
        QCOMPARE(opened.first().first().toStringList(),
                 QStringList({absolute(QStringLiteral("/music/a song.mp3")), absolute(QStringLiteral("/music/b.flac"))}));

        // Launched again without files: just come to the front.
        QVERIFY(second.sendToRunning({}));
        QVERIFY(opened.wait());
        QVERIFY(opened.last().first().toStringList().isEmpty());
    }

    void argumentsBecomeAbsolutePaths()
    {
        const QStringList paths = SingleInstance::pathsForArguments(
            {QStringLiteral("song.mp3"), QStringLiteral("--some-flag"), QStringLiteral("file:///tmp/x.ogg")});
        QCOMPARE(paths, QStringList({QDir::current().absoluteFilePath(QStringLiteral("song.mp3")),
                                     absolute(QStringLiteral("/tmp/x.ogg"))}));
    }

    void defaultKeyIsStable()
    {
        QCOMPARE(SingleInstance::defaultKey(), SingleInstance::defaultKey());
        QVERIFY(SingleInstance::defaultKey().startsWith(QLatin1String("newamp-")));
    }

    void appIconIsBuiltIn()
    {
        // The tray icon and Linux window icon.
        QVERIFY(QFile::exists(QStringLiteral(":/newamp/newamp.png")));
    }

    void mediaControlsFollowTheTrack()
    {
        Playlist playlist;
        PlaybackEngine engine;
        PlayerController controller(playlist, engine);
        RecordingBackend::Log log;
        MediaControls controls(controller, std::make_unique<RecordingBackend>(log));
        QVERIFY(controls.isAvailable());
        QVERIFY(!log.updates.isEmpty());
        QVERIFY(!log.updates.last().hasTrack);

        Track one = LocalFileSource::trackForFile(QStringLiteral("/music/one.mp3"));
        one.title = QStringLiteral("One");
        one.artist = QStringLiteral("Someone");
        one.durationMs = 180000;
        playlist.append({one, LocalFileSource::trackForFile(QStringLiteral("/music/two.mp3"))});
        const NowPlaying now = log.updates.last();
        QVERIFY(now.hasTrack);
        QCOMPARE(now.title, QStringLiteral("One"));
        QCOMPARE(now.artist, QStringLiteral("Someone"));
        QCOMPARE(now.durationMs, 180000);
        QVERIFY(now.canGoNext);
        QVERIFY(!now.canSeek); // stopped

        // Steady progress isn't passed on; a jump is.
        Q_EMIT engine.positionChanged(500);
        QVERIFY(log.seeks.isEmpty());
        Q_EMIT engine.positionChanged(60000);
        QCOMPARE(log.seeks.size(), 1);
    }

    void noBackendIsHarmless()
    {
        Playlist playlist;
        PlaybackEngine engine;
        PlayerController controller(playlist, engine);
        MediaControls controls(controller, std::unique_ptr<MediaControlsBackend>());
        QVERIFY(!controls.isAvailable());
        playlist.append({LocalFileSource::trackForFile(QStringLiteral("/music/one.mp3"))});
        Q_EMIT engine.positionChanged(60000);
    }
};

QTEST_MAIN(TestPlatform)
#include "tst_platform.moc"

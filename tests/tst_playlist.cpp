#include "core/LocalFileSource.h"
#include "core/Playlist.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

using namespace newamp;

class TestPlaylist : public QObject
{
    Q_OBJECT

private:
    static QList<Track> tracks(int n)
    {
        QList<Track> list;
        for (int i = 0; i < n; ++i)
            list << LocalFileSource::trackForFile(QStringLiteral("/music/%1.mp3").arg(i));
        return list;
    }

private Q_SLOTS:
    void appendSetsCurrentOnlyWhenEmpty()
    {
        Playlist playlist;
        QSignalSpy current(&playlist, &Playlist::currentIndexChanged);
        QCOMPARE(playlist.append(tracks(2)), 0);
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.append(tracks(3)), 2);
        QCOMPARE(playlist.currentIndex(), 0);
        QCOMPARE(playlist.count(), 5);
        QCOMPARE(current.count(), 1);
        QCOMPARE(playlist.append({}), -1);
    }

    void navigationStopsAtEnds()
    {
        Playlist playlist;
        playlist.append(tracks(2));
        QVERIFY(!playlist.previous());
        QVERIFY(playlist.next());
        QCOMPARE(playlist.currentIndex(), 1);
        QVERIFY(!playlist.next());
        QVERIFY(playlist.previous());
        QCOMPARE(playlist.currentIndex(), 0);
    }

    void clearResetsCursor()
    {
        Playlist playlist;
        playlist.append(tracks(3));
        playlist.clear();
        QVERIFY(playlist.isEmpty());
        QCOMPARE(playlist.currentIndex(), -1);
        QCOMPARE(playlist.currentTrack(), nullptr);
    }

    void displayTitle()
    {
        Track t = LocalFileSource::trackForFile(QStringLiteral("/music/Song Name.flac"));
        QCOMPARE(t.displayTitle(), QStringLiteral("Song Name"));
        t.artist = QStringLiteral("Artist");
        QCOMPARE(t.displayTitle(), QStringLiteral("Artist - Song Name"));
        QCOMPARE(t.sourceId, QStringLiteral("local"));
    }

    void folderExpansionFiltersAndSorts()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        for (const char *name : {"b.mp3", "a.FLAC", "notes.txt", "sub/c.ogg"}) {
            const QString path = dir.filePath(QString::fromLatin1(name));
            QDir().mkpath(QFileInfo(path).absolutePath());
            QFile f(path);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        const QList<Track> found = LocalFileSource::tracksForPaths({dir.path()});
        QCOMPARE(found.size(), 3);
        QCOMPARE(found.at(0).location.fileName(), QStringLiteral("a.FLAC"));
        QCOMPARE(found.at(1).location.fileName(), QStringLiteral("b.mp3"));
        QCOMPARE(found.at(2).location.fileName(), QStringLiteral("c.ogg"));
    }
};

QTEST_GUILESS_MAIN(TestPlaylist)
#include "tst_playlist.moc"

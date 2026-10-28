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

    void repeatWrapsBothWays()
    {
        Playlist playlist;
        playlist.append(tracks(3));
        playlist.setRepeat(true);
        QVERIFY(playlist.previous());
        QCOMPARE(playlist.currentIndex(), 2);
        QVERIFY(playlist.next());
        QCOMPARE(playlist.currentIndex(), 0);
    }

    void shuffleNeverRepeatsCurrentAndRemembersHistory()
    {
        Playlist playlist;
        playlist.append(tracks(5));
        playlist.setShuffle(true);
        QList<int> visited{playlist.currentIndex()};
        for (int i = 0; i < 20; ++i) {
            const int before = playlist.currentIndex();
            QVERIFY(playlist.next());
            QVERIFY(playlist.currentIndex() != before);
            visited << playlist.currentIndex();
        }
        // previous() retraces the shuffled order.
        for (int i = visited.size() - 2; i >= visited.size() - 5; --i) {
            QVERIFY(playlist.previous());
            QCOMPARE(playlist.currentIndex(), visited.at(i));
        }
    }

    void removeKeepsCurrentTrack()
    {
        Playlist playlist;
        playlist.append(tracks(5));
        playlist.setCurrentIndex(3);
        const QUrl current = playlist.currentTrack()->location;
        playlist.remove({0, 1, 4});
        QCOMPARE(playlist.count(), 2);
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(playlist.currentTrack()->location, current);
    }

    void removeCurrentMovesToNeighbour()
    {
        Playlist playlist;
        playlist.append(tracks(3));
        playlist.setCurrentIndex(2);
        playlist.remove({2, 2, 99});
        QCOMPARE(playlist.count(), 2);
        QCOMPARE(playlist.currentIndex(), 1);
        playlist.remove({0, 1});
        QCOMPARE(playlist.currentIndex(), -1);
    }

    void sortKeepsCurrentTrack()
    {
        Playlist playlist;
        playlist.append(tracks(3));
        playlist.setCurrentIndex(0);
        playlist.sortBy([](const Track &a, const Track &b) { return a.title > b.title; });
        QCOMPARE(playlist.at(0).title, QStringLiteral("2"));
        QCOMPARE(playlist.currentIndex(), 2);
    }

    static QStringList names(const Playlist &playlist)
    {
        QStringList list;
        for (const Track &t : playlist.tracks())
            list << t.title;
        return list;
    }

    void reverseAndRandomizeKeepCurrentAndQueue()
    {
        Playlist playlist;
        playlist.append(tracks(5));
        playlist.setCurrentIndex(1);
        playlist.toggleQueued({3});
        playlist.reverse();
        QCOMPARE(names(playlist), QStringList({"4", "3", "2", "1", "0"}));
        QCOMPARE(playlist.currentTrack()->title, QStringLiteral("1"));
        QCOMPARE(playlist.queue(), QList<int>{1});

        playlist.randomize();
        QCOMPARE(playlist.count(), 5);
        QCOMPARE(playlist.currentTrack()->title, QStringLiteral("1"));
        QCOMPARE(playlist.at(playlist.queue().first()).title, QStringLiteral("3"));
    }

    void cropKeepsOnlySelection()
    {
        Playlist playlist;
        playlist.append(tracks(5));
        playlist.setCurrentIndex(3);
        playlist.crop({1, 3});
        QCOMPARE(names(playlist), QStringList({"1", "3"}));
        QCOMPARE(playlist.currentIndex(), 1);
    }

    void removeDuplicatesKeepsFirst()
    {
        Playlist playlist;
        playlist.append(tracks(3));
        playlist.append(tracks(2));
        Track other = playlist.at(0);
        other.sourceId = QStringLiteral("plex"); // same URL, other source: not a duplicate
        playlist.append({other});
        QCOMPARE(playlist.removeDuplicates(), 2);
        QCOMPARE(names(playlist), QStringList({"0", "1", "2", "0"}));
    }

    void removeDeadFilesOnlyDropsMissingLocalFiles()
    {
        QTemporaryDir dir;
        QFile f(dir.filePath(QStringLiteral("here.mp3")));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.close();
        Playlist playlist;
        playlist.append(LocalFileSource::tracksForPaths({f.fileName()}));
        playlist.append({LocalFileSource::trackForFile(dir.filePath(QStringLiteral("gone.mp3")))});
        Track remote;
        remote.sourceId = QStringLiteral("plex");
        remote.location = QUrl(QStringLiteral("plex:track/1"));
        playlist.append({remote});
        QCOMPARE(playlist.removeDeadFiles(), 1);
        QCOMPARE(playlist.count(), 2);
        QCOMPARE(playlist.at(0).location.fileName(), QStringLiteral("here.mp3"));
    }

    void searchMatchesEveryWord()
    {
        Playlist playlist;
        QList<Track> list = tracks(3);
        list[0].artist = QStringLiteral("Boards of Canada");
        list[0].title = QStringLiteral("Roygbiv");
        list[1].artist = QStringLiteral("Aphex Twin");
        list[2].album = QStringLiteral("Music Has the Right to Children");
        playlist.append(list);
        QCOMPARE(playlist.search(QStringLiteral("canada ROY")), QList<int>{0});
        QCOMPARE(playlist.search(QStringLiteral("children")), QList<int>{2});
        QCOMPARE(playlist.search(QStringLiteral("  ")), QList<int>({0, 1, 2}));
        QCOMPARE(playlist.search(QStringLiteral("1.mp3")), QList<int>{1});
    }

    void queuePlaysFirstAndToggles()
    {
        Playlist playlist;
        playlist.append(tracks(5));
        QSignalSpy queueChanged(&playlist, &Playlist::queueChanged);
        playlist.toggleQueued({3, 1});
        QCOMPARE(playlist.queuePosition(3), 1);
        QCOMPARE(playlist.queuePosition(1), 2);
        QCOMPARE(playlist.queuePosition(0), 0);
        // Toggling a mix queues the rest; toggling all-queued rows dequeues.
        playlist.toggleQueued({1, 4});
        QCOMPARE(playlist.queue(), QList<int>({3, 1, 4}));
        playlist.toggleQueued({4});
        QCOMPARE(playlist.queue(), QList<int>({3, 1}));

        // Removing a row shifts the queue with it.
        playlist.remove({2});
        QCOMPARE(playlist.queue(), QList<int>({2, 1}));

        QVERIFY(playlist.next());
        QCOMPARE(playlist.currentTrack()->title, QStringLiteral("3"));
        QVERIFY(playlist.next());
        QCOMPARE(playlist.currentTrack()->title, QStringLiteral("1"));
        QVERIFY(playlist.queue().isEmpty());
        // Then back to normal order from there.
        QVERIFY(playlist.next());
        QCOMPARE(playlist.currentTrack()->title, QStringLiteral("3"));
        QVERIFY(queueChanged.count() >= 6);

        playlist.toggleQueued({0});
        playlist.clear();
        QVERIFY(playlist.queue().isEmpty());
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

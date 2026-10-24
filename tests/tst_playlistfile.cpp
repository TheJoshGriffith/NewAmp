#include "core/LocalFileSource.h"
#include "core/PlaylistFile.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

using namespace newamp;

namespace {

Track sourceTrack(const char *source, const char *location, const char *title)
{
    Track track;
    track.sourceId = QString::fromLatin1(source);
    track.location = QUrl(QString::fromLatin1(location));
    track.title = QString::fromLatin1(title);
    track.artist = QStringLiteral("Artist");
    track.album = QStringLiteral("Album");
    track.durationMs = 185000;
    return track;
}

bool writeFile(const QString &path, const QByteArray &data)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(data) == data.size();
}

} // namespace

class TestPlaylistFile : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesExtendedM3u()
    {
        const QUrl base = QUrl::fromLocalFile(QStringLiteral("/music/lists/"));
        const QByteArray data = "\xEF\xBB\xBF#EXTM3U\r\n"
                                "#EXTINF:123,Some Band - First Song\r\n"
                                "../albums/one.mp3\r\n"
                                "\r\n"
                                "# a comment\r\n"
                                "sub\\two.flac\r\n"
                                "/abs/three.ogg\r\n"
                                "file:///abs/four%20four.mp3\r\n"
                                "#EXTINF:-1 tvg-id=\"x\",Caf\xc3\xa9 Radio\r\n"
                                "http://radio.example/live\r\n"
                                "plex://abc/library/parts/1/file.flac?ratingKey=7\r\n";
        const QList<Track> tracks = PlaylistFile::parse(data, PlaylistFile::Format::M3U, base);
        QCOMPARE(tracks.size(), 6);

        QCOMPARE(tracks.at(0).location, QUrl::fromLocalFile(QStringLiteral("/music/albums/one.mp3")));
        QCOMPARE(tracks.at(0).sourceId, QStringLiteral("local"));
        QCOMPARE(tracks.at(0).artist, QStringLiteral("Some Band"));
        QCOMPARE(tracks.at(0).title, QStringLiteral("First Song"));
        QCOMPARE(tracks.at(0).durationMs, 123000);

        // Windows separators, absolute paths and file URLs.
        QCOMPARE(tracks.at(1).location, QUrl::fromLocalFile(QStringLiteral("/music/lists/sub/two.flac")));
        QCOMPARE(tracks.at(1).title, QStringLiteral("two"));
        QCOMPARE(tracks.at(2).location, QUrl::fromLocalFile(QStringLiteral("/abs/three.ogg")));
        QCOMPARE(tracks.at(3).location, QUrl::fromLocalFile(QStringLiteral("/abs/four four.mp3")));

        // A stream, with its title and an unknown length.
        QCOMPARE(tracks.at(4).sourceId, QStringLiteral("local"));
        QCOMPARE(tracks.at(4).location, QUrl(QStringLiteral("http://radio.example/live")));
        QCOMPARE(tracks.at(4).title, QStringLiteral("Café Radio"));
        QCOMPARE(tracks.at(4).durationMs, -1);

        // A plugin's location keeps its source, loaded or not.
        QCOMPARE(tracks.at(5).sourceId, QStringLiteral("plex"));
        QCOMPARE(tracks.at(5).location.toString(), QStringLiteral("plex://abc/library/parts/1/file.flac?ratingKey=7"));
    }

    void readsLatin1M3u()
    {
        const QList<Track> tracks = PlaylistFile::parse("#EXTINF:10,Caf\xe9\n/a.mp3\n", PlaylistFile::Format::M3U,
                                                        QUrl::fromLocalFile(QStringLiteral("/")));
        QCOMPARE(tracks.size(), 1);
        QCOMPARE(tracks.at(0).title, QStringLiteral("Café"));
    }

    void parsesPls()
    {
        const QByteArray data = "[playlist]\n"
                                "File2=http://radio.example/backup\n"
                                "Title2=Backup\n"
                                "file1=song.mp3\n"
                                "Title1=Band - Song\n"
                                "Length1=61\n"
                                "Length2=-1\n"
                                "NumberOfEntries=2\n"
                                "Version=2\n";
        const QList<Track> tracks =
            PlaylistFile::parse(data, PlaylistFile::Format::Unknown, QUrl::fromLocalFile(QStringLiteral("/m/")));
        QCOMPARE(tracks.size(), 2);
        QCOMPARE(tracks.at(0).location, QUrl::fromLocalFile(QStringLiteral("/m/song.mp3")));
        QCOMPARE(tracks.at(0).artist, QStringLiteral("Band"));
        QCOMPARE(tracks.at(0).title, QStringLiteral("Song"));
        QCOMPARE(tracks.at(0).durationMs, 61000);
        QCOMPARE(tracks.at(1).location, QUrl(QStringLiteral("http://radio.example/backup")));
        QCOMPARE(tracks.at(1).title, QStringLiteral("Backup"));
        QCOMPARE(tracks.at(1).durationMs, -1);
    }

    void nativeKeepsEverySource()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Track local = LocalFileSource::trackForFile(dir.filePath(QStringLiteral("albums/one.flac")));
        local.title = QStringLiteral("One");
        const QList<Track> tracks = {
            local,
            LocalFileSource::trackForFile(QStringLiteral("/elsewhere/two.mp3")),
            sourceTrack("plex", "plex://abc/library/parts/1/file.flac?ratingKey=7", "From Plex"),
            sourceTrack("jellyfin", "jellyfin://server/Items/42", "From Jellyfin"),
            sourceTrack("dlna", "dlna:track?udn=uuid:1&res=http%3A%2F%2F10.0.0.2%2Fa.mp3", "From DLNA"),
            sourceTrack("spotify", "spotify:track:4uLU6hMCjMI75M1A2tKUQC", "From Spotify"),
            sourceTrack("local", "http://radio.example/live", "Radio"),
        };
        const QString path = dir.filePath(QStringLiteral("mix.nap"));
        int skipped = -1;
        QString error;
        QVERIFY2(PlaylistFile::save(path, tracks, &error, &skipped), qPrintable(error));
        QCOMPARE(skipped, 0);

        // Files beside the playlist are stored relative to it.
        QFile file(path);
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QByteArray saved = file.readAll();
        QVERIFY2(saved.contains(QDir::toNativeSeparators(QStringLiteral("albums/one.flac")).toUtf8().replace("\\", "\\\\")),
                 saved.constData());
        QVERIFY(!saved.contains(dir.path().toUtf8()));

        const QList<Track> loaded = PlaylistFile::read(path, &error);
        QVERIFY(error.isEmpty());
        QCOMPARE(loaded.size(), tracks.size());
        for (int i = 0; i < tracks.size(); ++i) {
            QCOMPARE(loaded.at(i).sourceId, tracks.at(i).sourceId);
            QCOMPARE(loaded.at(i).location, tracks.at(i).location);
            QCOMPARE(loaded.at(i).title, tracks.at(i).title);
            QCOMPARE(loaded.at(i).artist, tracks.at(i).artist);
            QCOMPARE(loaded.at(i).durationMs, tracks.at(i).durationMs);
        }

        // The folder can move with its playlist.
        const QString moved = dir.filePath(QStringLiteral("moved"));
        QVERIFY(QDir().mkpath(moved + QStringLiteral("/albums")));
        QVERIFY(QFile::copy(path, moved + QStringLiteral("/mix.nap")));
        QCOMPARE(PlaylistFile::read(moved + QStringLiteral("/mix.nap")).first().location,
                 QUrl::fromLocalFile(moved + QStringLiteral("/albums/one.flac")));
    }

    void m3uAndPlsLeaveOutPluginTracks()
    {
        const QList<Track> tracks = {
            sourceTrack("local", "file:///music/a.mp3", "A"),
            sourceTrack("plex", "plex://abc/library/parts/1/file.flac", "From Plex"),
            sourceTrack("local", "http://radio.example/live", "Radio"),
        };
        int skipped = -1;
        const QByteArray m3u = PlaylistFile::write(tracks, PlaylistFile::Format::M3U, QStringLiteral("/music"), &skipped);
        QCOMPARE(skipped, 1);
        QCOMPARE(m3u, QByteArray("#EXTM3U\n#EXTINF:185,Artist - A\na.mp3\n"
                                 "#EXTINF:185,Artist - Radio\nhttp://radio.example/live\n"));

        const QByteArray pls = PlaylistFile::write(tracks, PlaylistFile::Format::PLS, QStringLiteral("/other"), &skipped);
        QCOMPARE(skipped, 1);
        QVERIFY2(pls.contains(QDir::toNativeSeparators(QStringLiteral("File1=/music/a.mp3")).toUtf8()), pls.constData());
        QVERIFY(pls.contains("NumberOfEntries=2"));

        // And they read back as they were.
        const QList<Track> back = PlaylistFile::parse(m3u, PlaylistFile::Format::M3U,
                                                      QUrl::fromLocalFile(QStringLiteral("/music/")));
        QCOMPARE(back.size(), 2);
        QCOMPARE(back.at(0).location, tracks.at(0).location);
        QCOMPARE(back.at(0).title, QStringLiteral("A"));
        QCOMPARE(back.at(0).artist, QStringLiteral("Artist"));
        QCOMPARE(back.at(1).location, tracks.at(2).location);
    }

    void droppedPlaylistsExpand()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath(QStringLiteral("list.m3u")), "a.mp3\nb.flac\n"));
        QVERIFY(writeFile(dir.filePath(QStringLiteral("c.mp3")), ""));
        const QList<Track> tracks =
            LocalFileSource::tracksForPaths({dir.filePath(QStringLiteral("list.m3u")), dir.filePath(QStringLiteral("c.mp3"))});
        QCOMPARE(tracks.size(), 3);
        QCOMPARE(tracks.at(1).location, QUrl::fromLocalFile(dir.filePath(QStringLiteral("b.flac"))));

        // A folder's playlists aren't opened: its files already are.
        QCOMPARE(LocalFileSource::tracksForPaths({dir.path()}).size(), 1);
    }

    void rejectsBrokenNativeFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("broken.nap"));
        QVERIFY(writeFile(path, "not json"));
        QString error;
        QVERIFY(PlaylistFile::read(path, &error).isEmpty());
        QVERIFY(!error.isEmpty());
        QVERIFY(!PlaylistFile::save(dir.filePath(QStringLiteral("list.txt")), {}, &error));
    }
};

QTEST_GUILESS_MAIN(TestPlaylistFile)
#include "tst_playlistfile.moc"

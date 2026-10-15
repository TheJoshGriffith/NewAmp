#include "PlexApi.h"
#include "PlexReporter.h"

#include <QFile>
#include <QTest>
#include <QUrlQuery>

using namespace newamp;
using namespace newamp::plex;

namespace {
const QString ServerId = QStringLiteral("0123456789abcdef0123456789abcdef01234567");

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(NEWAMP_PLEX_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

Track plexTrack(const QString &ratingKey, qint64 durationMs)
{
    Track track;
    track.sourceId = QStringLiteral("plex");
    track.location = trackLocation({ServerId, QStringLiteral("/library/parts/1/2/file.flac"), ratingKey});
    track.durationMs = durationMs;
    return track;
}
} // namespace

class TestPlex : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void parsesPins()
    {
        const Pin pending = parsePin(fixture("pin.json"));
        QCOMPARE(pending.id, qint64(1234567));
        QCOMPARE(pending.code, QStringLiteral("abcd1234efgh"));
        QVERIFY(pending.authToken.isEmpty());

        const Pin claimed = parsePin(fixture("pin_claimed.json"));
        QCOMPARE(claimed.authToken, QStringLiteral("user-token-xyz"));

        QCOMPARE(parsePin("not json").id, qint64(0));
        QCOMPARE(parsePin(R"({"errors":[{"code":1020}]})").id, qint64(0));
    }

    void buildsAuthUrl()
    {
        QCOMPARE(authUrl(QStringLiteral("client-1"), QStringLiteral("abcd")).toString(QUrl::FullyEncoded),
                 QStringLiteral("https://app.plex.tv/auth#?clientID=client-1&code=abcd"
                                "&context%5Bdevice%5D%5Bproduct%5D=NewAmp"));
    }

    void keepsOnlyServers()
    {
        const QList<Server> servers = parseResources(fixture("resources.json"));
        // The player is skipped, and so is the server without connections.
        QCOMPARE(servers.size(), 2);
        QCOMPARE(servers[0].name, QStringLiteral("Home Server"));
        QCOMPARE(servers[0].machineIdentifier, ServerId);
        QCOMPARE(servers[0].accessToken, QStringLiteral("server-token"));
        QCOMPARE(servers[0].connections.size(), 4);
        QCOMPARE(servers[1].accessToken, QStringLiteral("shared-token"));
        QVERIFY(parseResources("{}").isEmpty());
    }

    void ordersConnections()
    {
        const QList<Server> servers = parseResources(fixture("resources.json"));
        QStringList uris;
        for (const Connection &c : orderedConnections(servers[0]))
            uris << c.uri;
        QCOMPARE(uris, QStringList({
                           QStringLiteral("https://192-168-1-10.abc.plex.direct:32400"), // local https
                           QStringLiteral("http://192.168.1.10:32400"),                  // local http
                           QStringLiteral("https://203-0-113-5.abc.plex.direct:32400"),  // remote
                           QStringLiteral("https://203-0-113-5.abc.plex.direct:8443"),   // relay
                       }));
        const QList<Connection> relayOnly = orderedConnections(servers[1]);
        QCOMPARE(relayOnly.size(), 1);
        QVERIFY(relayOnly.first().relay);
    }

    void parsesMusicSections()
    {
        const QList<Directory> sections = parseSections(fixture("sections.json"));
        QCOMPARE(sections.size(), 2); // the movie library is skipped
        QCOMPARE(sections[0].key, QStringLiteral("3"));
        QCOMPARE(sections[0].title, QStringLiteral("Music"));
        QCOMPARE(sections[1].title, QStringLiteral("Audiobooks"));
    }

    void parsesArtistsAndAlbums()
    {
        const QList<Directory> artists = parseDirectories(fixture("artists.json"));
        QCOMPARE(artists.size(), 2);
        QCOMPARE(artists[0].key, QStringLiteral("100"));
        QCOMPARE(artists[0].title, QStringLiteral("Aphex Twin"));
        QVERIFY(artists[0].subtitle.isEmpty());

        const QList<Directory> albums = parseDirectories(fixture("albums.json"));
        QCOMPARE(albums.size(), 2);
        QCOMPARE(albums[0].key, QStringLiteral("101"));
        QCOMPARE(albums[0].subtitle, QStringLiteral("1992"));
        QVERIFY(albums[1].subtitle.isEmpty());
    }

    void parsesTracks()
    {
        const QList<Track> tracks = parseTracks(fixture("tracks.json"), ServerId);
        QCOMPARE(tracks.size(), 2); // the entry without media is skipped

        const Track &first = tracks[0];
        QCOMPARE(first.sourceId, QStringLiteral("plex"));
        QCOMPARE(first.title, QStringLiteral("Xtal"));
        QCOMPARE(first.artist, QStringLiteral("Aphex Twin"));
        QCOMPARE(first.album, QStringLiteral("Selected Ambient Works 85-92"));
        QCOMPARE(first.durationMs, qint64(293000));
        QCOMPARE(first.location.toString(),
                 QStringLiteral("plex://%1/library/parts/6001/1600000000/file.flac?ratingKey=1001").arg(ServerId));

        // Track artist wins over the album artist; numeric keys and missing
        // durations are handled.
        const Track &second = tracks[1];
        QCOMPARE(second.artist, QStringLiteral("Aphex Twin"));
        QCOMPARE(second.durationMs, qint64(-1));
        QCOMPARE(parseTrackLocation(second.location).ratingKey, QStringLiteral("1002"));
    }

    void parsesPlaylists()
    {
        const QList<Directory> playlists = parseDirectories(fixture("playlists.json"));
        QCOMPARE(playlists.size(), 2);
        QCOMPARE(playlists[0].key, QStringLiteral("500"));
        QCOMPARE(playlists[0].title, QStringLiteral("Morning"));
        QCOMPARE(playlists[0].subtitle, QStringLiteral("42 tracks"));

        const QList<Track> items = parseTracks(fixture("playlist_items.json"), ServerId);
        QCOMPARE(items.size(), 1);
        QCOMPARE(items[0].title, QStringLiteral("Roygbiv"));
        QCOMPARE(items[0].artist, QStringLiteral("Boards of Canada"));
    }

    void roundTripsTrackLocation()
    {
        const TrackRef ref{ServerId, QStringLiteral("/library/parts/6001/1600000000/file.flac"), QStringLiteral("1001")};
        const TrackRef parsed = parseTrackLocation(trackLocation(ref));
        QCOMPARE(parsed.machineIdentifier, ref.machineIdentifier);
        QCOMPARE(parsed.partKey, ref.partKey);
        QCOMPARE(parsed.ratingKey, ref.ratingKey);
        QVERIFY(!parseTrackLocation(QUrl::fromLocalFile(QStringLiteral("/tmp/a.mp3"))).isValid());
    }

    void roundTripsFolderIds()
    {
        const QString id = folderId(QStringLiteral("abc123"), FolderKind::Album, QStringLiteral("42"));
        QString server, key;
        FolderKind kind = FolderKind::Server;
        QVERIFY(parseFolderId(id, &server, &kind, &key));
        QCOMPARE(server, QStringLiteral("abc123"));
        QCOMPARE(kind, FolderKind::Album);
        QCOMPARE(key, QStringLiteral("42"));
        QVERIFY(parseFolderId(folderId(QStringLiteral("abc123"), FolderKind::Playlists), &server, &kind, &key));
        QCOMPARE(kind, FolderKind::Playlists);
        QVERIFY(key.isEmpty());
        QVERIFY(!parseFolderId(QStringLiteral("abc123|nonsense|1"), &server, &kind, &key));
        QVERIFY(!parseFolderId(QString(), &server, &kind, &key));
    }

    void buildsStreamUrl()
    {
        const QUrl url = streamUrl(QStringLiteral("https://192-168-1-10.abc.plex.direct:32400/"),
                                   QStringLiteral("/library/parts/6001/1600000000/file.flac"),
                                   QStringLiteral("server-token"));
        QCOMPARE(url.toString(), QStringLiteral("https://192-168-1-10.abc.plex.direct:32400"
                                                "/library/parts/6001/1600000000/file.flac?X-Plex-Token=server-token"));
    }

    void buildsTranscodeUrl()
    {
        const QUrl url = transcodeUrl(QStringLiteral("https://192-168-1-10.abc.plex.direct:32400/"),
                                      QStringLiteral("5001"), QStringLiteral("server-token"),
                                      QStringLiteral("client-1"), QStringLiteral("session-1"));
        QCOMPARE(url.path(), QStringLiteral("/music/:/transcode/universal/start.m3u8"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("path"), QUrl::FullyDecoded),
                 QStringLiteral("/library/metadata/5001"));
        QCOMPARE(query.queryItemValue(QStringLiteral("protocol")), QStringLiteral("hls"));
        QCOMPARE(query.queryItemValue(QStringLiteral("session")), QStringLiteral("session-1"));
        QCOMPARE(query.queryItemValue(QStringLiteral("X-Plex-Client-Identifier")), QStringLiteral("client-1"));
        QCOMPARE(query.queryItemValue(QStringLiteral("X-Plex-Token")), QStringLiteral("server-token"));
        QVERIFY(url.toString(QUrl::FullyEncoded).contains(QLatin1String("path=%2Flibrary%2Fmetadata%2F5001")));
    }

    void buildsReportPaths()
    {
        const QString timeline = timelinePath(QStringLiteral("1001"), QStringLiteral("paused"), 5000, 293000);
        QVERIFY(timeline.startsWith(QStringLiteral("/:/timeline?")));
        const QUrlQuery query(QUrl(timeline).query());
        QCOMPARE(query.queryItemValue(QStringLiteral("ratingKey")), QStringLiteral("1001"));
        QCOMPARE(query.queryItemValue(QStringLiteral("key")), QStringLiteral("/library/metadata/1001"));
        QCOMPARE(query.queryItemValue(QStringLiteral("state")), QStringLiteral("paused"));
        QCOMPARE(query.queryItemValue(QStringLiteral("time")), QStringLiteral("5000"));
        QCOMPARE(query.queryItemValue(QStringLiteral("duration")), QStringLiteral("293000"));

        QCOMPARE(scrobblePath(QStringLiteral("1001")),
                 QStringLiteral("/:/scrobble?key=1001&identifier=com.plexapp.plugins.library"));
    }

    void reportsPlayback()
    {
        QStringList sent;
        PlexReporter reporter([&sent](const QString &server, const QString &path) {
            QCOMPARE(server, ServerId);
            sent << path.section(QLatin1Char('&'), 0, 0) + QLatin1Char(' ')
                        + QUrlQuery(QUrl(path).query()).queryItemValue(QStringLiteral("state"));
        });
        using State = PlaybackObserver::State;

        // Local tracks are ignored.
        Track local;
        local.sourceId = QStringLiteral("local");
        reporter.trackStarted(local);
        reporter.playbackStateChanged(State::Playing);
        QVERIFY(sent.isEmpty());

        reporter.trackStarted(plexTrack(QStringLiteral("1001"), 100000));
        reporter.playbackStateChanged(State::Playing); // already reported
        reporter.positionChanged(50000, 100000);
        reporter.playbackStateChanged(State::Paused);
        reporter.playbackStateChanged(State::Playing);
        reporter.positionChanged(91000, 100000);       // past 90%: scrobble once
        reporter.positionChanged(95000, 100000);
        reporter.trackFinished(plexTrack(QStringLiteral("1001"), 100000));
        reporter.playbackStateChanged(State::Stopped); // already stopped

        QCOMPARE(sent, QStringList({
                           QStringLiteral("/:/timeline?ratingKey=1001 playing"),
                           QStringLiteral("/:/timeline?ratingKey=1001 paused"),
                           QStringLiteral("/:/timeline?ratingKey=1001 playing"),
                           QStringLiteral("/:/scrobble?key=1001 "),
                           QStringLiteral("/:/timeline?ratingKey=1001 stopped"),
                       }));

        // Switching tracks mid-play stops the old one; finishing scrobbles.
        sent.clear();
        reporter.trackStarted(plexTrack(QStringLiteral("1"), 100000));
        reporter.trackStarted(plexTrack(QStringLiteral("2"), 100000));
        reporter.trackFinished(plexTrack(QStringLiteral("2"), 100000));
        QCOMPARE(sent, QStringList({
                           QStringLiteral("/:/timeline?ratingKey=1 playing"),
                           QStringLiteral("/:/timeline?ratingKey=1 stopped"),
                           QStringLiteral("/:/timeline?ratingKey=2 playing"),
                           QStringLiteral("/:/scrobble?key=2 "),
                           QStringLiteral("/:/timeline?ratingKey=2 stopped"),
                       }));
    }
};

QTEST_GUILESS_MAIN(TestPlex)
#include "tst_plex.moc"

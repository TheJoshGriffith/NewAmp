#include "SpotifyApi.h"
#include "SpotifyClient.h"
#include "SpotifyConnect.h"
#include "SpotifyWebPlayer.h"
#include "core/PlaybackEngine.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QSysInfo>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

using namespace newamp;
using namespace newamp::spotify;

namespace {
QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(NEWAMP_SPOTIFY_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

// Just enough of the Web API and one Connect device to play on.
class FakeSpotify : public QTcpServer
{
public:
    QList<Device> devices;
    QString deviceId; // where it plays; empty when idle
    QString item;     // the track URI playing
    QString context;
    bool playing = false;
    bool deaf = false;   // accepts play commands but never plays
    bool sleepy = false; // ignores play until playback is transferred to it
    QStringList log; // "PUT /v1/me/player/play?device_id=x {...}"

    FakeSpotify()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray data = socket->peek(65536);
                    const qsizetype end = data.indexOf("\r\n\r\n");
                    if (end < 0)
                        return;
                    const QByteArray head = data.left(end);
                    qsizetype length = 0;
                    for (const QByteArray &line : head.split('\n')) {
                        if (line.toLower().startsWith("content-length:"))
                            length = line.mid(15).trimmed().toLongLong();
                    }
                    if (data.size() < end + 4 + length)
                        return;
                    socket->read(end + 4 + length);
                    const QList<QByteArray> request = head.left(head.indexOf("\r\n")).split(' ');
                    const auto [status, body] = answer(QString::fromLatin1(request.value(0)),
                                                       QUrl(QString::fromLatin1(request.value(1))),
                                                       data.mid(end + 4, length));
                    socket->write("HTTP/1.1 " + QByteArray::number(status) + " X\r\nContent-Length: "
                                  + QByteArray::number(body.size())
                                  + "\r\nContent-Type: application/json\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
            }
        });
    }

    QString url(const char *path) const
    {
        return QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(QLatin1String(path));
    }

private:
    std::pair<int, QByteArray> answer(const QString &verb, const QUrl &url, const QByteArray &body)
    {
        const QString path = url.path();
        const QString device = QUrlQuery(url).queryItemValue(QStringLiteral("device_id"));
        log.append(verb + QLatin1Char(' ') + url.toString() + (body.isEmpty() ? QString() : QLatin1Char(' ') + QString::fromUtf8(body)));
        const QByteArray noDevice = R"({"error":{"status":404,"message":"Device not found","reason":"NO_ACTIVE_DEVICE"}})";
        if (path == QLatin1String("/token"))
            return {200, R"({"access_token":"a","token_type":"Bearer","expires_in":3600})"};
        if (path == QLatin1String("/v1/me/player/devices")) {
            QJsonArray list;
            for (const Device &d : devices)
                list.append(QJsonObject{{QStringLiteral("id"), d.id}, {QStringLiteral("name"), d.name},
                                        {QStringLiteral("type"), d.type}, {QStringLiteral("is_active"), d.active},
                                        {QStringLiteral("is_restricted"), d.restricted}});
            return {200, QJsonDocument(QJsonObject{{QStringLiteral("devices"), list}}).toJson()};
        }
        if (path == QLatin1String("/v1/me/player/play")) {
            const bool known = std::any_of(devices.cbegin(), devices.cend(), [&](const Device &d) { return d.id == device; });
            if (!known)
                return {404, noDevice};
            if (deaf || sleepy) {
                deviceId = device; // what Josh's app reported: active, idle, no item
                return {204, {}};
            }
            const QJsonObject o = QJsonDocument::fromJson(body).object();
            if (o.contains(QStringLiteral("uris"))) {
                item = o.value(QStringLiteral("uris")).toArray().first().toString();
                context.clear();
            } else if (o.contains(QStringLiteral("context_uri"))) {
                context = o.value(QStringLiteral("context_uri")).toString();
                item = QStringLiteral("spotify:track:first-of-playlist");
            }
            deviceId = device;
            playing = !item.isEmpty();
            return {204, {}};
        }
        if (path == QLatin1String("/v1/me/player") && verb == QLatin1String("PUT")) {
            sleepy = false; // transferring playback wakes the device
            return {204, {}};
        }
        if (path == QLatin1String("/v1/me/player/pause")) {
            playing = false;
            return {204, {}};
        }
        if (path.startsWith(QLatin1String("/v1/me/player/")))
            return {204, {}}; // volume, seek
        if (path == QLatin1String("/v1/me/player")) {
            if (deviceId.isEmpty())
                return {204, {}};
            if (item.isEmpty())
                return {200, QJsonDocument(QJsonObject{{QStringLiteral("is_playing"), false},
                                                       {QStringLiteral("progress_ms"), 0},
                                                       {QStringLiteral("device"), QJsonObject{{QStringLiteral("id"), deviceId}}}})
                                  .toJson()};
            QJsonObject o{{QStringLiteral("is_playing"), playing},
                          {QStringLiteral("progress_ms"), 1000},
                          {QStringLiteral("device"), QJsonObject{{QStringLiteral("id"), deviceId}}},
                          {QStringLiteral("item"),
                           QJsonObject{{QStringLiteral("id"), item.section(QLatin1Char(':'), 2)},
                                       {QStringLiteral("name"), QStringLiteral("Song")},
                                       {QStringLiteral("type"), QStringLiteral("track")},
                                       {QStringLiteral("duration_ms"), 200000},
                                       {QStringLiteral("artists"), QJsonArray{QJsonObject{{QStringLiteral("name"), QStringLiteral("Band")}}}}}}};
            if (!context.isEmpty())
                o.insert(QStringLiteral("context"), QJsonObject{{QStringLiteral("uri"), context}});
            return {200, QJsonDocument(o).toJson()};
        }
        return {404, R"({"error":{"status":404,"message":"no such path"}})"};
    }
};
} // namespace

class TestSpotify : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void makesPkceChallenges()
    {
        const QByteArray verifier = makeCodeVerifier();
        QCOMPARE(verifier.size(), 64);
        QVERIFY(verifier != makeCodeVerifier());
        // The example from RFC 7636, appendix B.
        QCOMPARE(codeChallenge("dBjftJeZ4CVP-mB92K27uhbUJU1p1r_wW1gFWFOEjXk"),
                 QByteArray("E9Melhoa2OwvFrEMTJguCHaoeK1t8URWbuGJSstw-cM"));
    }

    void buildsAuthorizeUrl()
    {
        const QUrl url = authorizeUrl(QStringLiteral("client-1"), "challenge", QStringLiteral("state-1"));
        QCOMPARE(url.host(), QStringLiteral("accounts.spotify.com"));
        QCOMPARE(url.path(), QStringLiteral("/authorize"));
        const QUrlQuery query(url);
        QCOMPARE(query.queryItemValue(QStringLiteral("client_id")), QStringLiteral("client-1"));
        QCOMPARE(query.queryItemValue(QStringLiteral("code_challenge_method")), QStringLiteral("S256"));
        QCOMPARE(query.queryItemValue(QStringLiteral("redirect_uri"), QUrl::FullyDecoded),
                 QStringLiteral("http://127.0.0.1:43827/callback"));
        QVERIFY(query.queryItemValue(QStringLiteral("scope"), QUrl::FullyDecoded)
                    .contains(QLatin1String("user-modify-playback-state")));
        QCOMPARE(query.queryItemValue(QStringLiteral("state")), QStringLiteral("state-1"));
    }

    void buildsTokenRequests()
    {
        const QUrlQuery body(QString::fromLatin1(tokenRequestBody(QStringLiteral("c"), QStringLiteral("a+b"), "v")));
        QCOMPARE(body.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("authorization_code"));
        QCOMPARE(body.queryItemValue(QStringLiteral("code"), QUrl::FullyDecoded), QStringLiteral("a+b"));
        QCOMPARE(body.queryItemValue(QStringLiteral("code_verifier")), QStringLiteral("v"));
        const QUrlQuery refresh(QString::fromLatin1(refreshRequestBody(QStringLiteral("c"), QStringLiteral("r"))));
        QCOMPARE(refresh.queryItemValue(QStringLiteral("grant_type")), QStringLiteral("refresh_token"));
        QCOMPARE(refresh.queryItemValue(QStringLiteral("refresh_token")), QStringLiteral("r"));
    }

    void parsesTokensAndErrors()
    {
        const Tokens tokens = parseTokens(fixture("token.json"));
        QCOMPARE(tokens.accessToken, QStringLiteral("BQD-access"));
        QCOMPARE(tokens.refreshToken, QStringLiteral("AQC-refresh"));
        QCOMPARE(tokens.expiresIn, 3600);
        QVERIFY(!parseTokens("{}").isValid());

        const ApiError error = parseError(fixture("error.json"));
        QCOMPARE(error.status, 404);
        QCOMPARE(error.reason, QStringLiteral("NO_ACTIVE_DEVICE"));
        const ApiError grant = parseError(R"({"error":"invalid_grant","error_description":"Refresh token revoked"})");
        QCOMPARE(grant.reason, QStringLiteral("invalid_grant"));
        QCOMPARE(grant.message, QStringLiteral("Refresh token revoked"));
    }

    void parsesPlaylistItems()
    {
        const Page<Track> page = parseTracks(fixture("playlist_items.json"), TrackList::Playlist);
        QVERIFY(page.next.isEmpty());
        // The local file and the podcast episode are skipped.
        QCOMPARE(page.items.size(), 2);
        const Track &first = page.items.at(0);
        QCOMPARE(first.sourceId, QStringLiteral("spotify"));
        QCOMPARE(first.location, QUrl(QStringLiteral("spotify:track:t1")));
        QCOMPARE(first.title, QStringLiteral("First Song"));
        QCOMPARE(first.artist, QStringLiteral("The Example, Guest"));
        QCOMPARE(first.album, QStringLiteral("Debut"));
        QCOMPARE(first.durationMs, qint64(201000));
        // Entries from before the February 2026 rename still parse.
        QCOMPARE(page.items.at(1).title, QStringLiteral("Legacy Field"));
    }

    void parsesSavedTracksAndAlbums()
    {
        const Page<Track> liked = parseTracks(fixture("saved_tracks.json"), TrackList::Saved);
        QCOMPARE(liked.items.size(), 1);
        QCOMPARE(liked.items.first().album, QStringLiteral("Somewhere"));

        QString name;
        const Page<Track> album = parseAlbumTracks(fixture("album.json"), &name);
        QCOMPARE(name, QStringLiteral("Debut"));
        QCOMPARE(album.items.size(), 2);
        QCOMPARE(album.items.at(1).album, QStringLiteral("Debut"));
        QCOMPARE(album.items.at(1).durationMs, qint64(-1));
        QCOMPARE(album.next, QStringLiteral("https://api.spotify.com/v1/albums/a1/tracks?offset=50&limit=50"));

        const Page<Directory> saved = parseSavedAlbums(fixture("saved_albums.json"));
        QCOMPARE(saved.items.size(), 2);
        QCOMPARE(saved.items.at(0).subtitle, QStringLiteral("The Example (1999)"));
        QVERIFY(saved.items.at(1).subtitle.isEmpty());
    }

    void parsesPlaylistsAndArtists()
    {
        const Page<Directory> playlists = parsePlaylists(fixture("playlists.json"));
        QCOMPARE(playlists.items.size(), 2);
        QCOMPARE(playlists.items.at(0).subtitle, QStringLiteral("24"));
        QCOMPARE(playlists.items.at(1).subtitle, QStringLiteral("3"));
        QVERIFY(!playlists.next.isEmpty());

        const Page<Directory> artists = parseFollowedArtists(fixture("followed_artists.json"));
        QCOMPARE(artists.items.size(), 2);
        QCOMPARE(artists.items.at(1).title, QStringLiteral("Another"));
        QVERIFY(artists.next.contains(QLatin1String("after=ar2")));
    }

    void parsesSearch()
    {
        const SearchResults results = parseSearch(fixture("search.json"));
        QCOMPARE(results.artists.size(), 1);
        QCOMPARE(results.albums.size(), 1);
        QCOMPARE(results.playlists.size(), 1); // null entries dropped
        QCOMPARE(results.playlists.first().id, QStringLiteral("pl9"));
        QCOMPARE(results.tracks.size(), 1);
        QCOMPARE(results.tracks.first().album, QStringLiteral("Debut"));
    }

    void parsesDevicesAndPlayback()
    {
        const QList<Device> devices = parseDevices(fixture("devices.json"));
        QCOMPARE(devices.size(), 2);
        QCOMPARE(devices.at(0).name, QStringLiteral("Laptop"));
        QVERIFY(devices.at(1).active);
        QVERIFY(devices.at(1).restricted);

        const Playback playback = parsePlayback(fixture("player.json"));
        QVERIFY(playback.valid);
        QVERIFY(playback.playing);
        QCOMPARE(playback.progressMs, qint64(42000));
        QCOMPARE(playback.deviceId, QStringLiteral("dev1"));
        QCOMPARE(trackUri(playback.track.location), QStringLiteral("spotify:track:t1"));
        QVERIFY(!parsePlayback(QByteArray()).valid);
    }

    void picksADevice()
    {
        const Device phone{QStringLiteral("phone"), QStringLiteral("Pixel"), QStringLiteral("Smartphone"), true, false};
        const Device studio{QStringLiteral("studio"), QStringLiteral("Josh's Mac Studio"), QStringLiteral("Computer"), false, false};
        const Device laptop{QStringLiteral("laptop"), QStringLiteral("Laptop"), QStringLiteral("Computer"), false, false};
        const Device speaker{QStringLiteral("speaker"), QStringLiteral("Speaker"), QStringLiteral("Speaker"), true, true};
        const QList<Device> devices{phone, studio, laptop, speaker};
        const QString host = QStringLiteral("Joshs-Mac-Studio.local");
        QCOMPARE(pickDevice(devices, {}, host)->id, QStringLiteral("studio"));    // this computer
        QCOMPARE(pickDevice(devices, QStringLiteral("laptop"), host)->id, QStringLiteral("laptop")); // the user's choice
        QCOMPARE(pickDevice(devices, {}, QStringLiteral("elsewhere"))->id, QStringLiteral("phone")); // the active one
        QCOMPARE(pickDevice(devices, QStringLiteral("gone"), QStringLiteral("elsewhere"))->id, QStringLiteral("phone"));
        QVERIFY(!pickDevice({speaker}, {}, host)); // restricted devices can't be used
        QVERIFY(!pickDevice({}, {}, host));
    }

    void playsOnAConnectDevice_data()
    {
        QTest::addColumn<QString>("uri");
        QTest::addColumn<QString>("expectedBody");
        QTest::newRow("track") << QStringLiteral("spotify:track:t1") << QStringLiteral(R"({"uris":["spotify:track:t1"]})");
        QTest::newRow("whole playlist") << QStringLiteral("spotify:playlist:p1")
                                        << QStringLiteral(R"({"context_uri":"spotify:playlist:p1"})");
    }

    void playsOnAConnectDevice()
    {
        QFETCH(QString, uri);
        QFETCH(QString, expectedBody);
        FakeSpotify server;
        QVERIFY(server.isListening());
        // A stale "active" device that would swallow commands, and the app
        // that can really play.
        server.devices = {{QStringLiteral("ghost"), QStringLiteral("Old phone"), QStringLiteral("Smartphone"), true, false},
                          {QStringLiteral("studio"), QSysInfo::machineHostName(), QStringLiteral("Computer"), false, false}};
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("clientId"), QStringLiteral("c"));
        settings.setValue(QStringLiteral("refreshToken"), QStringLiteral("r"));
        SpotifyClient client(settings);
        client.setEndpoints(server.url("/v1"), QUrl(server.url("/token")));

        PlaybackEngine engine;
        engine.registerSource(&client);
        QSignalSpy errors(&engine, &PlaybackEngine::errorOccurred);
        QSignalSpy meta(&engine, &PlaybackEngine::metaDataChanged);
        Track track;
        track.sourceId = client.id();
        track.location = QUrl(uri);
        engine.play(track);

        QTRY_VERIFY(server.playing);
        QCOMPARE(server.deviceId, QStringLiteral("studio"));
        const QString play = server.log.filter(QStringLiteral("/me/player/play")).first();
        QVERIFY2(play.endsWith(expectedBody), qPrintable(play));
        QTRY_COMPARE(engine.duration(), qint64(200000));
        // Tracks take their names from the device; a whole playlist keeps its own.
        QCOMPARE(meta.size(), uri.contains(QLatin1String(":track:")) ? 1 : 0);

        // It keeps playing once the device reports the track, well past the
        // point where a mismatch would have let go of it.
        QTest::qWait(6000);
        QVERIFY2(errors.isEmpty(), errors.isEmpty() ? "" : qPrintable(errors.first().first().toString()));
        QCOMPARE(engine.state(), PlaybackEngine::State::Playing);

        engine.pause();
        QTRY_VERIFY(!server.playing);
        engine.stop();
    }

    void wakesAnIdleDevice()
    {
        // Josh's Spotify app answered 204 to play and then did nothing.
        FakeSpotify server;
        server.devices = {{QStringLiteral("studio"), QStringLiteral("Studio"), QStringLiteral("Computer"), true, false}};
        server.sleepy = true;
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("refreshToken"), QStringLiteral("r"));
        SpotifyClient client(settings);
        client.setEndpoints(server.url("/v1"), QUrl(server.url("/token")));
        PlaybackEngine engine;
        engine.registerSource(&client);
        QSignalSpy errors(&engine, &PlaybackEngine::errorOccurred);
        Track track;
        track.sourceId = client.id();
        track.location = trackLocation(QStringLiteral("t1"));
        engine.play(track);

        QTRY_VERIFY_WITH_TIMEOUT(server.playing, 15000);
        QCOMPARE(server.item, QStringLiteral("spotify:track:t1"));
        const QString transfer = server.log.filter(QStringLiteral("PUT /v1/me/player ")).value(0);
        QVERIFY2(transfer.endsWith(QLatin1String(R"({"device_ids":["studio"],"play":false})")), qPrintable(transfer));
        QTRY_COMPARE(engine.duration(), qint64(200000));
        QVERIFY(errors.isEmpty());
        QCOMPARE(engine.state(), PlaybackEngine::State::Playing);
        engine.stop();
    }

    void saysWhenTheDeviceNeverPlays()
    {
        FakeSpotify server;
        server.devices = {{QStringLiteral("studio"), QStringLiteral("Studio"), QStringLiteral("Computer"), true, false}};
        server.deaf = true;
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("refreshToken"), QStringLiteral("r"));
        SpotifyClient client(settings);
        client.setEndpoints(server.url("/v1"), QUrl(server.url("/token")));
        PlaybackEngine engine;
        engine.registerSource(&client);
        QSignalSpy errors(&engine, &PlaybackEngine::errorOccurred);
        Track track;
        track.sourceId = client.id();
        track.location = trackLocation(QStringLiteral("t1"));
        engine.play(track);

        // Rather than going quiet, the track fails with what Spotify said.
        QTRY_VERIFY_WITH_TIMEOUT(!errors.isEmpty(), 20000);
        QVERIFY2(errors.first().first().toString().contains(QLatin1String("didn't start it")),
                 qPrintable(errors.first().first().toString()));
        QCOMPARE(engine.state(), PlaybackEngine::State::Stopped);
    }

    void prefersNewAmpsOwnPlayer()
    {
        FakeSpotify server;
        server.devices = {{QStringLiteral("studio"), QSysInfo::machineHostName(), QStringLiteral("Computer"), true, false},
                          {QStringLiteral("newamp"), QStringLiteral("NewAmp"), QStringLiteral("Computer"), false, false}};
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        settings.setValue(QStringLiteral("refreshToken"), QStringLiteral("r"));
        SpotifyClient client(settings);
        client.setEndpoints(server.url("/v1"), QUrl(server.url("/token")));
        client.connectPlayer()->setAppDevice(QStringLiteral("newamp"));
        PlaybackEngine engine;
        engine.registerSource(&client);
        Track track;
        track.sourceId = client.id();
        track.location = trackLocation(QStringLiteral("t1"));
        engine.play(track);

        QTRY_VERIFY(server.playing);
        QCOMPARE(server.deviceId, QStringLiteral("newamp"));
        engine.stop();
    }

    void reportsWebPlayerEvents()
    {
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        SpotifyClient client(settings);
        SpotifyWebPlayer player(client);
        QSignalSpy ready(&player, &SpotifyWebPlayer::ready);
        QSignalSpy notReady(&player, &SpotifyWebPlayer::notReady);
        QSignalSpy problems(&player, &SpotifyWebPlayer::problem);

        player.handleMessage(R"({"type":"ready","device_id":"d1"})");
        QCOMPARE(ready.size(), 1);
        QCOMPARE(ready.first().first().toString(), QStringLiteral("d1"));
        player.handleMessage(R"({"type":"not_ready","device_id":"d1"})");
        QCOMPARE(notReady.size(), 1);

        player.handleMessage(R"({"type":"drm","systems":["com.apple.fps"]})");
        QVERIFY(problems.isEmpty());
        player.handleMessage(R"({"type":"drm","systems":[]})");
        QCOMPARE(problems.size(), 1);
        player.handleMessage(R"({"type":"account_error","message":"premium required"})");
        QVERIFY(problems.last().first().toString().contains(QLatin1String("Premium")));
        player.handleMessage(R"({"type":"authentication_error","message":"bad scope"})");
        QVERIFY(problems.last().first().toString().contains(QLatin1String("sign in again")));
        player.handleMessage(R"({"type":"log","message":"chatter"})");
        QCOMPARE(problems.size(), 3);
    }

    void roundTripsIds()
    {
        QCOMPARE(trackUri(trackLocation(QStringLiteral("abc"))), QStringLiteral("spotify:track:abc"));
        QVERIFY(trackUri(QUrl(QStringLiteral("file:///music/a.mp3"))).isEmpty());
        QVERIFY(trackUri(QUrl(QStringLiteral("spotify:track:"))).isEmpty());

        FolderKind kind;
        QString id;
        QVERIFY(parseFolderId(folderId(FolderKind::Playlist, QStringLiteral("pl1")), &kind, &id));
        QCOMPARE(kind, FolderKind::Playlist);
        QCOMPARE(id, QStringLiteral("pl1"));
        QVERIFY(parseFolderId(folderId(FolderKind::LikedSongs), &kind, &id));
        QCOMPARE(kind, FolderKind::LikedSongs);
        QVERIFY(id.isEmpty());
        QVERIFY(!parseFolderId(QStringLiteral("nonsense"), &kind, &id));
        QVERIFY(!parseFolderId(QStringLiteral("other:x"), &kind, &id));
    }
};

QTEST_MAIN(TestSpotify)
#include "tst_spotify.moc"

#include "SpotifyApi.h"

#include <QFile>
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

QTEST_GUILESS_MAIN(TestSpotify)
#include "tst_spotify.moc"

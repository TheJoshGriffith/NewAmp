#include "JellyfinApi.h"
#include "JellyfinClient.h"
#include "JellyfinLibrary.h"
#include "JellyfinReporter.h"
#include "plugins/PluginHost.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>
#include <QUrlQuery>

#include <optional>

using namespace newamp;
using namespace newamp::jellyfin;

namespace {
const QString ServerId = QStringLiteral("f3c1a2b4d5e6478899aabbccddeeff00");
const QString UserId = QStringLiteral("0a1b2c3d4e5f60718293a4b5c6d7e8f9");

QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(NEWAMP_JELLYFIN_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

QUrlQuery queryOf(const QString &pathAndQuery)
{
    return QUrlQuery(QUrl(QStringLiteral("http://x") + pathAndQuery));
}

Track jellyfinTrack(const QString &itemId, qint64 durationMs)
{
    Track track;
    track.sourceId = QStringLiteral("jellyfin");
    track.location = trackLocation({ServerId, itemId, QStringLiteral("flac")});
    track.durationMs = durationMs;
    return track;
}

// Answers HTTP requests from a table keyed by path (without the query) and
// remembers each request, standing in for a Jellyfin server.
class FakeServer : public QTcpServer
{
public:
    struct Request
    {
        QByteArray method;
        QString path; // with query
        QByteArray authorization;
        QByteArray body;
    };
    struct Response
    {
        int status = 200;
        QByteArray body;
    };

    QHash<QString, Response> routes;
    QList<Request> requests;

    FakeServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] { serve(socket); });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QString url() const { return QStringLiteral("http://127.0.0.1:%1").arg(serverPort()); }

    QStringList paths() const
    {
        QStringList result;
        for (const Request &r : requests)
            result << r.path.section(QLatin1Char('?'), 0, 0);
        return result;
    }

private:
    void serve(QTcpSocket *socket)
    {
        const QByteArray data = socket->peek(1 << 20);
        const int end = data.indexOf("\r\n\r\n");
        if (end < 0)
            return;
        Request request;
        qint64 length = 0;
        const QList<QByteArray> lines = data.left(end).split('\n');
        const QList<QByteArray> first = lines.value(0).trimmed().split(' ');
        request.method = first.value(0);
        request.path = QString::fromLatin1(first.value(1));
        for (const QByteArray &line : lines.mid(1)) {
            const int colon = line.indexOf(':');
            const QByteArray name = line.left(colon).trimmed().toLower();
            const QByteArray value = line.mid(colon + 1).trimmed();
            if (name == "content-length")
                length = value.toLongLong();
            else if (name == "authorization")
                request.authorization = value;
        }
        if (data.size() < end + 4 + length)
            return; // the body is still arriving
        socket->read(end + 4 + length);
        request.body = data.mid(end + 4, int(length));
        requests << request;

        const Response response = routes.value(request.path.section(QLatin1Char('?'), 0, 0), {404, "not found"});
        socket->write("HTTP/1.1 " + QByteArray::number(response.status) + " X\r\nContent-Type: application/json"
                      + "\r\nContent-Length: " + QByteArray::number(response.body.size())
                      + "\r\nConnection: close\r\n\r\n" + response.body);
        socket->disconnectFromHost();
    }
};

// Records what the library asks of the host.
class RecordingHost : public PluginHost
{
public:
    int changed = 0;
    void addTracks(const QList<Track> &) override {}
    void playTracks(const QList<Track> &) override {}
    QWidget *dialogParent() const override { return nullptr; }
    std::unique_ptr<QSettings> settings(const QString &) const override { return nullptr; }
    void addPlaybackObserver(PlaybackObserver *) override {}
    void addLibrary(LibraryProvider *) override {}
    void libraryChanged(LibraryProvider *) override { ++changed; }
    void libraryStatusChanged(LibraryProvider *) override {}
    void showLibrary(LibraryProvider *) override {}
};
} // namespace

class TestJellyfin : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void normalizesServerAddresses()
    {
        QCOMPARE(normalizeServerUrl(QStringLiteral("192.168.1.20")), QStringLiteral("http://192.168.1.20:8096"));
        QCOMPARE(normalizeServerUrl(QStringLiteral(" nas:8920 ")), QStringLiteral("http://nas:8920"));
        QCOMPARE(normalizeServerUrl(QStringLiteral("https://music.example.com/jellyfin/")),
                 QStringLiteral("https://music.example.com/jellyfin"));
        QCOMPARE(normalizeServerUrl(QStringLiteral("http://nas:8096/web/#/home")), QStringLiteral("http://nas:8096/web"));
        QVERIFY(normalizeServerUrl(QString()).isEmpty());
        QVERIFY(normalizeServerUrl(QStringLiteral("ftp://nas")).isEmpty());
    }

    void buildsAuthorizationHeader()
    {
        QCOMPARE(authorizationHeader(QStringLiteral("Sam's \"Mac\""), QStringLiteral("dev-1")),
                 QByteArray("MediaBrowser Client=\"NewAmp\", Device=\"Sam%27s%20%22Mac%22\", DeviceId=\"dev-1\", "
                            "Version=\"" NEWAMP_VERSION "\""));
        QVERIFY(authorizationHeader(QStringLiteral("x"), QStringLiteral("y"), QStringLiteral("tok"))
                    .endsWith(", Token=\"tok\""));
    }

    void parsesServerAndSession()
    {
        const ServerInfo info = parseServerInfo(fixture("public_info.json"));
        QCOMPARE(info.id, ServerId);
        QCOMPARE(info.name, QStringLiteral("Media Box"));
        QVERIFY(parseServerInfo("<html>router login</html>").id.isEmpty());

        const Session session = parseAuthentication(fixture("auth.json"));
        QCOMPARE(session.token, QStringLiteral("access-token-123"));
        QCOMPARE(session.userId, UserId);
        QCOMPARE(session.userName, QStringLiteral("sam"));
        QCOMPARE(session.serverId, ServerId);
        QVERIFY(parseAuthentication("{}").token.isEmpty());

        const QJsonObject body = QJsonDocument::fromJson(authenticateBody(QStringLiteral("sam"), QStringLiteral("p\"w"))).object();
        QCOMPARE(body.value(QStringLiteral("Username")).toString(), QStringLiteral("sam"));
        QCOMPARE(body.value(QStringLiteral("Pw")).toString(), QStringLiteral("p\"w"));
    }

    void picksUserForApiKey()
    {
        const QList<User> users = parseUsers(fixture("users.json"));
        QCOMPARE(users.size(), 2); // the one without an id is skipped
        QCOMPARE(pickUser(users, QStringLiteral("sam"))->id, UserId);
        QCOMPARE(pickUser(users, QString())->name, QStringLiteral("admin"));
        QCOMPARE(pickUser(users, QStringLiteral("nobody"))->name, QStringLiteral("admin"));
        QVERIFY(!pickUser({}, QStringLiteral("sam")));
    }

    void parsesDiscoveryReplies()
    {
        const DiscoveredServer server = parseDiscoveryReply(fixture("discovery.json"));
        QCOMPARE(server.address, QStringLiteral("http://192.168.1.20:8096"));
        QCOMPARE(server.name, QStringLiteral("Media Box"));
        QVERIFY(parseDiscoveryReply(discoveryRequest()).address.isEmpty()); // our own broadcast echoed back
    }

    void parsesLibrariesArtistsAlbums()
    {
        const QList<Folder> views = parseMusicViews(fixture("views.json"));
        QCOMPARE(views.size(), 2); // movies and books are skipped
        QCOMPARE(views[0].title, QStringLiteral("Music"));
        QCOMPARE(views[1].title, QStringLiteral("Vinyl Rips"));

        const QList<Folder> artists = parseFolders(fixture("artists.json"));
        QCOMPARE(artists.size(), 2);
        QVERIFY(artists[0].detail.isEmpty());

        const QList<Folder> albums = parseFolders(fixture("albums.json"));
        QCOMPARE(albums[0].detail, QStringLiteral("2009"));
        QVERIFY(albums[1].detail.isEmpty());

        const QList<Folder> playlists = parseFolders(fixture("playlists.json"));
        QCOMPARE(playlists.size(), 1); // the video playlist is skipped
        QCOMPARE(playlists[0].detail, QStringLiteral("42 tracks"));
    }

    void parsesTracks()
    {
        const QList<Track> tracks = parseTracks(fixture("tracks.json"), ServerId);
        QCOMPARE(tracks.size(), 3); // the entry without an id is skipped

        const Track &first = tracks[0];
        QCOMPARE(first.sourceId, QStringLiteral("jellyfin"));
        QCOMPARE(first.title, QStringLiteral("Harbour"));
        QCOMPARE(first.artist, QStringLiteral("The Lanterns"));
        QCOMPARE(first.album, QStringLiteral("First Light"));
        QCOMPARE(first.durationMs, qint64(234567));
        QCOMPARE(first.location.toString(),
                 QStringLiteral("jellyfin://%1/dddd0000000000000000000000000001?container=flac").arg(ServerId));

        // The album artist stands in when the track lists none; FFmpeg's
        // container list for MP4 audio becomes m4a.
        QCOMPARE(tracks[1].artist, QStringLiteral("The Lanterns"));
        QCOMPARE(parseTrackLocation(tracks[1].location).container, QStringLiteral("m4a"));
        // The media source's path gives the suffix; no duration is -1.
        QCOMPARE(parseTrackLocation(tracks[2].location).container, QStringLiteral("mp3"));
        QCOMPARE(tracks[2].durationMs, qint64(-1));
    }

    void parsesSearchResults()
    {
        const SearchResults found = parseSearch(fixture("search.json"), ServerId);
        QCOMPARE(found.albums.size(), 1);
        QCOMPARE(found.albums[0].title, QStringLiteral("The Lanterns - First Light"));
        QCOMPARE(found.playlists.size(), 1); // the video playlist is left out
        QCOMPARE(found.playlists[0].title, QStringLiteral("Light Sleepers"));
        QCOMPARE(found.tracks.size(), 1);
        QCOMPARE(found.tracks[0].artist, QStringLiteral("Orla Vance"));
        QCOMPARE(parseTrackLocation(found.tracks[0].location).container, QStringLiteral("ogg"));

        const QList<Folder> artists = parseSearch(fixture("artists.json"), ServerId).artists;
        QCOMPARE(artists.size(), 2);
    }

    void buildsPaths()
    {
        const QUrlQuery search = queryOf(searchPath(UserId, QStringLiteral("Sigur Rós & co+"), QStringLiteral("lib")));
        QCOMPARE(search.queryItemValue(QStringLiteral("searchTerm"), QUrl::FullyDecoded),
                 QStringLiteral("Sigur Rós & co+"));
        QCOMPARE(search.queryItemValue(QStringLiteral("parentId")), QStringLiteral("lib"));
        QCOMPARE(search.queryItemValue(QStringLiteral("userId")), UserId);
        QVERIFY(!searchPath(UserId, QStringLiteral("a"), {}).contains(QStringLiteral("parentId")));
        QVERIFY(artistSearchPath(UserId, QStringLiteral("a"), {}).startsWith(QStringLiteral("/Artists?")));

        QVERIFY(viewsPath(UserId).startsWith(QStringLiteral("/UserViews?userId=")));
        QCOMPARE(queryOf(albumTracksPath(UserId, QStringLiteral("alb"))).queryItemValue(QStringLiteral("parentId")),
                 QStringLiteral("alb"));
        QVERIFY(playlistTracksPath(UserId, QStringLiteral("pl")).startsWith(QStringLiteral("/Playlists/pl/Items?")));
    }

    void buildsStreamUrls()
    {
        const TrackRef ref{ServerId, QStringLiteral("dddd0000000000000000000000000001"), QStringLiteral("flac")};
        const QUrl stream = streamUrl(QStringLiteral("http://nas:8096"), ref, QStringLiteral("tok"), QStringLiteral("dev"));
        QCOMPARE(stream.path(), QStringLiteral("/Audio/dddd0000000000000000000000000001/stream.flac"));
        QCOMPARE(stream.fileName(), QStringLiteral("stream.flac")); // picks the decoder
        const QUrlQuery query(stream);
        QCOMPARE(query.queryItemValue(QStringLiteral("static")), QStringLiteral("true"));
        QCOMPARE(query.queryItemValue(QStringLiteral("api_key")), QStringLiteral("tok"));

        const QUrl transcode = transcodeUrl(QStringLiteral("https://h/jf"), ref, QStringLiteral("tok"), QStringLiteral("dev"));
        QCOMPARE(transcode.path(), QStringLiteral("/jf/Audio/dddd0000000000000000000000000001/stream.mp3"));
        QCOMPARE(QUrlQuery(transcode).queryItemValue(QStringLiteral("audioCodec")), QStringLiteral("mp3"));
    }

    void roundTripsLocationsAndFolders()
    {
        const TrackRef ref{ServerId, QStringLiteral("abc"), QStringLiteral("m4a")};
        const TrackRef parsed = parseTrackLocation(trackLocation(ref));
        QCOMPARE(parsed.serverId, ref.serverId);
        QCOMPARE(parsed.itemId, ref.itemId);
        QCOMPARE(parsed.container, ref.container);
        QVERIFY(!parseTrackLocation(QUrl(QStringLiteral("plex://abc/library/parts/1"))).isValid());

        FolderKind kind = FolderKind::Library;
        QString item;
        QVERIFY(parseFolderId(folderId(FolderKind::Album, QStringLiteral("42")), &kind, &item));
        QCOMPARE(kind, FolderKind::Album);
        QCOMPARE(item, QStringLiteral("42"));
        QVERIFY(parseFolderId(folderId(FolderKind::Playlists), &kind, &item));
        QCOMPARE(kind, FolderKind::Playlists);
        QVERIFY(!parseFolderId(QStringLiteral("nonsense|1"), &kind, &item));
        QVERIFY(!parseFolderId(QString(), &kind, &item));
    }

    void reportsPlayback()
    {
        QList<QPair<QString, QJsonObject>> sent;
        JellyfinReporter reporter([&](const QString &path, const QByteArray &json) {
            sent.append({path, QJsonDocument::fromJson(json).object()});
        });
        const auto ticks = [&](int i) { return qint64(sent[i].second.value(QStringLiteral("PositionTicks")).toDouble()); };

        reporter.trackStarted(jellyfinTrack(QStringLiteral("item1"), 200000));
        QCOMPARE(sent.size(), 1);
        QCOMPARE(sent[0].first, QStringLiteral("/Sessions/Playing"));
        QCOMPARE(sent[0].second.value(QStringLiteral("ItemId")).toString(), QStringLiteral("item1"));

        reporter.playbackStateChanged(PlaybackObserver::State::Playing); // not news
        reporter.positionChanged(5000, 200000);                         // too soon for progress
        QCOMPARE(sent.size(), 1);

        reporter.playbackStateChanged(PlaybackObserver::State::Paused);
        QCOMPARE(sent.last().first, QStringLiteral("/Sessions/Playing/Progress"));
        QCOMPARE(sent.last().second.value(QStringLiteral("EventName")).toString(), QStringLiteral("pause"));
        QCOMPARE(sent.last().second.value(QStringLiteral("IsPaused")).toBool(), true);
        QCOMPARE(ticks(int(sent.size()) - 1), qint64(5000) * 10000);
        reporter.playbackStateChanged(PlaybackObserver::State::Playing);
        QCOMPARE(sent.last().second.value(QStringLiteral("EventName")).toString(), QStringLiteral("unpause"));

        // Finishing reports a stop at the very end, which marks it played.
        reporter.trackFinished(jellyfinTrack(QStringLiteral("item1"), 200000));
        QCOMPARE(sent.last().first, QStringLiteral("/Sessions/Playing/Stopped"));
        QCOMPARE(ticks(int(sent.size()) - 1), qint64(200000) * 10000);
        const int count = int(sent.size());
        reporter.playbackStateChanged(PlaybackObserver::State::Stopped);
        QCOMPARE(int(sent.size()), count);

        // Starting another track closes out one left playing.
        reporter.trackStarted(jellyfinTrack(QStringLiteral("item2"), 1000));
        reporter.trackStarted(jellyfinTrack(QStringLiteral("item3"), 1000));
        QCOMPARE(sent[sent.size() - 2].first, QStringLiteral("/Sessions/Playing/Stopped"));
        QCOMPARE(sent[sent.size() - 2].second.value(QStringLiteral("ItemId")).toString(), QStringLiteral("item2"));

        // Tracks from elsewhere aren't reported.
        reporter.playbackStateChanged(PlaybackObserver::State::Stopped);
        const int before = int(sent.size());
        Track local;
        local.sourceId = QStringLiteral("local");
        local.location = QUrl::fromLocalFile(QStringLiteral("/tmp/a.mp3"));
        reporter.trackStarted(local);
        reporter.playbackStateChanged(PlaybackObserver::State::Paused);
        reporter.trackFinished(local);
        QCOMPARE(int(sent.size()), before);
    }

    void signsInAndBrowses()
    {
        FakeServer server;
        server.routes.insert(QStringLiteral("/System/Info/Public"), {200, fixture("public_info.json")});
        server.routes.insert(QStringLiteral("/Users/AuthenticateByName"), {200, fixture("auth.json")});
        server.routes.insert(QStringLiteral("/UserViews"), {200, fixture("views.json")});
        server.routes.insert(QStringLiteral("/Artists"), {200, fixture("artists.json")});
        server.routes.insert(QStringLiteral("/Items"), {200, fixture("search.json")});

        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        JellyfinClient client(settings);
        RecordingHost host;
        JellyfinLibrary library(client, host, {});
        QVERIFY(!library.canSearch());

        QString error = QStringLiteral("pending");
        client.signIn(server.url(), QStringLiteral("sam"), QStringLiteral("secret"), {},
                      [&](const QString &e) { error = e; });
        QTRY_COMPARE(error, QString());
        QVERIFY(client.isSignedIn());
        QVERIFY(library.canSearch());
        QCOMPARE(host.changed, 1);
        QCOMPARE(client.userId(), UserId);
        QCOMPARE(settings.value(QStringLiteral("token")).toString(), QStringLiteral("access-token-123"));
        const FakeServer::Request auth = server.requests.last();
        QCOMPARE(auth.method, QByteArray("POST"));
        QVERIFY(auth.authorization.startsWith("MediaBrowser Client=\"NewAmp\""));
        QVERIFY(!auth.authorization.contains("Token="));
        QCOMPARE(QJsonDocument::fromJson(auth.body).object().value(QStringLiteral("Pw")).toString(),
                 QStringLiteral("secret"));

        // The top level lists music libraries, then playlists; requests now
        // carry the token.
        QList<LibraryItem> items;
        bool listed = false;
        library.listItems({}, [&](const QList<LibraryItem> &result) {
            items = result;
            listed = true;
        });
        QTRY_VERIFY(listed);
        QCOMPARE(items.size(), 3);
        QCOMPARE(items[0].title, QStringLiteral("Music"));
        QCOMPARE(items[2].title, QStringLiteral("Playlists"));
        QVERIFY(server.requests.last().authorization.contains("Token=\"access-token-123\""));

        // Search from a library asks both endpoints within it.
        bool searched = false;
        library.search(items[0].id, QStringLiteral("light"), [&](const QList<LibraryItem> &result) {
            items = result;
            searched = true;
        });
        QTRY_VERIFY(searched);
        QCOMPARE(items.size(), 5); // two artists, an album, a playlist and a track
        QCOMPARE(items[0].kind, LibraryItem::Kind::Artist);
        QCOMPARE(items.last().kind, LibraryItem::Kind::Track);
        QCOMPARE(items.last().title, QStringLiteral("Orla Vance - Lights Out"));
        QCOMPARE(queryOf(server.requests.last().path).queryItemValue(QStringLiteral("parentId")),
                 QStringLiteral("aaaa0000000000000000000000000002"));

        // Stream URLs point at the server for its own tracks only.
        const Track track = items.last().track;
        QVERIFY(client.streamUrl(track).toString().startsWith(server.url() + QStringLiteral("/Audio/")));
        Track other = track;
        other.location = trackLocation({QStringLiteral("someotherserver"), QStringLiteral("x"), {}});
        QVERIFY(client.streamUrl(other).isEmpty());

        // Signing out ends the session on the server and forgets the token,
        // but keeps the address.
        client.signOut();
        QVERIFY(!client.isSignedIn());
        QTRY_COMPARE(server.paths().last(), QStringLiteral("/Sessions/Logout"));
        QVERIFY(settings.value(QStringLiteral("token")).toString().isEmpty());
        QCOMPARE(settings.value(QStringLiteral("serverUrl")).toString(), server.url());
        QCOMPARE(host.changed, 2);
    }

    void parsesAndEditsItems()
    {
        const TrackInfo info = parseItemInfo(fixture("item.json"));
        QVERIFY(info.error.isEmpty());
        const auto tag = [&](const char *key) {
            for (const TagField &field : info.tags)
                if (field.key == QLatin1String(key))
                    return field.value;
            return QStringLiteral("<none>");
        };
        QCOMPARE(tag("TITLE"), QStringLiteral("Harbour"));
        QCOMPARE(tag("ARTIST"), QStringLiteral("The Lanterns; Orla Vance"));
        QCOMPARE(tag("ALBUMARTIST"), QStringLiteral("The Lanterns"));
        QCOMPARE(tag("TRACKNUMBER"), QStringLiteral("1"));
        QCOMPARE(tag("DATE"), QStringLiteral("2019"));
        QCOMPARE(tag("GENRE"), QStringLiteral("Folk; Indie"));
        QCOMPARE(tag("COMMENT"), QStringLiteral("<none>")); // Jellyfin keeps none
        QCOMPARE(info.details.first(), qMakePair(QStringLiteral("Format"), QStringLiteral("FLAC")));
        QVERIFY(info.details.contains(qMakePair(QStringLiteral("Bitrate"), QStringLiteral("958 kbps"))));
        QVERIFY(info.details.contains(qMakePair(QStringLiteral("Sample rate"), QStringLiteral("44100 Hz"))));
        QVERIFY(info.details.contains(qMakePair(QStringLiteral("Length"), QStringLiteral("3:55"))));

        const QJsonObject edited = QJsonDocument::fromJson(editedItem(fixture("item.json"), {
            {QStringLiteral("TITLE"), QStringLiteral("Harbour (Live)")},
            {QStringLiteral("ARTIST"), QStringLiteral("Orla Vance")},
            {QStringLiteral("TRACKNUMBER"), QStringLiteral("4/12")},
            {QStringLiteral("DATE"), QString()},
            {QStringLiteral("GENRE"), QStringLiteral("Rock; Pop")},
        })).object();
        QCOMPARE(edited.value(QStringLiteral("Name")).toString(), QStringLiteral("Harbour (Live)"));
        QCOMPARE(edited.value(QStringLiteral("ArtistItems")).toArray().size(), 1);
        QCOMPARE(edited.value(QStringLiteral("ArtistItems")).toArray().first().toObject().value(QStringLiteral("Name")).toString(),
                 QStringLiteral("Orla Vance"));
        QCOMPARE(edited.value(QStringLiteral("IndexNumber")).toInt(), 4);
        QVERIFY(edited.value(QStringLiteral("ProductionYear")).isNull());
        QCOMPARE(edited.value(QStringLiteral("Genres")).toArray().size(), 2);
        // Everything else goes back as it came, or the server would clear it.
        QCOMPARE(edited.value(QStringLiteral("Album")).toString(), QStringLiteral("First Light"));
        QCOMPARE(edited.value(QStringLiteral("Overview")).toString(), QStringLiteral("kept as is"));
        QCOMPARE(edited.value(QStringLiteral("ParentIndexNumber")).toInt(), 1);
        QVERIFY(editedItem("{}", {}).isEmpty());
    }

    void editsTagsOnTheServer()
    {
        FakeServer server;
        server.routes.insert(QStringLiteral("/System/Info/Public"), {200, fixture("public_info.json")});
        server.routes.insert(QStringLiteral("/Users/AuthenticateByName"), {200, fixture("auth.json")});
        server.routes.insert(QStringLiteral("/Users/Me"), {200, fixture("me_admin.json")});
        const QString itemRoute = QStringLiteral("/Items/dddd0000000000000000000000000001");
        server.routes.insert(itemRoute, {200, fixture("item.json")});

        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        JellyfinClient client(settings);
        QString error = QStringLiteral("pending");
        client.signIn(server.url(), QStringLiteral("sam"), QStringLiteral("secret"), {},
                      [&](const QString &e) { error = e; });
        QTRY_COMPARE(error, QString());

        MetadataEditor *editor = client.metadataEditor();
        QVERIFY(editor);
        const Track track = jellyfinTrack(QStringLiteral("dddd0000000000000000000000000001"), -1);
        std::optional<TrackInfo> info;
        editor->readInfo(track, [&](const TrackInfo &i) { info = i; });
        QTRY_VERIFY(info.has_value());
        QVERIFY(info->canEdit);
        QCOMPARE(info->details.first().second, QStringLiteral("Media Box"));

        std::optional<QString> saved;
        editor->writeTags(track, {{QStringLiteral("TITLE"), QStringLiteral("New")}},
                          [&](const QString &e) { saved = e; });
        QTRY_VERIFY(saved.has_value());
        QCOMPARE(*saved, QString());
        const FakeServer::Request post = server.requests.last();
        QCOMPARE(post.method, QByteArray("POST"));
        QCOMPARE(post.path, itemRoute);
        const QJsonObject body = QJsonDocument::fromJson(post.body).object();
        QCOMPARE(body.value(QStringLiteral("Name")).toString(), QStringLiteral("New"));
        QCOMPARE(body.value(QStringLiteral("Album")).toString(), QStringLiteral("First Light"));

        // Someone else's track, and a user who isn't an administrator.
        Track other = track;
        other.location = trackLocation({QStringLiteral("someotherserver"), QStringLiteral("x"), {}});
        info.reset();
        editor->readInfo(other, [&](const TrackInfo &i) { info = i; });
        QVERIFY(info.has_value() && !info->error.isEmpty());

        client.signOut();
        server.routes.insert(QStringLiteral("/Users/Me"), {200, fixture("me_user.json")});
        error = QStringLiteral("pending");
        client.signIn(server.url(), QStringLiteral("kim"), QStringLiteral("pw"), {},
                      [&](const QString &e) { error = e; });
        QTRY_COMPARE(error, QString());
        info.reset();
        editor->readInfo(track, [&](const TrackInfo &i) { info = i; });
        QTRY_VERIFY(info.has_value());
        QVERIFY(!info->canEdit);
        QVERIFY(info->note.contains(QStringLiteral("administrator")));

        server.routes.insert(itemRoute, {403, "forbidden"});
        saved.reset();
        editor->writeTags(track, {{QStringLiteral("TITLE"), QStringLiteral("New")}},
                          [&](const QString &e) { saved = e; });
        QTRY_VERIFY(saved.has_value());
        QVERIFY(!saved->isEmpty());
    }

    void explainsSignInFailures()
    {
        FakeServer server;
        QTemporaryDir dir;
        QSettings settings(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        JellyfinClient client(settings);
        QString error;
        const auto signIn = [&](const QString &apiKey) {
            error = QStringLiteral("pending");
            client.signIn(server.url(), QStringLiteral("sam"), QStringLiteral("wrong"), apiKey,
                          [&](const QString &e) { error = e; });
            QTRY_VERIFY(error != QLatin1String("pending"));
        };

        signIn({}); // no Jellyfin here
        QVERIFY2(error.contains(QStringLiteral("Could not reach")), qPrintable(error));

        server.routes.insert(QStringLiteral("/System/Info/Public"), {200, "<html>hello</html>"});
        signIn({});
        QVERIFY2(error.contains(QStringLiteral("doesn't look like")), qPrintable(error));

        server.routes.insert(QStringLiteral("/System/Info/Public"), {200, fixture("public_info.json")});
        server.routes.insert(QStringLiteral("/Users/AuthenticateByName"), {401, ""});
        signIn({});
        QCOMPARE(error, QStringLiteral("Wrong user name or password"));

        server.routes.insert(QStringLiteral("/Users"), {401, ""});
        signIn(QStringLiteral("bad-key"));
        QCOMPARE(error, QStringLiteral("The server didn't accept that API key"));
        QVERIFY(!client.isSignedIn());

        // A good key browses as the named user.
        server.routes.insert(QStringLiteral("/Users"), {200, fixture("users.json")});
        signIn(QStringLiteral("good-key"));
        QCOMPARE(error, QString());
        QCOMPARE(client.userId(), UserId);
        QVERIFY(server.requests.last().authorization.contains("Token=\"good-key\""));

        // Signing out leaves an API key alone on the server.
        const int count = int(server.requests.size());
        client.signOut();
        QTest::qWait(100);
        QCOMPARE(int(server.requests.size()), count);

        QVERIFY(client.isSignedIn() == false);
        error = QStringLiteral("pending");
        client.signIn(QStringLiteral("   "), {}, {}, {}, [&](const QString &e) { error = e; });
        QVERIFY(error.startsWith(QStringLiteral("Enter the server's address")));
    }
};

QTEST_GUILESS_MAIN(TestJellyfin)
#include "tst_jellyfin.moc"

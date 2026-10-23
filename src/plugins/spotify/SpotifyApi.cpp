#include "SpotifyApi.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <optional>

namespace newamp::spotify {

namespace {

const QString Accounts = QStringLiteral("https://accounts.spotify.com");

// Reading the library and controlling playback; "streaming" lets a later
// in-app player (the Web Playback SDK) use the same sign-in.
const QString Scopes = QStringLiteral("user-read-playback-state user-modify-playback-state "
                                      "user-read-currently-playing playlist-read-private "
                                      "playlist-read-collaborative user-library-read user-follow-read "
                                      "streaming");

QJsonObject object(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

QString form(const QList<std::pair<QString, QString>> &fields)
{
    QUrlQuery query;
    for (const auto &[key, value] : fields)
        query.addQueryItem(key, QString::fromLatin1(QUrl::toPercentEncoding(value)));
    return query.toString(QUrl::FullyEncoded);
}

QString artistNames(const QJsonArray &artists)
{
    QStringList names;
    for (const QJsonValue &artist : artists)
        names.append(artist.toObject().value(QStringLiteral("name")).toString());
    names.removeAll(QString());
    return names.join(QStringLiteral(", "));
}

QString year(const QJsonObject &album)
{
    return album.value(QStringLiteral("release_date")).toString().left(4);
}

// Null when the object isn't a playable Spotify track.
std::optional<Track> parseTrack(const QJsonObject &o, const QString &album)
{
    const QString id = o.value(QStringLiteral("id")).toString();
    if (id.isEmpty() || o.value(QStringLiteral("is_local")).toBool()
        || o.value(QStringLiteral("type")).toString(QStringLiteral("track")) != QLatin1String("track"))
        return std::nullopt;
    Track track;
    track.sourceId = QString::fromLatin1(SourceId);
    track.location = trackLocation(id);
    track.title = o.value(QStringLiteral("name")).toString();
    track.artist = artistNames(o.value(QStringLiteral("artists")).toArray());
    const QJsonObject albumObject = o.value(QStringLiteral("album")).toObject();
    track.album = albumObject.isEmpty() ? album : albumObject.value(QStringLiteral("name")).toString();
    const qint64 duration = o.value(QStringLiteral("duration_ms")).toInteger(-1);
    track.durationMs = duration > 0 ? duration : -1;
    return track;
}

Directory albumDirectory(const QJsonObject &album)
{
    QString subtitle = artistNames(album.value(QStringLiteral("artists")).toArray());
    const QString released = year(album);
    if (!released.isEmpty())
        subtitle = subtitle.isEmpty() ? released : subtitle + QStringLiteral(" (") + released + QLatin1Char(')');
    return {album.value(QStringLiteral("id")).toString(), album.value(QStringLiteral("name")).toString(), subtitle};
}

Directory playlistDirectory(const QJsonObject &playlist)
{
    // The track count moved from "tracks" to "items" in February 2026.
    QJsonValue counts = playlist.value(QStringLiteral("items"));
    if (!counts.isObject())
        counts = playlist.value(QStringLiteral("tracks"));
    const int total = counts.toObject().value(QStringLiteral("total")).toInt(-1);
    return {playlist.value(QStringLiteral("id")).toString(), playlist.value(QStringLiteral("name")).toString(),
            total >= 0 ? QString::number(total) : QString()};
}

Directory artistDirectory(const QJsonObject &artist)
{
    return {artist.value(QStringLiteral("id")).toString(), artist.value(QStringLiteral("name")).toString(), {}};
}

// Every object in a paging object's items, through convert; entries it
// can't use (null playlists in search results, missing ids) are dropped.
template<typename T, typename Convert>
Page<T> parsePage(const QJsonObject &paging, Convert convert)
{
    Page<T> page;
    for (const QJsonValue &value : paging.value(QStringLiteral("items")).toArray()) {
        if (!value.isObject())
            continue;
        T item = convert(value.toObject());
        if (!item.id.isEmpty())
            page.items.append(item);
    }
    page.next = paging.value(QStringLiteral("next")).toString();
    return page;
}

} // namespace

QString redirectUri()
{
    return QStringLiteral("http://127.0.0.1:%1/callback").arg(RedirectPort);
}

QByteArray makeCodeVerifier()
{
    // 64 characters from the unreserved set RFC 7636 allows.
    static const char Alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-._~";
    QByteArray verifier(64, Qt::Uninitialized);
    for (char &c : verifier)
        c = Alphabet[QRandomGenerator::system()->bounded(int(sizeof(Alphabet) - 1))];
    return verifier;
}

QByteArray codeChallenge(const QByteArray &verifier)
{
    return QCryptographicHash::hash(verifier, QCryptographicHash::Sha256)
        .toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
}

QUrl authorizeUrl(const QString &clientId, const QByteArray &challenge, const QString &state)
{
    QUrl url(Accounts + QStringLiteral("/authorize"));
    url.setQuery(form({{QStringLiteral("client_id"), clientId},
                       {QStringLiteral("response_type"), QStringLiteral("code")},
                       {QStringLiteral("redirect_uri"), redirectUri()},
                       {QStringLiteral("code_challenge_method"), QStringLiteral("S256")},
                       {QStringLiteral("code_challenge"), QString::fromLatin1(challenge)},
                       {QStringLiteral("scope"), Scopes},
                       {QStringLiteral("state"), state}}),
                 QUrl::StrictMode);
    return url;
}

QByteArray tokenRequestBody(const QString &clientId, const QString &code, const QByteArray &verifier)
{
    return form({{QStringLiteral("grant_type"), QStringLiteral("authorization_code")},
                 {QStringLiteral("code"), code},
                 {QStringLiteral("redirect_uri"), redirectUri()},
                 {QStringLiteral("client_id"), clientId},
                 {QStringLiteral("code_verifier"), QString::fromLatin1(verifier)}})
        .toLatin1();
}

QByteArray refreshRequestBody(const QString &clientId, const QString &refreshToken)
{
    return form({{QStringLiteral("grant_type"), QStringLiteral("refresh_token")},
                 {QStringLiteral("refresh_token"), refreshToken},
                 {QStringLiteral("client_id"), clientId}})
        .toLatin1();
}

Tokens parseTokens(const QByteArray &json)
{
    const QJsonObject o = object(json);
    return {o.value(QStringLiteral("access_token")).toString(), o.value(QStringLiteral("refresh_token")).toString(),
            o.value(QStringLiteral("expires_in")).toInt()};
}

ApiError parseError(const QByteArray &json)
{
    const QJsonObject o = object(json);
    const QJsonValue error = o.value(QStringLiteral("error"));
    if (error.isObject()) {
        const QJsonObject e = error.toObject();
        return {e.value(QStringLiteral("status")).toInt(), e.value(QStringLiteral("message")).toString(),
                e.value(QStringLiteral("reason")).toString()};
    }
    // The accounts service answers {"error": "invalid_grant", "error_description": ...}.
    return {0, o.value(QStringLiteral("error_description")).toString(error.toString()), error.toString()};
}

Page<Track> parseTracks(const QByteArray &json, TrackList layout, const QString &album)
{
    const QJsonObject paging = object(json);
    Page<Track> page;
    for (const QJsonValue &value : paging.value(QStringLiteral("items")).toArray()) {
        QJsonObject o = value.toObject();
        if (layout == TrackList::Saved)
            o = o.value(QStringLiteral("track")).toObject();
        else if (layout == TrackList::Playlist)
            o = (o.contains(QStringLiteral("item")) ? o.value(QStringLiteral("item")) : o.value(QStringLiteral("track")))
                    .toObject();
        if (std::optional<Track> track = parseTrack(o, album))
            page.items.append(*track);
    }
    page.next = paging.value(QStringLiteral("next")).toString();
    return page;
}

Page<Track> parseAlbumTracks(const QByteArray &json, QString *albumName)
{
    const QJsonObject album = object(json);
    *albumName = album.value(QStringLiteral("name")).toString();
    const QByteArray tracks = QJsonDocument(album.value(QStringLiteral("tracks")).toObject()).toJson();
    return parseTracks(tracks, TrackList::Plain, *albumName);
}

Page<Directory> parsePlaylists(const QByteArray &json)
{
    return parsePage<Directory>(object(json), playlistDirectory);
}

Page<Directory> parseSavedAlbums(const QByteArray &json)
{
    return parsePage<Directory>(object(json), [](const QJsonObject &o) {
        return albumDirectory(o.value(QStringLiteral("album")).toObject());
    });
}

Page<Directory> parseAlbums(const QByteArray &json)
{
    return parsePage<Directory>(object(json), albumDirectory);
}

Page<Directory> parseFollowedArtists(const QByteArray &json)
{
    return parsePage<Directory>(object(json).value(QStringLiteral("artists")).toObject(), artistDirectory);
}

SearchResults parseSearch(const QByteArray &json)
{
    const QJsonObject o = object(json);
    const auto section = [&o](const char *name) { return o.value(QLatin1String(name)).toObject(); };
    SearchResults results;
    results.artists = parsePage<Directory>(section("artists"), artistDirectory).items;
    results.albums = parsePage<Directory>(section("albums"), albumDirectory).items;
    results.playlists = parsePage<Directory>(section("playlists"), playlistDirectory).items;
    for (const QJsonValue &value : section("tracks").value(QStringLiteral("items")).toArray()) {
        if (std::optional<Track> track = parseTrack(value.toObject(), {}))
            results.tracks.append(*track);
    }
    return results;
}

QList<Device> parseDevices(const QByteArray &json)
{
    QList<Device> devices;
    for (const QJsonValue &value : object(json).value(QStringLiteral("devices")).toArray()) {
        const QJsonObject o = value.toObject();
        Device device;
        device.id = o.value(QStringLiteral("id")).toString();
        device.name = o.value(QStringLiteral("name")).toString();
        device.type = o.value(QStringLiteral("type")).toString();
        device.active = o.value(QStringLiteral("is_active")).toBool();
        device.restricted = o.value(QStringLiteral("is_restricted")).toBool();
        if (!device.id.isEmpty())
            devices.append(device);
    }
    return devices;
}

Playback parsePlayback(const QByteArray &json)
{
    Playback playback;
    const QJsonObject o = object(json);
    if (o.isEmpty())
        return playback;
    playback.valid = true;
    playback.playing = o.value(QStringLiteral("is_playing")).toBool();
    playback.progressMs = o.value(QStringLiteral("progress_ms")).toInteger();
    playback.deviceId = o.value(QStringLiteral("device")).toObject().value(QStringLiteral("id")).toString();
    if (std::optional<Track> track = parseTrack(o.value(QStringLiteral("item")).toObject(), {}))
        playback.track = *track;
    return playback;
}

QUrl trackLocation(const QString &trackId)
{
    return QUrl(QStringLiteral("spotify:track:") + trackId);
}

QString trackUri(const QUrl &location)
{
    const QString uri = location.toString();
    return uri.startsWith(QLatin1String("spotify:track:")) && uri.size() > 14 ? uri : QString();
}

namespace {
const char *const KindNames[] = {"playlists", "liked", "albums", "artists", "playlist", "album", "artist"};
}

QString folderId(FolderKind kind, const QString &id)
{
    return QLatin1String(KindNames[int(kind)]) + QLatin1Char(':') + id;
}

bool parseFolderId(const QString &folderId, FolderKind *kind, QString *id)
{
    const qsizetype colon = folderId.indexOf(QLatin1Char(':'));
    if (colon < 0)
        return false;
    const QStringView name = QStringView(folderId).left(colon);
    for (int i = 0; i < int(std::size(KindNames)); ++i) {
        if (name == QLatin1String(KindNames[i])) {
            *kind = FolderKind(i);
            *id = folderId.mid(colon + 1);
            return true;
        }
    }
    return false;
}

} // namespace newamp::spotify

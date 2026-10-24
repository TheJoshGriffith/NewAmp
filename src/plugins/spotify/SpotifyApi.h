#pragma once

#include "core/Track.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

// Pure helpers for the Spotify Web API: sign-in (OAuth with PKCE), JSON
// parsing and ids. Nothing here touches the network, so all of it is
// unit-tested with fixtures.
//
// Written for the API as Spotify serves it to apps in development mode since
// February 2026: playlist contents are "items" (only for playlists the user
// owns or collaborates on), and a search page holds at most 10 results.
namespace newamp::spotify {

inline constexpr const char *SourceId = "spotify";

// Spotify only accepts loopback redirects to 127.0.0.1 with a port that is
// registered in the app's dashboard, so the port is fixed.
inline constexpr quint16 RedirectPort = 43827;
QString redirectUri();

// PKCE: a random verifier kept by the app and its SHA-256 challenge, sent
// with the authorization request.
QByteArray makeCodeVerifier();
QByteArray codeChallenge(const QByteArray &verifier);

QUrl authorizeUrl(const QString &clientId, const QByteArray &challenge, const QString &state);
QByteArray tokenRequestBody(const QString &clientId, const QString &code, const QByteArray &verifier);
QByteArray refreshRequestBody(const QString &clientId, const QString &refreshToken);

struct Tokens
{
    QString accessToken;
    QString refreshToken; // empty when Spotify keeps the old one
    int expiresIn = 0;    // seconds
    bool isValid() const { return !accessToken.isEmpty(); }
};
Tokens parseTokens(const QByteArray &json);

// The "error" object of a failed request: status, message and, for player
// calls, a reason such as NO_ACTIVE_DEVICE or PREMIUM_REQUIRED.
struct ApiError
{
    int status = 0;
    QString message;
    QString reason;
};
ApiError parseError(const QByteArray &json);

// A browsable entry: a playlist, album or artist.
struct Directory
{
    QString id;
    QString title;
    QString subtitle;
};

// One page of a paged list. next is the absolute URL of the following page,
// empty on the last one.
template<typename T>
struct Page
{
    QList<T> items;
    QString next;
};

// How a page of tracks is laid out.
enum class TrackList {
    Plain,    // items are tracks (album tracks)
    Saved,    // items[].track (liked songs)
    Playlist, // items[].item, or items[].track before February 2026
};

// album names tracks whose objects don't carry one (album tracks). Local
// files, podcasts and unavailable entries are skipped.
Page<Track> parseTracks(const QByteArray &json, TrackList layout, const QString &album = {});
// A whole album (GET /albums/{id}): its name, and the first page of its
// tracks, which carry that name. Later pages parse as TrackList::Plain.
Page<Track> parseAlbumTracks(const QByteArray &json, QString *albumName);
Page<Directory> parsePlaylists(const QByteArray &json);
Page<Directory> parseSavedAlbums(const QByteArray &json);
Page<Directory> parseAlbums(const QByteArray &json); // an artist's albums
Page<Directory> parseFollowedArtists(const QByteArray &json);

struct SearchResults
{
    QList<Directory> artists;
    QList<Directory> albums;
    QList<Directory> playlists;
    QList<Track> tracks;
};
SearchResults parseSearch(const QByteArray &json);

struct Device
{
    QString id;
    QString name;
    QString type;
    bool active = false;
    bool restricted = false; // won't accept Web API commands
};
QList<Device> parseDevices(const QByteArray &json);
// The device to play on: the user's choice (preferredId) if it is open,
// else the Spotify app on this computer (a Computer named like hostName),
// else the active device, else any. Devices that refuse remote control are
// skipped. Null when there is none.
const Device *pickDevice(const QList<Device> &devices, const QString &preferredId, const QString &hostName);

// GET /me/player. An empty body (no playback anywhere) leaves valid false.
struct Playback
{
    bool valid = false;
    bool playing = false;
    qint64 progressMs = 0;
    QString deviceId;
    QString contextUri; // the playlist or album it plays from, if any
    Track track; // empty location when nothing (or an ad or episode) plays
};
Playback parsePlayback(const QByteArray &json);

// Tracks are stored as spotify:track:<id>, which is also what the player
// is asked to play.
QUrl trackLocation(const QString &trackId);
QString trackUri(const QUrl &location); // empty when it isn't a Spotify track

// A whole playlist as one playlist entry, spotify:playlist:<id>, which the
// device plays from start to end. For playlists the user follows but
// doesn't own, whose songs Spotify won't list to apps in development mode.
QUrl playlistLocation(const QString &playlistId);
// The URI to play for a stored location: a track or a whole playlist.
// Empty for anything else.
QString playableUri(const QUrl &location);
bool isPlaylistUri(const QString &uri);

// Media Library folder ids are "<kind>:<spotify id>".
enum class FolderKind { Playlists, LikedSongs, Albums, Artists, Playlist, Album, Artist };
QString folderId(FolderKind kind, const QString &id = {});
bool parseFolderId(const QString &folderId, FolderKind *kind, QString *id);

} // namespace newamp::spotify

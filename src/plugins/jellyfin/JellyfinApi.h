#pragma once

#include "core/MetadataEditor.h"
#include "core/Track.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

// Pure helpers for the Jellyfin web API: JSON parsing and URL building.
// Nothing here touches the network, so all of it is unit-tested with
// fixtures.
namespace newamp::jellyfin {

inline constexpr const char *SourceId = "jellyfin";

// Accepts what people type ("nas:8096", "music.example.com/jellyfin/") and
// returns a base URL without a trailing slash. Without a scheme it assumes
// http, and without a port as well, Jellyfin's default 8096. Empty when the
// text isn't a usable address.
QString normalizeServerUrl(const QString &text);

// The value of the Authorization header Jellyfin expects from clients. The
// token is left out before sign-in.
QByteArray authorizationHeader(const QString &deviceName, const QString &deviceId, const QString &token = {});

// From /System/Info/Public, which answers without signing in.
struct ServerInfo
{
    QString id;
    QString name;
    QString version;
};
// Returns an empty id when the response isn't from a Jellyfin server.
ServerInfo parseServerInfo(const QByteArray &json);

// A server that answered the "who is JellyfinServer?" broadcast on UDP 7359.
struct DiscoveredServer
{
    QString address;
    QString id;
    QString name;
};
inline constexpr quint16 DiscoveryPort = 7359;
QByteArray discoveryRequest();
// Returns an empty address for anything that isn't a reply.
DiscoveredServer parseDiscoveryReply(const QByteArray &json);

// The body for POST /Users/AuthenticateByName.
QByteArray authenticateBody(const QString &userName, const QString &password);

struct Session
{
    QString token;
    QString userId;
    QString userName;
    QString serverId;
};
// From /Users/AuthenticateByName; an empty token when sign-in failed.
Session parseAuthentication(const QByteArray &json);

struct User
{
    QString id;
    QString name;
};
// From /Users, for signing in with an API key: an API key belongs to no
// user, so NewAmp browses as one picked from the list.
QList<User> parseUsers(const QByteArray &json);
// The user named userName (ignoring case), else the first; null when none.
const User *pickUser(const QList<User> &users, const QString &userName);

// A browsable entry: a music library, artist, album or playlist.
struct Folder
{
    QString id;
    QString title;
    QString detail;
};

// The user's libraries that hold music (CollectionType "music"), from
// /UserViews.
QList<Folder> parseMusicViews(const QByteArray &json);
// Any { "Items": [...] } of artists, albums or playlists. Albums show their
// year, playlists their track count.
QList<Folder> parseFolders(const QByteArray &json);
// Audio items with the file type they're served as. Entries without an id
// are skipped.
QList<Track> parseTracks(const QByteArray &json, const QString &serverId);

// What a Jellyfin track's location encodes:
// jellyfin://<serverId>/<itemId>?container=<file type>
struct TrackRef
{
    QString serverId;
    QString itemId;
    QString container;
    bool isValid() const { return !serverId.isEmpty() && !itemId.isEmpty(); }
};

QUrl trackLocation(const TrackRef &ref);
TrackRef parseTrackLocation(const QUrl &location);

// The original file, untouched. The path ends in stream.<container> so the
// player picks its decoder from the suffix, as it does for files.
QUrl streamUrl(const QString &serverUrl, const TrackRef &ref, const QString &token, const QString &deviceId);
// The server's conversion of the track to MP3, for files NewAmp can't play.
QUrl transcodeUrl(const QString &serverUrl, const TrackRef &ref, const QString &token, const QString &deviceId);

// Server paths (with query) for browsing. userId scopes each to what the
// user may see.
QString viewsPath(const QString &userId);
QString artistsPath(const QString &userId, const QString &libraryId);
QString albumsPath(const QString &userId, const QString &artistId);
QString albumTracksPath(const QString &userId, const QString &albumId);
QString artistTracksPath(const QString &userId, const QString &artistId);
QString playlistsPath(const QString &userId);
QString playlistTracksPath(const QString &userId, const QString &playlistId);

// Search: albums, playlists and tracks from /Items, artists from /Artists
// (which knows every artist, not only album artists). An empty libraryId
// searches everything.
QString searchPath(const QString &userId, const QString &query, const QString &libraryId);
QString artistSearchPath(const QString &userId, const QString &query, const QString &libraryId);

struct SearchResults
{
    QList<Folder> artists;
    QList<Folder> albums; // titled "<album artist> - <album>"
    QList<Folder> playlists;
    QList<Track> tracks;
};
SearchResults parseSearch(const QByteArray &json, const QString &serverId);

// Bodies for playback reports: POST /Sessions/Playing, then
// /Sessions/Playing/Progress, then /Sessions/Playing/Stopped. The server
// keeps "now playing" and marks a track played when it stops near the end.
QByteArray playingBody(const QString &itemId, qint64 positionMs, bool paused);
QByteArray progressBody(const QString &itemId, qint64 positionMs, bool paused, const QString &event);
QByteArray stoppedBody(const QString &itemId, qint64 positionMs);

// File Info. GET itemPath() returns the whole item, which is edited and
// POSTed back whole to updateItemPath(): Jellyfin replaces the item with
// what it is sent, so any field left out would be cleared.
QString itemPath(const QString &userId, const QString &itemId);
QString updateItemPath(const QString &itemId);
// The item's tags and details. Comments aren't kept by Jellyfin, so COMMENT
// isn't offered. canEdit and note are left to the caller.
TrackInfo parseItemInfo(const QByteArray &json);
// The item with the changes applied, for updateItemPath(). Empty when json
// isn't an item.
QByteArray editedItem(const QByteArray &json, const QList<TagField> &changes);
// From /Users/Me: only administrators may edit items.
bool parseIsAdministrator(const QByteArray &json);

// Media Library folder ids are "<kind>|<item id>".
enum class FolderKind { Library, Artist, Album, Playlists, Playlist };
QString folderId(FolderKind kind, const QString &itemId = {});
bool parseFolderId(const QString &id, FolderKind *kind, QString *itemId);

} // namespace newamp::jellyfin

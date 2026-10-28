#pragma once

#include "core/MetadataEditor.h"
#include "core/Track.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QUrl>

// Pure helpers for the Plex web API: JSON parsing and URL building. Nothing
// here touches the network, so all of it is unit-tested with fixtures.
namespace newamp::plex {

inline constexpr const char *SourceId = "plex";

// A sign-in PIN from plex.tv. authToken is filled in once the user approves.
struct Pin
{
    qint64 id = 0;
    QString code;
    QString authToken;
};

// Returns a Pin with id 0 when the response isn't a valid PIN.
Pin parsePin(const QByteArray &json);
// The page where the user approves a PIN.
QUrl authUrl(const QString &clientId, const QString &code);

struct Connection
{
    QString uri;
    bool local = false;
    bool relay = false;
};

// A Plex Media Server the account can reach.
struct Server
{
    QString name;
    QString machineIdentifier;
    QString accessToken;
    bool owned = true; // false for servers shared with this account
    QList<Connection> connections;
};

// Keeps only resources that provide "server".
QList<Server> parseResources(const QByteArray &json);
// Connections in the order to try them: local, then remote, then relay
// (https first within each group).
QList<Connection> orderedConnections(const Server &server);

// A browsable entry: a library section, artist, album or playlist. key is
// the section id for sections and the ratingKey for everything else.
struct Directory
{
    QString key;
    QString title;
    QString subtitle;
    QString parentTitle; // an album's artist
};

// Music library sections only (type "artist").
QList<Directory> parseSections(const QByteArray &json);
// Any MediaContainer of directories: artists, albums or playlists.
QList<Directory> parseDirectories(const QByteArray &json);
// Tracks in a MediaContainer (album children, playlist items, allLeaves).
// Entries without a playable part are skipped.
QList<Track> parseTracks(const QByteArray &json, const QString &machineIdentifier);

// What a search found, music only.
struct SearchResults
{
    QList<Directory> artists;
    QList<Directory> albums;
    QList<Directory> playlists;
    QList<Track> tracks;
};

// Server path for a search, within one library section when sectionKey is
// set and across the whole server otherwise.
QString searchPath(const QString &query, const QString &sectionKey = {});
// The hubs a search answers with; hubs of video, photos and the like, and
// playlists that aren't audio, are skipped.
SearchResults parseSearch(const QByteArray &json, const QString &machineIdentifier);

// What a Plex track's location encodes: plex://<machineIdentifier><partKey>?ratingKey=<key>
struct TrackRef
{
    QString machineIdentifier;
    QString partKey;
    QString ratingKey;
    bool isValid() const { return !machineIdentifier.isEmpty() && !partKey.isEmpty(); }
};

QUrl trackLocation(const TrackRef &ref);
TrackRef parseTrackLocation(const QUrl &location);

// Direct-play URL: <connection uri><part key>, with the client's details,
// the formats NewAmp plays and the token in the query.
QUrl streamUrl(const QString &connectionUri, const QString &partKey, const QString &token, const QString &clientId);

// The server's own conversion of a track to an HLS stream, which plays even
// when the direct file can't be served. Plex picks the media version and
// the output format; session names this playback on the server.
QUrl transcodeUrl(const QString &connectionUri, const QString &ratingKey, const QString &token,
                  const QString &clientId, const QString &session);

// Server paths (with query) for playback reporting.
QString timelinePath(const QString &ratingKey, const QString &state, qint64 timeMs, qint64 durationMs);
QString scrobblePath(const QString &ratingKey);

// File Info. metadataPath() gives a track's full details; edits are PUT to
// the paths editPaths() returns. Plex stores them in its database, never in
// the file, and locks each edited field so a rescan keeps it.
QString metadataPath(const QString &ratingKey);

// Where a track's edits go: the track itself, and its album and artist,
// which hold the album title.
struct EditTarget
{
    QString sectionId;
    QString ratingKey;
    QString albumRatingKey;
    QString artistRatingKey;
};

// The track's tags and details from metadataPath(), and where edits to it
// go. The album title applies to the whole album; album artist, year and
// genre are read-only, since editing them renames the artist everywhere or
// needs whole tag lists; comments aren't kept. canEdit and note are left to
// the caller.
TrackInfo parseTrackInfo(const QByteArray &json, EditTarget *target);
// PUT paths (with query) for the changes: one for the track, and one for
// its album when the album title changed.
QStringList editPaths(const EditTarget &target, const QList<TagField> &changes);

// Media Library folder ids are "<server>|<kind>|<key>", so the library
// needs no notion of a current server.
enum class FolderKind { Server, Section, Artist, Album, Playlists, Playlist };
QString folderId(const QString &machineIdentifier, FolderKind kind, const QString &key = {});
bool parseFolderId(const QString &id, QString *machineIdentifier, FolderKind *kind, QString *key);

} // namespace newamp::plex

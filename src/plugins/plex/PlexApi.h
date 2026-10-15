#pragma once

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
};

// Music library sections only (type "artist").
QList<Directory> parseSections(const QByteArray &json);
// Any MediaContainer of directories: artists, albums or playlists.
QList<Directory> parseDirectories(const QByteArray &json);
// Tracks in a MediaContainer (album children, playlist items, allLeaves).
// Entries without a playable part are skipped.
QList<Track> parseTracks(const QByteArray &json, const QString &machineIdentifier);

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

// Direct-play URL: <connection uri><part key>?X-Plex-Token=<token>.
QUrl streamUrl(const QString &connectionUri, const QString &partKey, const QString &token);

// Server paths (with query) for playback reporting.
QString timelinePath(const QString &ratingKey, const QString &state, qint64 timeMs, qint64 durationMs);
QString scrobblePath(const QString &ratingKey);

} // namespace newamp::plex

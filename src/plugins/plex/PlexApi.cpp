#include "PlexApi.h"

#include <QCoreApplication>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QUrlQuery>

#include <algorithm>
#include <iterator>

namespace newamp::plex {

namespace {
// Plex sends ids as strings in some places and numbers in others.
QString text(const QJsonValue &value)
{
    if (value.isDouble())
        return QString::number(qint64(value.toDouble()));
    return value.toString();
}

QJsonObject object(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

// The entries of a MediaContainer, whichever key holds them.
QJsonArray containerItems(const QByteArray &json)
{
    const QJsonObject container = object(json).value(QStringLiteral("MediaContainer")).toObject();
    QJsonArray items = container.value(QStringLiteral("Metadata")).toArray();
    if (items.isEmpty())
        items = container.value(QStringLiteral("Directory")).toArray();
    return items;
}
} // namespace

Pin parsePin(const QByteArray &json)
{
    const QJsonObject o = object(json);
    Pin pin;
    pin.id = qint64(o.value(QStringLiteral("id")).toDouble());
    pin.code = o.value(QStringLiteral("code")).toString();
    pin.authToken = o.value(QStringLiteral("authToken")).toString(); // null until approved
    if (pin.code.isEmpty())
        pin.id = 0;
    return pin;
}

QUrl authUrl(const QString &clientId, const QString &code)
{
    // The parameters live in the fragment, so build it by hand.
    QUrl url(QStringLiteral("https://app.plex.tv/auth"));
    url.setFragment(QStringLiteral("?clientID=%1&code=%2&context%5Bdevice%5D%5Bproduct%5D=NewAmp")
                        .arg(QString::fromLatin1(QUrl::toPercentEncoding(clientId)),
                             QString::fromLatin1(QUrl::toPercentEncoding(code))),
                    QUrl::StrictMode);
    return url;
}

QList<Server> parseResources(const QByteArray &json)
{
    QList<Server> servers;
    const QJsonArray resources = QJsonDocument::fromJson(json).array();
    for (const QJsonValue &value : resources) {
        const QJsonObject o = value.toObject();
        const QStringList provides = o.value(QStringLiteral("provides")).toString().split(QLatin1Char(','));
        if (!provides.contains(QStringLiteral("server")))
            continue;
        Server server;
        server.name = o.value(QStringLiteral("name")).toString();
        server.machineIdentifier = o.value(QStringLiteral("clientIdentifier")).toString();
        server.accessToken = o.value(QStringLiteral("accessToken")).toString();
        server.owned = o.value(QStringLiteral("owned")).toBool(true);
        for (const QJsonValue &c : o.value(QStringLiteral("connections")).toArray()) {
            const QJsonObject co = c.toObject();
            Connection connection;
            connection.uri = co.value(QStringLiteral("uri")).toString();
            connection.local = co.value(QStringLiteral("local")).toBool();
            connection.relay = co.value(QStringLiteral("relay")).toBool();
            if (!connection.uri.isEmpty())
                server.connections << connection;
        }
        if (!server.machineIdentifier.isEmpty() && !server.connections.isEmpty())
            servers << server;
    }
    return servers;
}

QList<Connection> orderedConnections(const Server &server)
{
    auto rank = [](const Connection &c) {
        const int group = c.relay ? 2 : (c.local ? 0 : 1);
        const bool https = c.uri.startsWith(QStringLiteral("https:"));
        return group * 2 + (https ? 0 : 1);
    };
    QList<Connection> ordered = server.connections;
    std::stable_sort(ordered.begin(), ordered.end(),
                     [&](const Connection &a, const Connection &b) { return rank(a) < rank(b); });
    return ordered;
}

QList<Directory> parseSections(const QByteArray &json)
{
    QList<Directory> sections;
    for (const QJsonValue &value : containerItems(json)) {
        const QJsonObject o = value.toObject();
        if (o.value(QStringLiteral("type")).toString() != QLatin1String("artist"))
            continue;
        sections << Directory{text(o.value(QStringLiteral("key"))), o.value(QStringLiteral("title")).toString(), {}};
    }
    return sections;
}

namespace {

// Returns false for entries without a ratingKey.
bool parseDirectory(const QJsonObject &o, Directory *directory)
{
    directory->key = text(o.value(QStringLiteral("ratingKey")));
    directory->title = o.value(QStringLiteral("title")).toString();
    directory->parentTitle = o.value(QStringLiteral("parentTitle")).toString();
    if (const int year = o.value(QStringLiteral("year")).toInt())
        directory->subtitle = QString::number(year);
    else if (o.contains(QStringLiteral("leafCount")))
        directory->subtitle = QStringLiteral("%1 tracks").arg(o.value(QStringLiteral("leafCount")).toInt());
    return !directory->key.isEmpty();
}

// Returns false for entries without a playable part.
bool parseTrack(const QJsonObject &o, const QString &machineIdentifier, Track *out)
{
    const QJsonObject media = o.value(QStringLiteral("Media")).toArray().first().toObject();
    const QJsonObject part = media.value(QStringLiteral("Part")).toArray().first().toObject();
    TrackRef ref{machineIdentifier, part.value(QStringLiteral("key")).toString(),
                 text(o.value(QStringLiteral("ratingKey")))};
    if (!ref.isValid())
        return false;
    Track &track = *out;
    track.sourceId = QString::fromLatin1(SourceId);
    track.location = trackLocation(ref);
    track.title = o.value(QStringLiteral("title")).toString();
    // originalTitle is the track artist when it differs from the album's.
    track.artist = o.value(QStringLiteral("originalTitle")).toString();
    if (track.artist.isEmpty())
        track.artist = o.value(QStringLiteral("grandparentTitle")).toString();
    track.album = o.value(QStringLiteral("parentTitle")).toString();
    const qint64 duration = qint64(o.value(QStringLiteral("duration")).toDouble());
    track.durationMs = duration > 0 ? duration : -1;
    return true;
}

} // namespace

QList<Directory> parseDirectories(const QByteArray &json)
{
    QList<Directory> directories;
    for (const QJsonValue &value : containerItems(json)) {
        Directory directory;
        if (parseDirectory(value.toObject(), &directory))
            directories << directory;
    }
    return directories;
}

QList<Track> parseTracks(const QByteArray &json, const QString &machineIdentifier)
{
    QList<Track> tracks;
    for (const QJsonValue &value : containerItems(json)) {
        Track track;
        if (parseTrack(value.toObject(), machineIdentifier, &track))
            tracks << track;
    }
    return tracks;
}

QString searchPath(const QString &query, const QString &sectionKey)
{
    QUrlQuery q;
    q.addQueryItem(QStringLiteral("query"), QString::fromLatin1(QUrl::toPercentEncoding(query)));
    q.addQueryItem(QStringLiteral("limit"), QStringLiteral("20"));
    if (!sectionKey.isEmpty())
        q.addQueryItem(QStringLiteral("sectionId"), sectionKey);
    return QStringLiteral("/hubs/search?") + q.toString(QUrl::FullyEncoded);
}

SearchResults parseSearch(const QByteArray &json, const QString &machineIdentifier)
{
    SearchResults results;
    const QJsonObject container = object(json).value(QStringLiteral("MediaContainer")).toObject();
    for (const QJsonValue &hubValue : container.value(QStringLiteral("Hub")).toArray()) {
        const QJsonObject hub = hubValue.toObject();
        const QString type = hub.value(QStringLiteral("type")).toString();
        QJsonArray entries = hub.value(QStringLiteral("Metadata")).toArray();
        if (entries.isEmpty())
            entries = hub.value(QStringLiteral("Directory")).toArray();
        for (const QJsonValue &value : std::as_const(entries)) {
            const QJsonObject o = value.toObject();
            Directory directory;
            Track track;
            if (type == QLatin1String("track")) {
                if (parseTrack(o, machineIdentifier, &track))
                    results.tracks << track;
            } else if (type == QLatin1String("artist")) {
                if (parseDirectory(o, &directory))
                    results.artists << directory;
            } else if (type == QLatin1String("album")) {
                if (parseDirectory(o, &directory))
                    results.albums << directory;
            } else if (type == QLatin1String("playlist")
                       && o.value(QStringLiteral("playlistType")).toString() == QLatin1String("audio")) {
                if (parseDirectory(o, &directory))
                    results.playlists << directory;
            }
        }
    }
    return results;
}

QUrl trackLocation(const TrackRef &ref)
{
    QUrl url;
    url.setScheme(QStringLiteral("plex"));
    url.setHost(ref.machineIdentifier);
    url.setPath(ref.partKey);
    if (!ref.ratingKey.isEmpty())
        url.setQuery(QStringLiteral("ratingKey=") + ref.ratingKey);
    return url;
}

TrackRef parseTrackLocation(const QUrl &location)
{
    if (location.scheme() != QLatin1String("plex"))
        return {};
    return {location.host(), location.path(QUrl::FullyEncoded),
            QUrlQuery(location).queryItemValue(QStringLiteral("ratingKey"))};
}

namespace {

// What NewAmp plays, in Plex's client-profile terms. Without it the server
// falls back to a generic profile that, for one, has no FLAC: it then
// refuses the file with a 500 rather than sending it.
const QString ProfileExtra = QStringLiteral(
    "add-direct-play-profile(type=musicProfile&protocol=http"
    "&container=mp3,flac,ogg,wav,mp4,m4a,aac&audioCodec=mp3,flac,vorbis,opus,pcm,aac,alac)"
    "+add-transcode-target(type=musicProfile&context=streaming&protocol=hls&container=mpegts&audioCodec=aac)");

QString trimmedBase(const QString &connectionUri)
{
    QString base = connectionUri;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    return base;
}

// Players can't send headers, so the client details ride in the query,
// every value fully percent-encoded, as Plex's own clients send them.
QUrl withClientQuery(const QString &url, QList<QPair<QString, QString>> items, const QString &clientId,
                     const QString &token)
{
    items += {
        {QStringLiteral("X-Plex-Client-Identifier"), clientId},
        {QStringLiteral("X-Plex-Product"), QStringLiteral("NewAmp")},
        {QStringLiteral("X-Plex-Platform"), QStringLiteral("Chrome")},
        {QStringLiteral("X-Plex-Client-Profile-Extra"), ProfileExtra},
        {QStringLiteral("X-Plex-Token"), token},
    };
    QStringList pairs;
    for (const auto &[key, value] : std::as_const(items))
        pairs << key + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value));
    QUrl result(url);
    result.setQuery(pairs.join(QLatin1Char('&')), QUrl::StrictMode);
    return result;
}

} // namespace

QUrl streamUrl(const QString &connectionUri, const QString &partKey, const QString &token, const QString &clientId)
{
    return withClientQuery(trimmedBase(connectionUri) + partKey, {}, clientId, token);
}

QUrl transcodeUrl(const QString &connectionUri, const QString &ratingKey, const QString &token,
                  const QString &clientId, const QString &session)
{
    return withClientQuery(trimmedBase(connectionUri) + QStringLiteral("/music/:/transcode/universal/start.m3u8"),
                           {
                               {QStringLiteral("path"), QStringLiteral("/library/metadata/") + ratingKey},
                               {QStringLiteral("protocol"), QStringLiteral("hls")},
                               {QStringLiteral("directPlay"), QStringLiteral("0")},
                               {QStringLiteral("directStream"), QStringLiteral("0")},
                               {QStringLiteral("hasMDE"), QStringLiteral("1")},
                               {QStringLiteral("session"), session},
                           },
                           clientId, token);
}

QString timelinePath(const QString &ratingKey, const QString &state, qint64 timeMs, qint64 durationMs)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("ratingKey"), ratingKey);
    query.addQueryItem(QStringLiteral("key"), QStringLiteral("/library/metadata/") + ratingKey);
    query.addQueryItem(QStringLiteral("state"), state);
    query.addQueryItem(QStringLiteral("time"), QString::number(qMax<qint64>(0, timeMs)));
    query.addQueryItem(QStringLiteral("duration"), QString::number(qMax<qint64>(0, durationMs)));
    return QStringLiteral("/:/timeline?") + query.toString(QUrl::FullyEncoded);
}

QString scrobblePath(const QString &ratingKey)
{
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("key"), ratingKey);
    query.addQueryItem(QStringLiteral("identifier"), QStringLiteral("com.plexapp.plugins.library"));
    return QStringLiteral("/:/scrobble?") + query.toString(QUrl::FullyEncoded);
}

QString metadataPath(const QString &ratingKey)
{
    return QStringLiteral("/library/metadata/") + ratingKey;
}

TrackInfo parseTrackInfo(const QByteArray &json, EditTarget *target)
{
    const auto tr = [](const char *text) { return QCoreApplication::translate("Plex", text); };
    TrackInfo info;
    const QJsonArray items = containerItems(json);
    const QJsonObject o = items.first().toObject();
    if (text(o.value(QStringLiteral("ratingKey"))).isEmpty()) {
        info.error = tr("Plex sent no details for this track.");
        return info;
    }
    *target = {text(o.value(QStringLiteral("librarySectionID"))), text(o.value(QStringLiteral("ratingKey"))),
               text(o.value(QStringLiteral("parentRatingKey"))), text(o.value(QStringLiteral("grandparentRatingKey")))};
    if (target->sectionId.isEmpty())
        target->sectionId = text(object(json).value(QStringLiteral("MediaContainer")).toObject().value(QStringLiteral("librarySectionID")));

    const auto str = [&](const char *key) { return o.value(QLatin1String(key)).toString(); };
    const auto num = [&](const char *key) {
        const QJsonValue v = o.value(QLatin1String(key));
        return v.isDouble() ? QString::number(v.toInt()) : QString();
    };
    QString artist = str("originalTitle");
    if (artist.isEmpty())
        artist = str("grandparentTitle");
    QStringList genres;
    for (const QJsonValue &genre : o.value(QStringLiteral("Genre")).toArray())
        genres << genre.toObject().value(QStringLiteral("tag")).toString();
    const QString year = o.value(QStringLiteral("parentYear")).isDouble() ? QString::number(o.value(QStringLiteral("parentYear")).toInt())
                                                                          : num("year");
    info.tags = {
        {QStringLiteral("TITLE"), str("title")},
        {QStringLiteral("ARTIST"), artist},
        {QStringLiteral("ALBUM"), str("parentTitle"), true},
        {QStringLiteral("ALBUMARTIST"), str("grandparentTitle"), false, true},
        {QStringLiteral("TRACKNUMBER"), num("index")},
        {QStringLiteral("DISCNUMBER"), num("parentIndex")},
        {QStringLiteral("DATE"), year, false, true},
        {QStringLiteral("GENRE"), genres.join(QStringLiteral("; ")), false, true},
    };
    if (target->albumRatingKey.isEmpty())
        info.tags[2].readOnly = true;

    const QJsonObject media = o.value(QStringLiteral("Media")).toArray().first().toObject();
    const QJsonObject part = media.value(QStringLiteral("Part")).toArray().first().toObject();
    QJsonObject audio;
    for (const QJsonValue &stream : part.value(QStringLiteral("Stream")).toArray())
        if (stream.toObject().value(QStringLiteral("streamType")).toInt() == 2) {
            audio = stream.toObject();
            break;
        }
    const QString container = media.value(QStringLiteral("container")).toString().toUpper();
    const QString codec = media.value(QStringLiteral("audioCodec")).toString().toUpper();
    if (!container.isEmpty() || !codec.isEmpty())
        info.details.append({tr("Format"), codec.isEmpty() || codec == container || container.isEmpty()
                                               ? (container.isEmpty() ? codec : container)
                                               : QStringLiteral("%1 (%2)").arg(codec, container)});
    const qint64 ms = qint64(o.value(QStringLiteral("duration")).toDouble());
    if (ms > 0) {
        const qint64 s = (ms + 500) / 1000;
        info.details.append({tr("Length"), QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'))});
    }
    if (const int bitrate = media.value(QStringLiteral("bitrate")).toInt(); bitrate > 0)
        info.details.append({tr("Bitrate"), tr("%1 kbps").arg(bitrate)});
    if (const int rate = audio.value(QStringLiteral("samplingRate")).toInt(); rate > 0)
        info.details.append({tr("Sample rate"), tr("%1 Hz").arg(rate)});
    const int channels = media.value(QStringLiteral("audioChannels")).toInt(audio.value(QStringLiteral("channels")).toInt());
    if (channels > 0)
        info.details.append({tr("Channels"), channels == 1 ? tr("Mono") : channels == 2 ? tr("Stereo") : QString::number(channels)});
    if (const qint64 size = qint64(part.value(QStringLiteral("size")).toDouble()); size > 0)
        info.details.append({tr("Size"), QLocale().formattedDataSize(size)});
    if (const QString file = part.value(QStringLiteral("file")).toString(); !file.isEmpty())
        info.details.append({tr("Location"), file});
    return info;
}

namespace {

// "3/12" -> "3"; empty when there's no number.
QString leadingNumber(const QString &value)
{
    QString digits;
    for (const QChar c : value.trimmed()) {
        if (!c.isDigit())
            break;
        digits += c;
    }
    return digits;
}

// type and id, then field.value and field.locked for each edit, every value
// fully percent-encoded.
QString editPath(const QString &sectionId, int type, const QString &id, const QList<QPair<QString, QString>> &fields)
{
    QStringList query = {QStringLiteral("type=%1").arg(type), QStringLiteral("id=") + id};
    for (const auto &[name, value] : fields)
        query << name + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value));
    return QStringLiteral("/library/sections/%1/all?").arg(sectionId) + query.join(QLatin1Char('&'));
}

} // namespace

QStringList editPaths(const EditTarget &target, const QList<TagField> &changes)
{
    QList<QPair<QString, QString>> track;
    QList<QPair<QString, QString>> album;
    const auto edit = [](QList<QPair<QString, QString>> &fields, const QString &field, const QString &value) {
        fields.append({field + QStringLiteral(".value"), value});
        fields.append({field + QStringLiteral(".locked"), QStringLiteral("1")});
    };
    for (const TagField &change : changes) {
        const QString value = change.value.trimmed();
        if (change.key == QLatin1String("TITLE"))
            edit(track, QStringLiteral("title"), value);
        else if (change.key == QLatin1String("ARTIST"))
            edit(track, QStringLiteral("originalTitle"), value);
        else if (change.key == QLatin1String("TRACKNUMBER"))
            edit(track, QStringLiteral("index"), leadingNumber(value));
        else if (change.key == QLatin1String("DISCNUMBER"))
            edit(track, QStringLiteral("parentIndex"), leadingNumber(value));
        else if (change.key == QLatin1String("ALBUM") && !target.albumRatingKey.isEmpty()) {
            edit(album, QStringLiteral("title"), value);
            // Without the artist Plex can move a renamed album to another.
            if (!target.artistRatingKey.isEmpty())
                album.append({QStringLiteral("artist.id.value"), target.artistRatingKey});
        }
    }
    QStringList paths;
    if (!track.isEmpty())
        paths << editPath(target.sectionId, 10, target.ratingKey, track);
    if (!album.isEmpty())
        paths << editPath(target.sectionId, 9, target.albumRatingKey, album);
    return paths;
}

namespace {
const char *const FolderKindNames[] = {"server", "section", "artist", "album", "playlists", "playlist"};
}

QString folderId(const QString &machineIdentifier, FolderKind kind, const QString &key)
{
    return QStringLiteral("%1|%2|%3").arg(machineIdentifier, QLatin1String(FolderKindNames[int(kind)]), key);
}

bool parseFolderId(const QString &id, QString *machineIdentifier, FolderKind *kind, QString *key)
{
    const QStringList parts = id.split(QLatin1Char('|'));
    if (parts.size() != 3)
        return false;
    for (int i = 0; i < int(std::size(FolderKindNames)); ++i) {
        if (parts[1] == QLatin1String(FolderKindNames[i])) {
            *machineIdentifier = parts[0];
            *kind = FolderKind(i);
            *key = parts[2];
            return true;
        }
    }
    return false;
}

} // namespace newamp::plex

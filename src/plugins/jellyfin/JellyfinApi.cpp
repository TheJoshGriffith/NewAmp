#include "JellyfinApi.h"

#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <iterator>

namespace newamp::jellyfin {

namespace {
constexpr qint64 TicksPerMs = 10000; // Jellyfin times are in 100 ns ticks
constexpr quint16 DefaultPort = 8096;

QJsonObject object(const QByteArray &json)
{
    return QJsonDocument::fromJson(json).object();
}

QJsonArray items(const QByteArray &json)
{
    const QJsonDocument document = QJsonDocument::fromJson(json);
    if (document.isArray())
        return document.array();
    return document.object().value(QStringLiteral("Items")).toArray();
}

QString str(const QJsonObject &o, const char *key)
{
    return o.value(QLatin1String(key)).toString();
}

// Builds "<path>?k=v&..." with every value fully percent-encoded: ASP.NET
// reads a bare '+' as a space, so QUrlQuery's lenient encoding won't do.
QString withQuery(const QString &path, const QList<QPair<QString, QString>> &query)
{
    QStringList pairs;
    for (const auto &[key, value] : query) {
        if (!value.isEmpty())
            pairs << key + QLatin1Char('=') + QString::fromLatin1(QUrl::toPercentEncoding(value));
    }
    return pairs.isEmpty() ? path : path + QLatin1Char('?') + pairs.join(QLatin1Char('&'));
}

// The file type a track is served as, which picks NewAmp's decoder. The
// file's own suffix is best; Jellyfin's container names come from FFmpeg
// and can be a list ("mov,mp4,m4a,3gp,3g2,mj2").
QString containerOf(const QJsonObject &item)
{
    const QJsonObject source = item.value(QStringLiteral("MediaSources")).toArray().first().toObject();
    for (const QString &path : {str(item, "Path"), str(source, "Path")}) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (!suffix.isEmpty() && suffix.size() <= 5)
            return suffix;
    }
    QString container = str(item, "Container");
    if (container.isEmpty())
        container = str(source, "Container");
    const QStringList names = container.toLower().split(QLatin1Char(','), Qt::SkipEmptyParts);
    if (names.contains(QStringLiteral("m4a")))
        return QStringLiteral("m4a");
    return names.value(0);
}

QUrl serverUrl(const QString &base, const QString &pathAndQuery)
{
    return QUrl(base + pathAndQuery, QUrl::StrictMode);
}
} // namespace

QString normalizeServerUrl(const QString &text)
{
    QString input = text.trimmed();
    if (input.isEmpty())
        return {};
    const bool hasScheme = input.contains(QStringLiteral("://"));
    if (!hasScheme)
        input.prepend(QStringLiteral("http://"));
    QUrl url(input, QUrl::TolerantMode);
    if (!url.isValid() || url.host().isEmpty()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return {};
    if (!hasScheme && url.port() == -1)
        url.setPort(DefaultPort);
    url = url.adjusted(QUrl::RemoveQuery | QUrl::RemoveFragment | QUrl::RemoveUserInfo);
    QString result = url.toString(QUrl::FullyEncoded);
    while (result.endsWith(QLatin1Char('/')))
        result.chop(1);
    return result;
}

QByteArray authorizationHeader(const QString &deviceName, const QString &deviceId, const QString &token)
{
    // Values are quoted, so the free-text device name is percent-encoded.
    QByteArray header = "MediaBrowser Client=\"NewAmp\", Device=\"" + QUrl::toPercentEncoding(deviceName)
        + "\", DeviceId=\"" + QUrl::toPercentEncoding(deviceId) + "\", Version=\"" NEWAMP_VERSION "\"";
    if (!token.isEmpty())
        header += ", Token=\"" + QUrl::toPercentEncoding(token) + '"';
    return header;
}

ServerInfo parseServerInfo(const QByteArray &json)
{
    const QJsonObject o = object(json);
    return {str(o, "Id"), str(o, "ServerName"), str(o, "Version")};
}

QByteArray discoveryRequest()
{
    return QByteArrayLiteral("who is JellyfinServer?");
}

DiscoveredServer parseDiscoveryReply(const QByteArray &json)
{
    const QJsonObject o = object(json);
    DiscoveredServer server{normalizeServerUrl(str(o, "Address")), str(o, "Id"), str(o, "Name")};
    if (server.id.isEmpty())
        server.address.clear();
    return server;
}

QByteArray authenticateBody(const QString &userName, const QString &password)
{
    return QJsonDocument(QJsonObject{{QStringLiteral("Username"), userName}, {QStringLiteral("Pw"), password}})
        .toJson(QJsonDocument::Compact);
}

Session parseAuthentication(const QByteArray &json)
{
    const QJsonObject o = object(json);
    const QJsonObject user = o.value(QStringLiteral("User")).toObject();
    Session session{str(o, "AccessToken"), str(user, "Id"), str(user, "Name"), str(o, "ServerId")};
    if (session.serverId.isEmpty())
        session.serverId = str(user, "ServerId");
    if (session.userId.isEmpty())
        session.token.clear();
    return session;
}

QList<User> parseUsers(const QByteArray &json)
{
    QList<User> users;
    for (const QJsonValue &value : items(json)) {
        const QJsonObject o = value.toObject();
        if (!str(o, "Id").isEmpty())
            users << User{str(o, "Id"), str(o, "Name")};
    }
    return users;
}

const User *pickUser(const QList<User> &users, const QString &userName)
{
    for (const User &user : users) {
        if (user.name.compare(userName.trimmed(), Qt::CaseInsensitive) == 0)
            return &user;
    }
    return users.isEmpty() ? nullptr : &users.first();
}

QList<Folder> parseMusicViews(const QByteArray &json)
{
    QList<Folder> views;
    for (const QJsonValue &value : items(json)) {
        const QJsonObject o = value.toObject();
        if (str(o, "CollectionType") == QLatin1String("music") && !str(o, "Id").isEmpty())
            views << Folder{str(o, "Id"), str(o, "Name"), {}};
    }
    return views;
}

QList<Folder> parseFolders(const QByteArray &json)
{
    QList<Folder> folders;
    for (const QJsonValue &value : items(json)) {
        const QJsonObject o = value.toObject();
        // Playlists of videos share the endpoint with music ones.
        if (str(o, "Type") == QLatin1String("Playlist") && str(o, "MediaType") == QLatin1String("Video"))
            continue;
        Folder folder{str(o, "Id"), str(o, "Name"), {}};
        if (const int year = o.value(QStringLiteral("ProductionYear")).toInt())
            folder.detail = QString::number(year);
        else if (o.contains(QStringLiteral("ChildCount")))
            folder.detail = QStringLiteral("%1 tracks").arg(o.value(QStringLiteral("ChildCount")).toInt());
        if (!folder.id.isEmpty())
            folders << folder;
    }
    return folders;
}

QList<Track> parseTracks(const QByteArray &json, const QString &serverId)
{
    QList<Track> tracks;
    for (const QJsonValue &value : items(json)) {
        const QJsonObject o = value.toObject();
        const TrackRef ref{serverId, str(o, "Id"), containerOf(o)};
        if (!ref.isValid())
            continue;
        Track track;
        track.sourceId = QString::fromLatin1(SourceId);
        track.location = trackLocation(ref);
        track.title = str(o, "Name");
        track.artist = o.value(QStringLiteral("Artists")).toArray().first().toString();
        if (track.artist.isEmpty())
            track.artist = str(o, "AlbumArtist");
        track.album = str(o, "Album");
        const qint64 ticks = qint64(o.value(QStringLiteral("RunTimeTicks")).toDouble());
        track.durationMs = ticks > 0 ? ticks / TicksPerMs : -1;
        tracks << track;
    }
    return tracks;
}

SearchResults parseSearch(const QByteArray &json, const QString &serverId)
{
    SearchResults results;
    QJsonArray audio;
    for (const QJsonValue &value : items(json)) {
        const QJsonObject o = value.toObject();
        const QString type = str(o, "Type");
        if (type == QLatin1String("Audio")) {
            audio.append(o);
            continue;
        }
        const QList<Folder> folder = parseFolders(QJsonDocument(QJsonArray{o}).toJson(QJsonDocument::Compact));
        if (folder.isEmpty())
            continue;
        if (type == QLatin1String("MusicArtist")) {
            results.artists << folder.first();
        } else if (type == QLatin1String("MusicAlbum")) {
            Folder album = folder.first();
            // Albums from different artists often share a name.
            const QString artist = str(o, "AlbumArtist");
            if (!artist.isEmpty())
                album.title = artist + QStringLiteral(" - ") + album.title;
            results.albums << album;
        } else if (type == QLatin1String("Playlist")) {
            results.playlists << folder.first();
        }
    }
    results.tracks = parseTracks(QJsonDocument(audio).toJson(QJsonDocument::Compact), serverId);
    return results;
}

QUrl trackLocation(const TrackRef &ref)
{
    QUrl url;
    url.setScheme(QStringLiteral("jellyfin"));
    url.setHost(ref.serverId);
    url.setPath(QLatin1Char('/') + ref.itemId);
    if (!ref.container.isEmpty())
        url.setQuery(QStringLiteral("container=") + ref.container);
    return url;
}

TrackRef parseTrackLocation(const QUrl &location)
{
    if (location.scheme() != QLatin1String("jellyfin"))
        return {};
    QString item = location.path();
    if (item.startsWith(QLatin1Char('/')))
        item.remove(0, 1);
    if (item.contains(QLatin1Char('/')))
        return {};
    const QString query = location.query();
    const QString container = query.startsWith(QStringLiteral("container=")) ? query.mid(10) : QString();
    return {location.host(), item, container};
}

QUrl streamUrl(const QString &serverUrl, const TrackRef &ref, const QString &token, const QString &deviceId)
{
    const QString file = ref.container.isEmpty() ? QStringLiteral("stream") : QStringLiteral("stream.") + ref.container;
    // Players can't send headers, so the token rides in the query.
    return jellyfin::serverUrl(serverUrl, withQuery(QStringLiteral("/Audio/%1/%2").arg(ref.itemId, file),
                                                    {
                                                        {QStringLiteral("static"), QStringLiteral("true")},
                                                        {QStringLiteral("deviceId"), deviceId},
                                                        {QStringLiteral("api_key"), token},
                                                    }));
}

QUrl transcodeUrl(const QString &serverUrl, const TrackRef &ref, const QString &token, const QString &deviceId)
{
    return jellyfin::serverUrl(serverUrl, withQuery(QStringLiteral("/Audio/%1/stream.mp3").arg(ref.itemId),
                                                    {
                                                        {QStringLiteral("container"), QStringLiteral("mp3")},
                                                        {QStringLiteral("audioCodec"), QStringLiteral("mp3")},
                                                        {QStringLiteral("audioBitRate"), QStringLiteral("320000")},
                                                        {QStringLiteral("deviceId"), deviceId},
                                                        {QStringLiteral("api_key"), token},
                                                    }));
}

namespace {
const QString TrackFields = QStringLiteral("Path,MediaSources");
const QString TrackOrder = QStringLiteral("ParentIndexNumber,IndexNumber,SortName");
} // namespace

QString viewsPath(const QString &userId)
{
    return withQuery(QStringLiteral("/UserViews"), {{QStringLiteral("userId"), userId}});
}

QString artistsPath(const QString &userId, const QString &libraryId)
{
    return withQuery(QStringLiteral("/Artists/AlbumArtists"), {
                                                                  {QStringLiteral("userId"), userId},
                                                                  {QStringLiteral("parentId"), libraryId},
                                                                  {QStringLiteral("sortBy"), QStringLiteral("SortName")},
                                                              });
}

QString albumsPath(const QString &userId, const QString &artistId)
{
    return withQuery(QStringLiteral("/Items"), {
                                                   {QStringLiteral("userId"), userId},
                                                   {QStringLiteral("artistIds"), artistId},
                                                   {QStringLiteral("includeItemTypes"), QStringLiteral("MusicAlbum")},
                                                   {QStringLiteral("recursive"), QStringLiteral("true")},
                                                   {QStringLiteral("sortBy"), QStringLiteral("ProductionYear,SortName")},
                                               });
}

QString albumTracksPath(const QString &userId, const QString &albumId)
{
    return withQuery(QStringLiteral("/Items"), {
                                                   {QStringLiteral("userId"), userId},
                                                   {QStringLiteral("parentId"), albumId},
                                                   {QStringLiteral("includeItemTypes"), QStringLiteral("Audio")},
                                                   {QStringLiteral("recursive"), QStringLiteral("true")},
                                                   {QStringLiteral("sortBy"), TrackOrder},
                                                   {QStringLiteral("fields"), TrackFields},
                                               });
}

QString artistTracksPath(const QString &userId, const QString &artistId)
{
    return withQuery(QStringLiteral("/Items"), {
                                                   {QStringLiteral("userId"), userId},
                                                   {QStringLiteral("artistIds"), artistId},
                                                   {QStringLiteral("includeItemTypes"), QStringLiteral("Audio")},
                                                   {QStringLiteral("recursive"), QStringLiteral("true")},
                                                   {QStringLiteral("sortBy"), QStringLiteral("ProductionYear,Album,") + TrackOrder},
                                                   {QStringLiteral("fields"), TrackFields},
                                               });
}

QString playlistsPath(const QString &userId)
{
    return withQuery(QStringLiteral("/Items"), {
                                                   {QStringLiteral("userId"), userId},
                                                   {QStringLiteral("includeItemTypes"), QStringLiteral("Playlist")},
                                                   {QStringLiteral("recursive"), QStringLiteral("true")},
                                                   {QStringLiteral("sortBy"), QStringLiteral("SortName")},
                                                   {QStringLiteral("fields"), QStringLiteral("ChildCount")},
                                               });
}

QString playlistTracksPath(const QString &userId, const QString &playlistId)
{
    return withQuery(QStringLiteral("/Playlists/%1/Items").arg(playlistId), {
                                                                                {QStringLiteral("userId"), userId},
                                                                                {QStringLiteral("fields"), TrackFields},
                                                                            });
}

QString searchPath(const QString &userId, const QString &query, const QString &libraryId)
{
    return withQuery(QStringLiteral("/Items"),
                     {
                         {QStringLiteral("userId"), userId},
                         {QStringLiteral("searchTerm"), query},
                         {QStringLiteral("parentId"), libraryId},
                         {QStringLiteral("includeItemTypes"), QStringLiteral("MusicAlbum,Playlist,Audio")},
                         {QStringLiteral("recursive"), QStringLiteral("true")},
                         {QStringLiteral("limit"), QStringLiteral("100")},
                         {QStringLiteral("fields"), TrackFields},
                     });
}

QString artistSearchPath(const QString &userId, const QString &query, const QString &libraryId)
{
    return withQuery(QStringLiteral("/Artists"), {
                                                     {QStringLiteral("userId"), userId},
                                                     {QStringLiteral("searchTerm"), query},
                                                     {QStringLiteral("parentId"), libraryId},
                                                     {QStringLiteral("limit"), QStringLiteral("20")},
                                                 });
}

namespace {
QByteArray report(const QString &itemId, qint64 positionMs, QJsonObject extra)
{
    extra.insert(QStringLiteral("ItemId"), itemId);
    extra.insert(QStringLiteral("PositionTicks"), double(qMax<qint64>(0, positionMs) * TicksPerMs));
    return QJsonDocument(extra).toJson(QJsonDocument::Compact);
}
} // namespace

QByteArray playingBody(const QString &itemId, qint64 positionMs, bool paused)
{
    return report(itemId, positionMs,
                  {{QStringLiteral("IsPaused"), paused},
                   {QStringLiteral("CanSeek"), true},
                   {QStringLiteral("PlayMethod"), QStringLiteral("DirectPlay")}});
}

QByteArray progressBody(const QString &itemId, qint64 positionMs, bool paused, const QString &event)
{
    return report(itemId, positionMs,
                  {{QStringLiteral("IsPaused"), paused},
                   {QStringLiteral("CanSeek"), true},
                   {QStringLiteral("PlayMethod"), QStringLiteral("DirectPlay")},
                   {QStringLiteral("EventName"), event}});
}

QByteArray stoppedBody(const QString &itemId, qint64 positionMs)
{
    return report(itemId, positionMs, {});
}

namespace {
const char *const FolderKindNames[] = {"library", "artist", "album", "playlists", "playlist"};
}

QString folderId(FolderKind kind, const QString &itemId)
{
    return QStringLiteral("%1|%2").arg(QLatin1String(FolderKindNames[int(kind)]), itemId);
}

bool parseFolderId(const QString &id, FolderKind *kind, QString *itemId)
{
    const QStringList parts = id.split(QLatin1Char('|'));
    if (parts.size() != 2)
        return false;
    for (int i = 0; i < int(std::size(FolderKindNames)); ++i) {
        if (parts[0] == QLatin1String(FolderKindNames[i])) {
            *kind = FolderKind(i);
            *itemId = parts[1];
            return true;
        }
    }
    return false;
}

} // namespace newamp::jellyfin

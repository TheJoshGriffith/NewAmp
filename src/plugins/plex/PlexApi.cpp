#include "PlexApi.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QUrlQuery>

#include <algorithm>

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

QList<Directory> parseDirectories(const QByteArray &json)
{
    QList<Directory> directories;
    for (const QJsonValue &value : containerItems(json)) {
        const QJsonObject o = value.toObject();
        Directory directory;
        directory.key = text(o.value(QStringLiteral("ratingKey")));
        directory.title = o.value(QStringLiteral("title")).toString();
        if (const int year = o.value(QStringLiteral("year")).toInt())
            directory.subtitle = QString::number(year);
        else if (o.contains(QStringLiteral("leafCount")))
            directory.subtitle = QStringLiteral("%1 tracks").arg(o.value(QStringLiteral("leafCount")).toInt());
        if (!directory.key.isEmpty())
            directories << directory;
    }
    return directories;
}

QList<Track> parseTracks(const QByteArray &json, const QString &machineIdentifier)
{
    QList<Track> tracks;
    for (const QJsonValue &value : containerItems(json)) {
        const QJsonObject o = value.toObject();
        const QJsonObject media = o.value(QStringLiteral("Media")).toArray().first().toObject();
        const QJsonObject part = media.value(QStringLiteral("Part")).toArray().first().toObject();
        TrackRef ref{machineIdentifier, part.value(QStringLiteral("key")).toString(),
                     text(o.value(QStringLiteral("ratingKey")))};
        if (!ref.isValid())
            continue;
        Track track;
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
        tracks << track;
    }
    return tracks;
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

QUrl streamUrl(const QString &connectionUri, const QString &partKey, const QString &token)
{
    QString base = connectionUri;
    while (base.endsWith(QLatin1Char('/')))
        base.chop(1);
    QUrl url(base + partKey);
    QUrlQuery query;
    query.addQueryItem(QStringLiteral("X-Plex-Token"), token);
    url.setQuery(query);
    return url;
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

} // namespace newamp::plex

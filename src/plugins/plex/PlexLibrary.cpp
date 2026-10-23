#include "PlexLibrary.h"

#include "PlexClient.h"
#include "plugins/PluginHost.h"

#include <memory>

namespace newamp::plex {

namespace {

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

using Kind = LibraryItem::Kind;

LibraryItem trackItem(const Track &track, const QString &title)
{
    return {{}, title, formatDuration(track.durationMs), Kind::Track, track};
}

} // namespace

PlexLibrary::PlexLibrary(PlexClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_host(host)
    , m_actions(std::move(actions))
    , m_requests(new QObject(this))
{
    // A new account or server list makes every open folder stale.
    connect(&m_client, &PlexClient::signedInChanged, this, &PlexLibrary::restart);
    connect(&m_client, &PlexClient::serversChanged, this, &PlexLibrary::restart);
    connect(&m_client, &PlexClient::status, this, [this](const QString &message) {
        m_status = message;
        m_host.libraryStatusChanged(this);
    });
}

void PlexLibrary::restart()
{
    delete m_requests;
    m_requests = new QObject(this);
    m_host.libraryChanged(this);
}

void PlexLibrary::get(const QString &server, const QString &path,
                      const std::function<void(const QByteArray &)> &onJson)
{
    m_client.get(server, path, m_requests, onJson);
}

void PlexLibrary::listItems(const QString &id, const ItemsCallback &done)
{
    if (id.isEmpty()) {
        if (!m_client.isSignedIn()) {
            m_status = tr("Not signed in. Right-click here to sign in to Plex.");
            done({});
            return;
        }
        const QList<PlexClient::ServerInfo> servers = m_client.servers();
        if (servers.isEmpty()) {
            if (m_status.isEmpty())
                m_status = tr("Looking for Plex servers...");
            return; // serversChanged() restarts the library
        }
        if (servers.size() == 1) {
            listServer(servers.first().machineIdentifier, done);
            return;
        }
        QList<LibraryItem> items;
        for (const PlexClient::ServerInfo &server : servers)
            items.append({folderId(server.machineIdentifier, FolderKind::Server), server.name, {}, Kind::Folder, {}});
        done(items);
        return;
    }

    QString server, key;
    FolderKind kind;
    if (!parseFolderId(id, &server, &kind, &key)) {
        done({});
        return;
    }
    const auto directories = [server, done](FolderKind childKind, Kind itemKind) {
        return [server, done, childKind, itemKind](const QByteArray &json) {
            QList<LibraryItem> items;
            for (const Directory &d : parseDirectories(json))
                items.append({folderId(server, childKind, d.key), d.title, d.subtitle, itemKind, {}});
            done(items);
        };
    };
    switch (kind) {
    case FolderKind::Server: listServer(server, done); break;
    case FolderKind::Section:
        get(server, QStringLiteral("/library/sections/%1/all?type=8").arg(key), directories(FolderKind::Artist, Kind::Artist));
        break;
    case FolderKind::Artist:
        get(server, QStringLiteral("/library/metadata/%1/children").arg(key), directories(FolderKind::Album, Kind::Album));
        break;
    case FolderKind::Playlists:
        get(server, QStringLiteral("/playlists?playlistType=audio"), directories(FolderKind::Playlist, Kind::Playlist));
        break;
    case FolderKind::Album:
    case FolderKind::Playlist:
        folderTracks(id, [done](const QList<Track> &tracks) {
            QList<LibraryItem> items;
            for (const Track &track : tracks)
                items.append(trackItem(track, track.title));
            done(items);
        });
        break;
    }
}

void PlexLibrary::listServer(const QString &server, const ItemsCallback &done)
{
    get(server, QStringLiteral("/library/sections"), [this, server, done](const QByteArray &json) {
        QList<LibraryItem> items;
        for (const Directory &section : parseSections(json))
            items.append({folderId(server, FolderKind::Section, section.key), section.title, {}, Kind::Folder, {}});
        if (items.isEmpty())
            m_status = tr("This server has no music libraries.");
        items.append({folderId(server, FolderKind::Playlists), tr("Playlists"), {}, Kind::Folder, {}});
        done(items);
    });
}

void PlexLibrary::folderTracks(const QString &id, const TracksCallback &done)
{
    QString server, key;
    FolderKind kind;
    if (!parseFolderId(id, &server, &kind, &key)) {
        done({});
        return;
    }
    QString path;
    switch (kind) {
    case FolderKind::Artist: path = QStringLiteral("/library/metadata/%1/allLeaves").arg(key); break;
    case FolderKind::Album: path = QStringLiteral("/library/metadata/%1/children").arg(key); break;
    case FolderKind::Playlist: path = QStringLiteral("/playlists/%1/items").arg(key); break;
    default: done({}); return; // servers, sections and the playlist folder are too big to queue
    }
    get(server, path, [done, server](const QByteArray &json) { done(parseTracks(json, server)); });
}

bool PlexLibrary::canSearch() const
{
    return m_client.isSignedIn() && !m_client.servers().isEmpty();
}

void PlexLibrary::search(const QString &scopeId, const QString &query, const ItemsCallback &done)
{
    // A music section searches only itself; anything deeper searches its
    // server, and the top level every server.
    QStringList servers;
    QString section;
    QString server, key;
    FolderKind kind;
    if (parseFolderId(scopeId, &server, &kind, &key)) {
        servers = {server};
        if (kind == FolderKind::Section)
            section = key;
    } else {
        for (const PlexClient::ServerInfo &info : m_client.servers())
            servers << info.machineIdentifier;
    }
    if (servers.isEmpty() || query.trimmed().isEmpty()) {
        done({});
        return;
    }

    // Each server answers on its own; the results go out once all have.
    auto collected = std::make_shared<QList<LibraryItem>>();
    auto remaining = std::make_shared<int>(int(servers.size()));
    for (const QString &id : std::as_const(servers)) {
        get(id, searchPath(query.trimmed(), section), [collected, remaining, done, id](const QByteArray &json) {
            const SearchResults found = parseSearch(json, id);
            for (const Directory &d : found.artists)
                collected->append({folderId(id, FolderKind::Artist, d.key), d.title, {}, Kind::Artist, {}});
            for (const Directory &d : found.albums) {
                // Albums from different artists often share a name.
                const QString title = d.parentTitle.isEmpty() ? d.title : d.parentTitle + QStringLiteral(" - ") + d.title;
                collected->append({folderId(id, FolderKind::Album, d.key), title, d.subtitle, Kind::Album, {}});
            }
            for (const Directory &d : found.playlists)
                collected->append({folderId(id, FolderKind::Playlist, d.key), d.title, d.subtitle, Kind::Playlist, {}});
            for (const Track &track : found.tracks)
                collected->append(trackItem(track, track.displayTitle()));
            if (--*remaining == 0)
                done(*collected);
        });
    }
}

} // namespace newamp::plex

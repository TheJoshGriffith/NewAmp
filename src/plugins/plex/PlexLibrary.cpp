#include "PlexLibrary.h"

#include "PlexClient.h"
#include "plugins/PluginHost.h"

namespace newamp::plex {

namespace {

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
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
            items.append({folderId(server.machineIdentifier, FolderKind::Server), server.name, {}, true, {}});
        done(items);
        return;
    }

    QString server, key;
    FolderKind kind;
    if (!parseFolderId(id, &server, &kind, &key)) {
        done({});
        return;
    }
    const auto directories = [server, done](FolderKind childKind) {
        return [server, done, childKind](const QByteArray &json) {
            QList<LibraryItem> items;
            for (const Directory &d : parseDirectories(json))
                items.append({folderId(server, childKind, d.key), d.title, d.subtitle, true, {}});
            done(items);
        };
    };
    switch (kind) {
    case FolderKind::Server: listServer(server, done); break;
    case FolderKind::Section:
        get(server, QStringLiteral("/library/sections/%1/all?type=8").arg(key), directories(FolderKind::Artist));
        break;
    case FolderKind::Artist:
        get(server, QStringLiteral("/library/metadata/%1/children").arg(key), directories(FolderKind::Album));
        break;
    case FolderKind::Playlists:
        get(server, QStringLiteral("/playlists?playlistType=audio"), directories(FolderKind::Playlist));
        break;
    case FolderKind::Album:
    case FolderKind::Playlist:
        folderTracks(id, [done](const QList<Track> &tracks) {
            QList<LibraryItem> items;
            for (const Track &track : tracks)
                items.append({{}, track.title, formatDuration(track.durationMs), false, track});
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
            items.append({folderId(server, FolderKind::Section, section.key), section.title, {}, true, {}});
        if (items.isEmpty())
            m_status = tr("This server has no music libraries.");
        items.append({folderId(server, FolderKind::Playlists), tr("Playlists"), {}, true, {}});
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

} // namespace newamp::plex

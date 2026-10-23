#include "JellyfinLibrary.h"

#include "JellyfinClient.h"
#include "plugins/PluginHost.h"

#include <memory>

namespace newamp::jellyfin {

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

void appendFolders(QList<LibraryItem> &items, const QList<Folder> &folders, FolderKind folderKind, Kind kind)
{
    for (const Folder &f : folders)
        items.append({folderId(folderKind, f.id), f.title, f.detail, kind, {}});
}

} // namespace

JellyfinLibrary::JellyfinLibrary(JellyfinClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_host(host)
    , m_actions(std::move(actions))
    , m_requests(new QObject(this))
{
    // Another account or server makes every open folder stale.
    connect(&m_client, &JellyfinClient::signedInChanged, this, &JellyfinLibrary::restart);
    connect(&m_client, &JellyfinClient::status, this, [this](const QString &message) {
        m_status = message;
        m_host.libraryStatusChanged(this);
    });
}

void JellyfinLibrary::restart()
{
    delete m_requests;
    m_requests = new QObject(this);
    m_host.libraryChanged(this);
}

void JellyfinLibrary::get(const QString &path, const std::function<void(const QByteArray &)> &onJson)
{
    m_client.get(path, m_requests, onJson);
}

void JellyfinLibrary::listItems(const QString &id, const ItemsCallback &done)
{
    const QString user = m_client.userId();
    if (id.isEmpty()) {
        if (!m_client.isSignedIn()) {
            m_status = tr("Not signed in. Right-click here to sign in to Jellyfin.");
            done({});
            return;
        }
        get(viewsPath(user), [this, done](const QByteArray &json) {
            QList<LibraryItem> items;
            appendFolders(items, parseMusicViews(json), FolderKind::Library, Kind::Folder);
            if (items.isEmpty())
                m_status = tr("This server has no music libraries.");
            items.append({folderId(FolderKind::Playlists), tr("Playlists"), {}, Kind::Folder, {}});
            done(items);
        });
        return;
    }

    FolderKind kind;
    QString key;
    if (!parseFolderId(id, &kind, &key)) {
        done({});
        return;
    }
    const auto folders = [done](FolderKind childKind, Kind itemKind) {
        return [done, childKind, itemKind](const QByteArray &json) {
            QList<LibraryItem> items;
            appendFolders(items, parseFolders(json), childKind, itemKind);
            done(items);
        };
    };
    switch (kind) {
    case FolderKind::Library: get(artistsPath(user, key), folders(FolderKind::Artist, Kind::Artist)); break;
    case FolderKind::Artist: get(albumsPath(user, key), folders(FolderKind::Album, Kind::Album)); break;
    case FolderKind::Playlists: get(playlistsPath(user), folders(FolderKind::Playlist, Kind::Playlist)); break;
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

void JellyfinLibrary::folderTracks(const QString &id, const TracksCallback &done)
{
    FolderKind kind;
    QString key;
    if (!parseFolderId(id, &kind, &key)) {
        done({});
        return;
    }
    const QString user = m_client.userId();
    QString path;
    switch (kind) {
    case FolderKind::Artist: path = artistTracksPath(user, key); break;
    case FolderKind::Album: path = albumTracksPath(user, key); break;
    case FolderKind::Playlist: path = playlistTracksPath(user, key); break;
    default: done({}); return; // libraries and the playlist folder are too big to queue
    }
    // Read at reply time: the server can change with a new sign-in, but
    // that drops this request first.
    get(path, [this, done](const QByteArray &json) { done(parseTracks(json, m_client.serverId())); });
}

bool JellyfinLibrary::canSearch() const
{
    return m_client.isSignedIn();
}

void JellyfinLibrary::search(const QString &scopeId, const QString &query, const ItemsCallback &done)
{
    const QString term = query.trimmed();
    if (!m_client.isSignedIn() || term.isEmpty()) {
        done({});
        return;
    }
    // A music library searches only itself; anywhere else searches it all.
    FolderKind kind;
    QString key, library;
    if (parseFolderId(scopeId, &kind, &key) && kind == FolderKind::Library)
        library = key;

    // Artists and everything else come from two endpoints; the results go
    // out once both have answered.
    auto artists = std::make_shared<QList<LibraryItem>>();
    auto rest = std::make_shared<QList<LibraryItem>>();
    auto remaining = std::make_shared<int>(2);
    auto finish = [artists, rest, remaining, done] {
        if (--*remaining == 0)
            done(*artists + *rest);
    };
    const QString user = m_client.userId();
    get(artistSearchPath(user, term, library), [artists, finish](const QByteArray &json) {
        appendFolders(*artists, parseFolders(json), FolderKind::Artist, Kind::Artist);
        finish();
    });
    get(searchPath(user, term, library), [this, rest, finish](const QByteArray &json) {
        const SearchResults found = parseSearch(json, m_client.serverId());
        appendFolders(*rest, found.albums, FolderKind::Album, Kind::Album);
        appendFolders(*rest, found.playlists, FolderKind::Playlist, Kind::Playlist);
        for (const Track &track : found.tracks)
            rest->append(trackItem(track, track.displayTitle()));
        finish();
    });
}

} // namespace newamp::jellyfin

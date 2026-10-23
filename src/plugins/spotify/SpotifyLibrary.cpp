#include "SpotifyLibrary.h"

#include "SpotifyClient.h"
#include "plugins/PluginHost.h"

#include <QUrl>

namespace newamp::spotify {

namespace {

// Big libraries are listed up to this many entries (pages of 50).
constexpr int ListLimit = 2000;
// An artist is queued from this many of their albums and singles.
constexpr int ArtistAlbumLimit = 20;

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

LibraryItem::Kind itemKind(FolderKind kind)
{
    switch (kind) {
    case FolderKind::Playlist: return LibraryItem::Kind::Playlist;
    case FolderKind::Album: return LibraryItem::Kind::Album;
    case FolderKind::Artist: return LibraryItem::Kind::Artist;
    default: return LibraryItem::Kind::Folder;
    }
}

LibraryItem folderItem(FolderKind kind, const Directory &d)
{
    return {folderId(kind, d.id), d.title, d.subtitle, itemKind(kind), {}};
}

// Album tracks are listed by title alone; everything else with its artist.
QList<LibraryItem> trackItems(const QList<Track> &tracks, bool withArtist)
{
    QList<LibraryItem> items;
    for (const Track &track : tracks)
        items.append({{}, withArtist ? track.displayTitle() : track.title, formatDuration(track.durationMs),
                      LibraryItem::Kind::Track, track});
    return items;
}

} // namespace

SpotifyLibrary::SpotifyLibrary(SpotifyClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_host(host)
    , m_actions(std::move(actions))
    , m_requests(new QObject(this))
{
    connect(&m_client, &SpotifyClient::signedInChanged, this, &SpotifyLibrary::restart);
    connect(&m_client, &SpotifyClient::status, this, &SpotifyLibrary::setStatus);
}

void SpotifyLibrary::restart()
{
    delete m_requests;
    m_requests = new QObject(this);
    m_host.libraryChanged(this);
}

void SpotifyLibrary::setStatus(const QString &status)
{
    m_status = status;
    m_host.libraryStatusChanged(this);
}

bool SpotifyLibrary::canSearch() const
{
    return m_client.isSignedIn();
}

template<typename T>
void SpotifyLibrary::getAll(const QString &path, const Parser<T> &parse, int limit,
                            const std::function<void(QList<T>)> &done, QList<T> sofar)
{
    m_client.get(path, m_requests, [this, parse, limit, done, sofar](const QByteArray &json) mutable {
        const Page<T> page = parse(json);
        sofar.append(page.items);
        if (page.next.isEmpty() || sofar.size() >= limit) {
            done(sofar.mid(0, limit));
            return;
        }
        getAll(page.next, parse, limit, done, std::move(sofar));
    });
}

void SpotifyLibrary::listDirectories(const QString &path, const Parser<Directory> &parse, FolderKind kind,
                                     const ItemsCallback &done)
{
    getAll<Directory>(path, parse, ListLimit, [done, kind](const QList<Directory> &directories) {
        QList<LibraryItem> items;
        for (const Directory &d : directories)
            items.append(folderItem(kind, d));
        done(items);
    });
}

void SpotifyLibrary::listItems(const QString &id, const ItemsCallback &done)
{
    if (id.isEmpty()) {
        if (!m_client.isSignedIn()) {
            m_status = m_client.clientId().isEmpty()
                           ? tr("Right-click here to set your Spotify client ID, then sign in.")
                           : tr("Not signed in. Right-click here to sign in to Spotify.");
            done({});
            return;
        }
        done({{folderId(FolderKind::Playlists), tr("Playlists"), {}, LibraryItem::Kind::Folder, {}},
              {folderId(FolderKind::LikedSongs), tr("Liked Songs"), {}, LibraryItem::Kind::Folder, {}},
              {folderId(FolderKind::Albums), tr("Albums"), {}, LibraryItem::Kind::Folder, {}},
              {folderId(FolderKind::Artists), tr("Artists"), {}, LibraryItem::Kind::Folder, {}}});
        return;
    }

    FolderKind kind;
    QString key;
    if (!parseFolderId(id, &kind, &key)) {
        done({});
        return;
    }
    switch (kind) {
    case FolderKind::Playlists:
        listDirectories(QStringLiteral("/me/playlists?limit=50"), parsePlaylists, FolderKind::Playlist, done);
        break;
    case FolderKind::Albums:
        listDirectories(QStringLiteral("/me/albums?limit=50"), parseSavedAlbums, FolderKind::Album, done);
        break;
    case FolderKind::Artists:
        listDirectories(QStringLiteral("/me/following?type=artist&limit=50"), parseFollowedArtists,
                        FolderKind::Artist, done);
        break;
    case FolderKind::Artist:
        listDirectories(QStringLiteral("/artists/%1/albums?include_groups=album,single&limit=50").arg(key),
                        parseAlbums, FolderKind::Album, done);
        break;
    case FolderKind::LikedSongs:
    case FolderKind::Playlist:
    case FolderKind::Album:
        folderTracks(id, [done, kind](const QList<Track> &tracks) {
            done(trackItems(tracks, kind != FolderKind::Album));
        });
        break;
    }
}

void SpotifyLibrary::folderTracks(const QString &id, const TracksCallback &done)
{
    FolderKind kind;
    QString key;
    if (!parseFolderId(id, &kind, &key)) {
        done({});
        return;
    }
    switch (kind) {
    case FolderKind::LikedSongs:
        getAll<Track>(QStringLiteral("/me/tracks?limit=50"),
                      [](const QByteArray &json) { return parseTracks(json, TrackList::Saved); }, ListLimit, done);
        break;
    case FolderKind::Playlist:
        // Spotify answers 403 for playlists the user neither owns nor
        // collaborates on, which apps in development mode may not read.
        m_client.request(
            "GET", QStringLiteral("/playlists/%1/items?limit=50").arg(key), {}, m_requests,
            [this, done](int status, const QByteArray &json) {
                if (status == 403 || status == 404) {
                    setStatus(tr("Spotify only lets NewAmp open playlists you own or collaborate on."));
                    return;
                }
                if (status != 200) {
                    setStatus(SpotifyClient::errorText(status, json));
                    return;
                }
                const Page<Track> first = parseTracks(json, TrackList::Playlist);
                if (first.next.isEmpty()) {
                    done(first.items);
                    return;
                }
                getAll<Track>(first.next, [](const QByteArray &page) { return parseTracks(page, TrackList::Playlist); },
                              ListLimit, done, first.items);
            });
        break;
    case FolderKind::Album: albumTracks(key, done); break;
    case FolderKind::Artist:
        getAll<Directory>(QStringLiteral("/artists/%1/albums?include_groups=album,single&limit=50").arg(key),
                          parseAlbums, ArtistAlbumLimit, [this, done](const QList<Directory> &albums) {
                              QStringList ids;
                              for (const Directory &album : albums)
                                  ids.append(album.id);
                              artistTracks(ids, {}, done);
                          });
        break;
    default: done({}); return; // the lists of playlists, albums and artists are too big to queue
    }
}

void SpotifyLibrary::albumTracks(const QString &albumId, const TracksCallback &done)
{
    m_client.get(QStringLiteral("/albums/%1").arg(albumId), m_requests, [this, done](const QByteArray &json) {
        QString name;
        const Page<Track> first = parseAlbumTracks(json, &name);
        if (first.next.isEmpty()) {
            done(first.items);
            return;
        }
        getAll<Track>(first.next, [name](const QByteArray &page) { return parseTracks(page, TrackList::Plain, name); },
                      ListLimit, done, first.items);
    });
}

void SpotifyLibrary::artistTracks(QStringList albumIds, QList<Track> sofar, const TracksCallback &done)
{
    // One album at a time, so the queue keeps the artist's album order.
    if (albumIds.isEmpty()) {
        done(sofar);
        return;
    }
    const QString next = albumIds.takeFirst();
    albumTracks(next, [this, albumIds, sofar, done](const QList<Track> &tracks) mutable {
        sofar.append(tracks);
        artistTracks(albumIds, std::move(sofar), done);
    });
}

void SpotifyLibrary::search(const QString &scopeId, const QString &query, const ItemsCallback &done)
{
    Q_UNUSED(scopeId) // Spotify searches its whole catalogue
    if (!m_client.isSignedIn() || query.trimmed().isEmpty()) {
        done({});
        return;
    }
    // Ten of each, the most a page holds for apps in development mode.
    const QString path = QStringLiteral("/search?type=artist,album,playlist,track&limit=10&q=")
                       + QString::fromLatin1(QUrl::toPercentEncoding(query.trimmed()));
    m_client.get(path, m_requests, [done](const QByteArray &json) {
        const SearchResults results = parseSearch(json);
        QList<LibraryItem> items;
        for (const Directory &d : results.artists)
            items.append(folderItem(FolderKind::Artist, d));
        for (const Directory &d : results.albums)
            items.append(folderItem(FolderKind::Album, d));
        for (const Directory &d : results.playlists)
            items.append(folderItem(FolderKind::Playlist, d));
        items.append(trackItems(results.tracks, true));
        done(items);
    });
}

} // namespace newamp::spotify

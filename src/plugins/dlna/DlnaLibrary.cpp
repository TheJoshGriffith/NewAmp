#include "DlnaLibrary.h"

#include "DlnaClient.h"
#include "plugins/PluginHost.h"

namespace newamp::dlna {

namespace {

constexpr int PageSize = 200;      // what servers commonly allow per call
constexpr int MaxObjects = 5000;   // a folder bigger than this is cut short
constexpr int MaxSearchResults = 200;

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}

LibraryItem::Kind containerKind(const QString &upnpClass)
{
    if (upnpClass.startsWith(QLatin1String("object.container.album")))
        return LibraryItem::Kind::Album;
    if (upnpClass.startsWith(QLatin1String("object.container.person")))
        return LibraryItem::Kind::Artist;
    if (upnpClass.startsWith(QLatin1String("object.container.playlistContainer")))
        return LibraryItem::Kind::Playlist;
    return LibraryItem::Kind::Folder;
}

} // namespace

struct DlnaLibrary::Fetch
{
    QString udn;
    QString objectId;
    ObjectsCallback done;
    QList<DidlObject> objects;
};

DlnaLibrary::DlnaLibrary(DlnaClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_host(host)
    , m_actions(std::move(actions))
    , m_requests(new QObject(this))
{
    // Servers come and go at any time; only the list of them needs a
    // reload, so someone deep inside a server isn't sent back to the top.
    connect(&m_client, &DlnaClient::serversChanged, this, [this] {
        if (m_atTop)
            restart();
    });
    connect(&m_client, &DlnaClient::searchCapabilitiesChanged, this, [this] { m_host.libraryStatusChanged(this); });
    connect(&m_client, &DlnaClient::status, this, [this](const QString &message) {
        m_status = message;
        m_host.libraryStatusChanged(this);
    });
}

void DlnaLibrary::restart()
{
    delete m_requests;
    m_requests = new QObject(this);
    m_host.libraryChanged(this);
}

void DlnaLibrary::listItems(const QString &id, const ItemsCallback &done)
{
    m_atTop = id.isEmpty();
    if (id.isEmpty()) {
        QList<LibraryItem> items;
        for (const DlnaClient::ServerInfo &server : m_client.servers())
            items.append({folderId(server.device.udn, QStringLiteral("0")), server.device.name, {},
                          LibraryItem::Kind::Folder, {}});
        if (items.isEmpty() && m_client.isDiscovering())
            m_status = tr("Looking for media servers...");
        done(items);
        return;
    }

    QString udn, objectId;
    if (!parseFolderId(id, &udn, &objectId)) {
        done({});
        return;
    }
    browseAll(udn, objectId, [this, udn, done](const QList<DidlObject> &objects) {
        const QList<LibraryItem> items = toItems(udn, objects, false);
        if (items.isEmpty() && m_status.isEmpty())
            m_status = tr("Nothing to play here.");
        done(items);
    });
}

void DlnaLibrary::folderTracks(const QString &id, const TracksCallback &done)
{
    QString udn, objectId;
    if (!parseFolderId(id, &udn, &objectId)) {
        done({});
        return;
    }
    // Only the folder's own tracks: a folder of folders can hold a whole
    // library, which is too much to queue at once.
    browseAll(udn, objectId, [this, udn, done](const QList<DidlObject> &objects) {
        QList<Track> tracks;
        for (const LibraryItem &item : toItems(udn, objects, true)) {
            if (item.kind == LibraryItem::Kind::Track)
                tracks << item.track;
        }
        done(tracks);
    });
}

void DlnaLibrary::browseAll(const QString &udn, const QString &objectId, const ObjectsCallback &done)
{
    auto fetch = std::make_shared<Fetch>();
    fetch->udn = udn;
    fetch->objectId = objectId;
    fetch->done = done;
    browsePage(fetch, 0);
}

void DlnaLibrary::browsePage(const std::shared_ptr<Fetch> &fetch, int start)
{
    m_client.browse(fetch->udn, fetch->objectId, start, PageSize, m_requests, [this, fetch, start](const BrowseResult &result) {
        if (!result.ok) {
            if (start > 0)
                fetch->done(fetch->objects); // keep the pages that did arrive
            return;
        }
        const QList<DidlObject> page = parseDidl(result.didl);
        fetch->objects += page;
        const int returned = result.returned > 0 ? result.returned : int(page.size());
        const int next = start + returned;
        // Without a total, a full page means there may be more.
        const bool more = returned > 0 && (result.total > 0 ? next < result.total : returned >= PageSize);
        if (more && fetch->objects.size() < MaxObjects)
            browsePage(fetch, next);
        else
            fetch->done(fetch->objects);
    });
}

QList<LibraryItem> DlnaLibrary::toItems(const QString &udn, const QList<DidlObject> &objects, bool musicOnly) const
{
    const DlnaClient::ServerInfo *server = m_client.server(udn);
    const QString host = server ? server->location.host() : QString();
    QList<LibraryItem> items;
    for (const DidlObject &object : objects) {
        if (object.isContainer) {
            const LibraryItem::Kind kind = containerKind(object.upnpClass);
            if (musicOnly && kind == LibraryItem::Kind::Folder)
                continue;
            QString detail;
            if (kind == LibraryItem::Kind::Album && object.date.size() >= 4)
                detail = object.date.left(4);
            else if (object.childCount >= 0)
                detail = QString::number(object.childCount);
            items.append({folderId(udn, object.id), object.title, detail, kind, {}});
        } else if (const std::optional<Track> track = trackFromItem(object, udn, host)) {
            items.append({{}, object.title, formatDuration(track->durationMs), LibraryItem::Kind::Track, *track});
        }
    }
    return items;
}

bool DlnaLibrary::canSearch() const
{
    for (const DlnaClient::ServerInfo &server : m_client.servers()) {
        if (!searchCriteria(QStringLiteral("x"), server.searchCaps).isEmpty())
            return true;
    }
    return false;
}

void DlnaLibrary::search(const QString &scopeId, const QString &query, const ItemsCallback &done)
{
    // Search inside the folder when its server can; otherwise every server
    // that can, from the top.
    QList<std::pair<QString, QString>> targets; // udn, container id
    QString scopeUdn, scopeObject;
    if (parseFolderId(scopeId, &scopeUdn, &scopeObject)) {
        const DlnaClient::ServerInfo *server = m_client.server(scopeUdn);
        if (server && !searchCriteria(query, server->searchCaps).isEmpty())
            targets.append({server->device.udn, scopeObject});
    }
    if (targets.isEmpty()) {
        for (const DlnaClient::ServerInfo &server : m_client.servers()) {
            if (!searchCriteria(query, server.searchCaps).isEmpty())
                targets.append({server.device.udn, QStringLiteral("0")});
        }
    }
    if (targets.isEmpty()) {
        m_status = tr("These media servers can't search.");
        done({});
        return;
    }

    struct Pending
    {
        int remaining = 0;
        QList<LibraryItem> items;
        ItemsCallback done;
    };
    auto pending = std::make_shared<Pending>();
    pending->remaining = int(targets.size());
    pending->done = done;
    for (const auto &[udn, container] : targets) {
        const QStringList caps = m_client.server(udn)->searchCaps;
        // Without class filtering the server sends anything that matches.
        const bool filtered = caps.contains(QStringLiteral("*")) || caps.contains(QStringLiteral("upnp:class"));
        m_client.search(udn, container, searchCriteria(query, caps), 0, MaxSearchResults, m_requests,
                        [this, pending, udn = udn, filtered](const BrowseResult &result) {
                            if (result.ok)
                                pending->items += toItems(udn, parseDidl(result.didl), !filtered);
                            if (--pending->remaining == 0)
                                pending->done(pending->items);
                        });
    }
}

} // namespace newamp::dlna

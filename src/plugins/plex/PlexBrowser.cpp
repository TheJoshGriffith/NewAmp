#include "PlexBrowser.h"

#include "PlexClient.h"
#include "plugins/PluginHost.h"

#include <QBoxLayout>
#include <QComboBox>
#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QTreeWidget>
#include <QTreeWidgetItemIterator>

#include <memory>

namespace newamp::plex {

namespace {
constexpr int KindRole = Qt::UserRole;
constexpr int KeyRole = Qt::UserRole + 1;
constexpr int LoadedRole = Qt::UserRole + 2;

QString formatDuration(qint64 ms)
{
    if (ms <= 0)
        return {};
    const qint64 s = ms / 1000;
    return QStringLiteral("%1:%2").arg(s / 60).arg(s % 60, 2, 10, QLatin1Char('0'));
}
} // namespace

PlexBrowser::PlexBrowser(PlexClient &client, PluginHost &host, QWidget *parent)
    : QDialog(parent)
    , m_client(client)
    , m_host(host)
    , m_servers(new QComboBox(this))
    , m_tree(new QTreeWidget(this))
    , m_status(new QLabel(this))
    , m_account(new QPushButton(this))
{
    setWindowTitle(tr("Plex"));
    resize(560, 480);

    m_tree->setColumnCount(2);
    m_tree->setHeaderLabels({tr("Title"), tr("Info")});
    m_tree->header()->setSectionResizeMode(0, QHeaderView::Stretch);
    m_tree->header()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
    m_tree->header()->setStretchLastSection(false);
    m_tree->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_tree->setExpandsOnDoubleClick(false);
    m_status->setWordWrap(true);

    auto *refresh = new QPushButton(tr("Refresh"), this);
    auto *add = new QPushButton(tr("Add"), this);
    auto *play = new QPushButton(tr("Play"), this);
    auto *close = new QPushButton(tr("Close"), this);

    auto *top = new QHBoxLayout;
    top->addWidget(new QLabel(tr("Server:"), this));
    top->addWidget(m_servers, 1);
    top->addWidget(refresh);
    top->addWidget(m_account);
    auto *bottom = new QHBoxLayout;
    bottom->addWidget(m_status, 1);
    bottom->addWidget(add);
    bottom->addWidget(play);
    bottom->addWidget(close);
    auto *layout = new QVBoxLayout(this);
    layout->addLayout(top);
    layout->addWidget(m_tree, 1);
    layout->addLayout(bottom);

    connect(m_account, &QPushButton::clicked, this, [this] {
        if (m_client.isSignedIn())
            m_client.signOut();
        else
            m_client.signIn();
    });
    connect(refresh, &QPushButton::clicked, &m_client, &PlexClient::refreshServers);
    connect(add, &QPushButton::clicked, this, [this] { useSelection(false); });
    connect(play, &QPushButton::clicked, this, [this] { useSelection(true); });
    connect(close, &QPushButton::clicked, this, &QDialog::close);
    connect(m_servers, &QComboBox::currentIndexChanged, this, &PlexBrowser::showServer);
    connect(m_tree, &QTreeWidget::itemExpanded, this, &PlexBrowser::load);
    connect(m_tree, &QTreeWidget::itemDoubleClicked, this, [this](QTreeWidgetItem *item) {
        const int kind = item->data(0, KindRole).toInt();
        if (kind == Section || kind == Playlists)
            item->setExpanded(!item->isExpanded());
        else
            useSelection(true);
    });
    connect(&m_client, &PlexClient::status, m_status, &QLabel::setText);
    connect(&m_client, &PlexClient::signedInChanged, this, &PlexBrowser::updateAccount);
    connect(&m_client, &PlexClient::serversChanged, this, &PlexBrowser::updateServers);

    updateAccount();
    updateServers();
}

void PlexBrowser::updateAccount()
{
    m_account->setText(m_client.isSignedIn() ? tr("Sign out") : tr("Sign in..."));
    if (!m_client.isSignedIn())
        m_status->setText(tr("Sign in to browse your Plex music."));
}

void PlexBrowser::updateServers()
{
    // Keep the current choice when the list is refreshed.
    const QString current = serverId();
    const QSignalBlocker blocker(m_servers);
    m_servers->clear();
    for (const PlexClient::ServerInfo &server : m_client.servers())
        m_servers->addItem(server.name, server.machineIdentifier);
    const int index = m_servers->findData(current);
    m_servers->setCurrentIndex(index >= 0 ? index : 0);
    if (serverId() != current || m_tree->topLevelItemCount() == 0)
        showServer();
}

QString PlexBrowser::serverId() const
{
    return m_servers->currentData().toString();
}

void PlexBrowser::showServer()
{
    // Dropping the request owner discards replies aimed at the old items.
    delete m_requests;
    m_requests = new QObject(this);
    m_tracks.clear();
    m_tree->clear();
    if (serverId().isEmpty())
        return;

    get(QStringLiteral("/library/sections"), [this](const QByteArray &json) {
        const QList<Directory> sections = parseSections(json);
        for (int i = 0; i < sections.size(); ++i) {
            QTreeWidgetItem *item = addItem(nullptr, Section, sections[i].key, sections[i].title);
            m_tree->insertTopLevelItem(i, m_tree->takeTopLevelItem(m_tree->indexOfTopLevelItem(item)));
        }
        if (sections.isEmpty())
            m_status->setText(tr("This server has no music libraries."));
    });
    addItem(nullptr, Playlists, {}, tr("Playlists"));
}

QTreeWidgetItem *PlexBrowser::addItem(QTreeWidgetItem *parent, Kind kind, const QString &key,
                                      const QString &title, const QString &info)
{
    auto *item = parent ? new QTreeWidgetItem(parent) : new QTreeWidgetItem(m_tree);
    item->setText(0, title);
    item->setText(1, info);
    item->setData(0, KindRole, kind);
    item->setData(0, KeyRole, key);
    if (kind != TrackItem)
        item->setChildIndicatorPolicy(QTreeWidgetItem::ShowIndicator);
    return item;
}

void PlexBrowser::get(const QString &path, const std::function<void(const QByteArray &)> &onJson)
{
    m_client.get(serverId(), path, m_requests, onJson);
}

void PlexBrowser::load(QTreeWidgetItem *item)
{
    if (item->data(0, LoadedRole).toBool())
        return;
    item->setData(0, LoadedRole, true);
    const QString key = item->data(0, KeyRole).toString();
    auto addDirectories = [this, item](Kind kind) {
        return [this, item, kind](const QByteArray &json) {
            for (const Directory &d : parseDirectories(json))
                addItem(item, kind, d.key, d.title, d.subtitle);
            item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
        };
    };

    switch (item->data(0, KindRole).toInt()) {
    case Section:
        get(QStringLiteral("/library/sections/%1/all?type=8").arg(key), addDirectories(Artist));
        break;
    case Artist:
        get(QStringLiteral("/library/metadata/%1/children").arg(key), addDirectories(Album));
        break;
    case Playlists:
        get(QStringLiteral("/playlists?playlistType=audio"), addDirectories(Playlist));
        break;
    case Album:
    case Playlist:
        fetchTracks(item, [this, item](const QList<Track> &tracks) {
            for (const Track &track : tracks) {
                QTreeWidgetItem *child = addItem(item, TrackItem, {}, track.title,
                                                 formatDuration(track.durationMs));
                m_tracks.insert(child, track);
            }
            item->setChildIndicatorPolicy(QTreeWidgetItem::DontShowIndicatorWhenChildless);
        });
        break;
    }
}

void PlexBrowser::fetchTracks(QTreeWidgetItem *item, const std::function<void(const QList<Track> &)> &done)
{
    const QString key = item->data(0, KeyRole).toString();
    QString path;
    switch (item->data(0, KindRole).toInt()) {
    case TrackItem: done({m_tracks.value(item)}); return;
    case Artist: path = QStringLiteral("/library/metadata/%1/allLeaves").arg(key); break;
    case Album: path = QStringLiteral("/library/metadata/%1/children").arg(key); break;
    case Playlist: path = QStringLiteral("/playlists/%1/items").arg(key); break;
    default: done({}); return; // libraries and the playlist folder aren't playable
    }
    const QString server = serverId();
    get(path, [done, server](const QByteArray &json) { done(parseTracks(json, server)); });
}

void PlexBrowser::useSelection(bool play)
{
    // Selected items in tree order; tracks inside a selected album or
    // playlist are covered by it.
    QList<QTreeWidgetItem *> items;
    for (QTreeWidgetItemIterator it(m_tree, QTreeWidgetItemIterator::Selected); *it; ++it) {
        QTreeWidgetItem *parent = (*it)->parent();
        if (!(parent && parent->isSelected()))
            items << *it;
    }
    if (items.isEmpty())
        return;

    // Fetch each item's tracks (some need a request) and deliver them in
    // order once all have arrived.
    struct Pending
    {
        QList<QList<Track>> parts;
        int remaining = 0;
    };
    auto pending = std::make_shared<Pending>();
    pending->parts.resize(items.size());
    pending->remaining = items.size();
    for (int i = 0; i < items.size(); ++i) {
        fetchTracks(items[i], [this, pending, i, play](const QList<Track> &tracks) {
            pending->parts[i] = tracks;
            if (--pending->remaining > 0)
                return;
            QList<Track> all;
            for (const QList<Track> &part : std::as_const(pending->parts))
                all += part;
            if (all.isEmpty())
                return;
            if (play)
                m_host.playTracks(all);
            else
                m_host.addTracks(all);
            m_status->setText(tr("%n track(s) added", nullptr, int(all.size())));
        });
    }
}

} // namespace newamp::plex

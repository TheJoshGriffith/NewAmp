#pragma once

#include "core/Track.h"

#include <QDialog>
#include <QHash>

#include <functional>

class QComboBox;
class QLabel;
class QPushButton;
class QTreeWidget;
class QTreeWidgetItem;

namespace newamp {
class PluginHost;
}

namespace newamp::plex {

class PlexClient;

// A plain dialog for browsing a server's music: libraries, artists, albums,
// tracks and audio playlists, loaded lazily as the user expands them.
// Selected items can be added to the playlist or played.
class PlexBrowser final : public QDialog
{
    Q_OBJECT

public:
    PlexBrowser(PlexClient &client, PluginHost &host, QWidget *parent = nullptr);

private:
    enum Kind { Section = 1, Artist, Album, TrackItem, Playlists, Playlist };

    void updateAccount();
    void updateServers();
    void showServer();
    void load(QTreeWidgetItem *item);
    void fetchTracks(QTreeWidgetItem *item, const std::function<void(const QList<Track> &)> &done);
    void useSelection(bool play);
    void get(const QString &path, const std::function<void(const QByteArray &)> &onJson);
    QTreeWidgetItem *addItem(QTreeWidgetItem *parent, Kind kind, const QString &key, const QString &title,
                             const QString &info = {});
    QString serverId() const;

    PlexClient &m_client;
    PluginHost &m_host;
    QComboBox *m_servers;
    QTreeWidget *m_tree;
    QLabel *m_status;
    QPushButton *m_account;
    QObject *m_requests = nullptr; // parent of in-flight work; replaced with the tree
    QHash<const QTreeWidgetItem *, Track> m_tracks;
};

} // namespace newamp::plex

#pragma once

#include "plugins/LibraryProvider.h"

#include <QObject>

namespace newamp {
class PluginHost;
}

namespace newamp::plex {

class PlexClient;

// The account's Plex music as a library for the Media Library window:
// servers (when there are several), music sections, artists, albums,
// tracks and audio playlists. Search asks the server's own search, within
// the music section it starts from or across every server at the top.
class PlexLibrary final : public QObject, public LibraryProvider
{
    Q_OBJECT

public:
    PlexLibrary(PlexClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent = nullptr);

    QString libraryName() const override { return QStringLiteral("Plex"); }
    void listItems(const QString &folderId, const ItemsCallback &done) override;
    void folderTracks(const QString &folderId, const TracksCallback &done) override;
    QString status() const override { return m_status; }
    QList<QAction *> libraryActions() override { return m_actions; }
    bool canSearch() const override;
    void search(const QString &scopeId, const QString &query, const ItemsCallback &done) override;

private:
    void restart();
    void get(const QString &server, const QString &path, const std::function<void(const QByteArray &)> &onJson);
    void listServer(const QString &server, const ItemsCallback &done);

    PlexClient &m_client;
    PluginHost &m_host;
    QList<QAction *> m_actions;
    QString m_status;
    QObject *m_requests = nullptr; // parent of in-flight work; replaced to drop it
};

} // namespace newamp::plex

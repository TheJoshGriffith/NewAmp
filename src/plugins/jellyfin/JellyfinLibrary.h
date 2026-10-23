#pragma once

#include "plugins/LibraryProvider.h"

#include <QObject>

namespace newamp {
class PluginHost;
}

namespace newamp::jellyfin {

class JellyfinClient;

// The server's music as a library for the Media Library window: music
// libraries, artists, albums, tracks and playlists. Search covers the
// music library it starts from, or everything at the top.
class JellyfinLibrary final : public QObject, public LibraryProvider
{
    Q_OBJECT

public:
    JellyfinLibrary(JellyfinClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent = nullptr);

    QString libraryName() const override { return QStringLiteral("Jellyfin"); }
    void listItems(const QString &folderId, const ItemsCallback &done) override;
    void folderTracks(const QString &folderId, const TracksCallback &done) override;
    QString status() const override { return m_status; }
    QList<QAction *> libraryActions() override { return m_actions; }
    bool canSearch() const override;
    void search(const QString &scopeId, const QString &query, const ItemsCallback &done) override;

private:
    void restart();
    void get(const QString &path, const std::function<void(const QByteArray &)> &onJson);

    JellyfinClient &m_client;
    PluginHost &m_host;
    QList<QAction *> m_actions;
    QString m_status;
    QObject *m_requests = nullptr; // parent of in-flight work; replaced to drop it
};

} // namespace newamp::jellyfin

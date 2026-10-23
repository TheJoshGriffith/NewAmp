#pragma once

#include "SpotifyApi.h"
#include "plugins/LibraryProvider.h"

#include <QObject>

namespace newamp {
class PluginHost;
}

namespace newamp::spotify {

class SpotifyClient;

// The account's Spotify library for the Media Library window: playlists,
// liked songs, saved albums and followed artists, plus search across all
// of Spotify.
class SpotifyLibrary final : public QObject, public LibraryProvider
{
    Q_OBJECT

public:
    SpotifyLibrary(SpotifyClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent = nullptr);

    QString libraryName() const override { return QStringLiteral("Spotify"); }
    void listItems(const QString &folderId, const ItemsCallback &done) override;
    void folderTracks(const QString &folderId, const TracksCallback &done) override;
    bool canSearch() const override;
    void search(const QString &scopeId, const QString &query, const ItemsCallback &done) override;
    QString status() const override { return m_status; }
    QList<QAction *> libraryActions() override { return m_actions; }

private:
    template<typename T>
    using Parser = std::function<Page<T>(const QByteArray &json)>;

    void restart();
    void setStatus(const QString &status);
    // Follows "next" links from path until the last page or limit items.
    template<typename T>
    void getAll(const QString &path, const Parser<T> &parse, int limit, const std::function<void(QList<T>)> &done,
                QList<T> sofar = {});
    void listDirectories(const QString &path, const Parser<Directory> &parse, FolderKind kind,
                         const ItemsCallback &done);
    void albumTracks(const QString &albumId, const TracksCallback &done);
    void artistTracks(QStringList albumIds, QList<Track> sofar, const TracksCallback &done);

    SpotifyClient &m_client;
    PluginHost &m_host;
    QList<QAction *> m_actions;
    QString m_status;
    QObject *m_requests = nullptr; // parent of in-flight work; replaced to drop it
};

} // namespace newamp::spotify

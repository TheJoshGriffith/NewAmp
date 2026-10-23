#pragma once

#include "DlnaApi.h"
#include "plugins/LibraryProvider.h"

#include <QObject>

#include <memory>

namespace newamp {
class PluginHost;
}

namespace newamp::dlna {

class DlnaClient;

// The media servers on the network as a library for the Media Library
// window: one folder per server, then the server's own containers. Servers
// that support ContentDirectory Search can be searched.
class DlnaLibrary final : public QObject, public LibraryProvider
{
    Q_OBJECT

public:
    DlnaLibrary(DlnaClient &client, PluginHost &host, QList<QAction *> actions, QObject *parent = nullptr);

    QString libraryName() const override { return QStringLiteral("DLNA"); }
    void listItems(const QString &folderId, const ItemsCallback &done) override;
    void folderTracks(const QString &folderId, const TracksCallback &done) override;
    bool canSearch() const override;
    void search(const QString &scopeId, const QString &query, const ItemsCallback &done) override;
    QString status() const override { return m_status; }
    QList<QAction *> libraryActions() override { return m_actions; }

private:
    using ObjectsCallback = std::function<void(const QList<DidlObject> &objects)>;
    struct Fetch;

    void restart();
    // Every child of a container, a page at a time, up to a limit. done
    // runs with what arrived, or never when the first page fails.
    void browseAll(const QString &udn, const QString &objectId, const ObjectsCallback &done);
    void browsePage(const std::shared_ptr<Fetch> &fetch, int start);
    QList<LibraryItem> toItems(const QString &udn, const QList<DidlObject> &objects, bool musicOnly) const;

    DlnaClient &m_client;
    PluginHost &m_host;
    QList<QAction *> m_actions;
    QString m_status;
    QObject *m_requests = nullptr; // parent of in-flight work; replaced to drop it
    bool m_atTop = true;           // the window last asked for the list of servers
};

} // namespace newamp::dlna

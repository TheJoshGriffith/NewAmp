#pragma once

#include "DlnaApi.h"
#include "core/MediaSource.h"

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QSet>

#include <functional>

class QNetworkAccessManager;
class QNetworkReply;
class QSettings;
class QTimer;
class QUdpSocket;

namespace newamp::dlna {

// Finds UPnP media servers on the local network with SSDP, keeps track of
// them while they announce themselves, and sends their ContentDirectory
// Browse and Search calls. Also the MediaSource for DLNA tracks, which are
// plain HTTP resources on the server.
class DlnaClient final : public QObject, public MediaSource
{
    Q_OBJECT

public:
    struct ServerInfo
    {
        Device device;
        QUrl location;           // the device description
        QStringList searchCaps;  // empty: the server can't search
        bool manual = false;     // added by address, so it never expires
    };

    using BrowseCallback = std::function<void(const BrowseResult &result)>;

    // settings must outlive the client; servers added by address live there.
    explicit DlnaClient(QSettings &settings, QObject *parent = nullptr);
    ~DlnaClient() override;

    QString id() const override { return QString::fromLatin1(SourceId); }
    QString displayName() const override { return QStringLiteral("DLNA"); }
    QUrl streamUrl(const Track &track) const override;
    QUrl fallbackUrl(const Track &track) const override;

    // Asks every server on the network to answer, then keeps listening for
    // servers that come and go.
    void discover();
    bool isDiscovering() const { return m_discovering; }
    // A server that discovery can't reach (another subnet, a VPN), by its
    // device description URL. Remembered across restarts.
    void addServer(const QUrl &descriptionUrl);

    // Sorted by name.
    QList<ServerInfo> servers() const;
    const ServerInfo *server(const QString &udn) const;

    // ContentDirectory calls. done always runs (result.ok is false when the
    // call failed and status() has said why), unless context is gone.
    void browse(const QString &udn, const QString &objectId, int start, int count, QObject *context,
                const BrowseCallback &done);
    void search(const QString &udn, const QString &containerId, const QString &criteria, int start, int count,
                QObject *context, const BrowseCallback &done);

Q_SIGNALS:
    // A server appeared, left or moved.
    void serversChanged();
    // A server's search capabilities became known.
    void searchCapabilitiesChanged();
    // Human-readable progress and errors for the browser's status line.
    void status(const QString &message);

private:
    void listen();
    void sendSearch();
    void readDatagrams(QUdpSocket *socket);
    void handle(const SsdpMessage &message);
    void fetchDescription(const QUrl &location, bool manual, int maxAgeS);
    void fetchSearchCapabilities(const QString &udn);
    void call(const QString &udn, const QString &action, const QByteArray &body, QObject *context,
              const std::function<void(QNetworkReply *reply)> &done);
    void expire();

    QSettings &m_settings;
    QNetworkAccessManager *m_network;
    QUdpSocket *m_searchSocket = nullptr; // sends M-SEARCH and gets the answers
    QUdpSocket *m_notifySocket = nullptr; // hears announcements; may fail to bind
    QTimer *m_expiryTimer;
    QTimer *m_searchDone;
    QElapsedTimer m_clock;
    bool m_discovering = false;
    QHash<QString, ServerInfo> m_servers; // by lower-case UDN
    QHash<QString, qint64> m_expires;     // by lower-case UDN, in m_clock time
    QSet<QString> m_fetching;             // description URLs being fetched
};

} // namespace newamp::dlna

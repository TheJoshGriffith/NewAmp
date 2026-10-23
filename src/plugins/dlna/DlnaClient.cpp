#include "DlnaClient.h"

#include <QNetworkAccessManager>
#include <QNetworkDatagram>
#include <QNetworkInterface>
#include <QNetworkReply>
#include <QSettings>
#include <QTimer>
#include <QUdpSocket>

#include <algorithm>

namespace newamp::dlna {

namespace {
constexpr int RequestTimeoutMs = 15000;
constexpr int DescriptionTimeoutMs = 5000;
constexpr int SearchWaitS = 2;          // MX: how long servers may wait to answer
constexpr int SearchRepeatMs = 1000;    // UDP gets lost, so ask twice
constexpr int SearchDoneMs = 4000;      // after this, no answer means no servers
constexpr int ExpiryCheckMs = 5 * 60 * 1000;
constexpr int ExpiryGraceMs = 60 * 1000;
const QString ManualKey = QStringLiteral("manualServers");
const QByteArray UserAgent = "NewAmp/" NEWAMP_VERSION " UPnP/1.0 DLNADOC/1.50";

QString key(const QString &udn)
{
    return udn.toLower();
}

// Runs done(reply) when the reply finishes, unless context is gone by then.
// The reply is freed either way.
void onFinished(QNetworkReply *reply, QObject *context, std::function<void(QNetworkReply *)> done)
{
    QObject::connect(reply, &QNetworkReply::finished, reply, &QObject::deleteLater);
    QObject::connect(reply, &QNetworkReply::finished, context, [reply, done = std::move(done)] { done(reply); });
}

// The interfaces SSDP can use: up, multicast-capable, with an IPv4 address.
QList<QNetworkInterface> multicastInterfaces()
{
    QList<QNetworkInterface> result;
    for (const QNetworkInterface &iface : QNetworkInterface::allInterfaces()) {
        const auto flags = iface.flags();
        if (!(flags & QNetworkInterface::IsUp) || !(flags & QNetworkInterface::IsRunning)
            || !(flags & QNetworkInterface::CanMulticast) || (flags & QNetworkInterface::IsLoopBack))
            continue;
        const QList<QNetworkAddressEntry> entries = iface.addressEntries();
        if (std::any_of(entries.begin(), entries.end(), [](const QNetworkAddressEntry &e) {
                return e.ip().protocol() == QAbstractSocket::IPv4Protocol;
            }))
            result << iface;
    }
    return result;
}
} // namespace

DlnaClient::DlnaClient(QSettings &settings, QObject *parent)
    : QObject(parent)
    , m_settings(settings)
    , m_network(new QNetworkAccessManager(this))
    , m_expiryTimer(new QTimer(this))
    , m_searchDone(new QTimer(this))
{
    m_clock.start();
    m_expiryTimer->setInterval(ExpiryCheckMs);
    connect(m_expiryTimer, &QTimer::timeout, this, &DlnaClient::expire);
    m_searchDone->setSingleShot(true);
    m_searchDone->setInterval(SearchDoneMs);
    connect(m_searchDone, &QTimer::timeout, this, [this] {
        m_discovering = false;
        if (m_servers.isEmpty())
            Q_EMIT status(tr("No media servers found. Right-click here to look again or add one by address."));
        Q_EMIT serversChanged(); // the library stops saying it's looking
    });
}

DlnaClient::~DlnaClient() = default;

QUrl DlnaClient::streamUrl(const Track &track) const
{
    const TrackRef ref = parseTrackLocation(track.location);
    if (!ref.isValid())
        return {};
    const ServerInfo *info = server(ref.udn);
    return relocate(ref.resource, ref.host, info ? info->location.host() : QString());
}

QUrl DlnaClient::fallbackUrl(const Track &track) const
{
    const TrackRef ref = parseTrackLocation(track.location);
    if (!ref.isValid() || !ref.alternative.isValid())
        return {};
    const ServerInfo *info = server(ref.udn);
    return relocate(ref.alternative, ref.host, info ? info->location.host() : QString());
}

void DlnaClient::discover()
{
    listen();
    m_discovering = true;
    if (m_servers.isEmpty())
        Q_EMIT status(tr("Looking for media servers..."));
    sendSearch();
    QTimer::singleShot(SearchRepeatMs, this, &DlnaClient::sendSearch);
    m_searchDone->start();

    // Servers added by address answer directly.
    for (const QString &url : m_settings.value(ManualKey).toStringList())
        fetchDescription(QUrl(url), true, 0);
}

void DlnaClient::listen()
{
    if (m_searchSocket)
        return;
    const QHostAddress group(QString::fromLatin1(SsdpAddress));
    m_searchSocket = new QUdpSocket(this);
    if (!m_searchSocket->bind(QHostAddress::AnyIPv4, 0))
        Q_EMIT status(tr("Could not open a network socket: %1").arg(m_searchSocket->errorString()));
    m_searchSocket->setSocketOption(QAbstractSocket::MulticastTtlOption, 2);
    connect(m_searchSocket, &QUdpSocket::readyRead, this, [this] { readDatagrams(m_searchSocket); });

    // Announcements arrive on the SSDP port, which other UPnP software on
    // this computer may share. Without it, servers are still found by
    // searching, just not the moment they start.
    m_notifySocket = new QUdpSocket(this);
    if (m_notifySocket->bind(QHostAddress::AnyIPv4, SsdpPort,
                             QAbstractSocket::ShareAddress | QAbstractSocket::ReuseAddressHint)) {
        for (const QNetworkInterface &iface : multicastInterfaces())
            m_notifySocket->joinMulticastGroup(group, iface);
        connect(m_notifySocket, &QUdpSocket::readyRead, this, [this] { readDatagrams(m_notifySocket); });
    }
    m_expiryTimer->start();
}

void DlnaClient::sendSearch()
{
    if (!m_searchSocket)
        return;
    const QHostAddress group(QString::fromLatin1(SsdpAddress));
    const QByteArray request = searchRequest(SearchWaitS);
    // Send on every network, not just the default route's.
    const QList<QNetworkInterface> interfaces = multicastInterfaces();
    for (const QNetworkInterface &iface : interfaces) {
        m_searchSocket->setMulticastInterface(iface);
        m_searchSocket->writeDatagram(request, group, SsdpPort);
    }
    if (interfaces.isEmpty())
        m_searchSocket->writeDatagram(request, group, SsdpPort);
}

void DlnaClient::readDatagrams(QUdpSocket *socket)
{
    while (socket->hasPendingDatagrams()) {
        const QNetworkDatagram datagram = socket->receiveDatagram();
        if (const std::optional<SsdpMessage> message = parseSsdp(datagram.data()))
            handle(*message);
    }
}

void DlnaClient::handle(const SsdpMessage &message)
{
    if (!isMediaServer(message))
        return;
    const QString k = key(message.udn);
    if (message.byebye) {
        auto it = m_servers.find(k);
        if (it != m_servers.end() && !it->manual) {
            m_servers.erase(it);
            m_expires.remove(k);
            Q_EMIT serversChanged();
        }
        return;
    }
    const auto it = m_servers.constFind(k);
    if (it != m_servers.constEnd() && it->location == message.location) {
        m_expires[k] = m_clock.elapsed() + qint64(message.maxAgeS) * 1000 + ExpiryGraceMs;
        return;
    }
    // New, or at a new address.
    fetchDescription(message.location, false, message.maxAgeS);
}

void DlnaClient::fetchDescription(const QUrl &location, bool manual, int maxAgeS)
{
    const QString url = location.toString();
    if (!location.isValid() || m_fetching.contains(url))
        return;
    m_fetching.insert(url);
    QNetworkRequest request(location);
    request.setTransferTimeout(DescriptionTimeoutMs);
    request.setHeader(QNetworkRequest::UserAgentHeader, UserAgent);
    onFinished(m_network->get(request), this, [this, location, url, manual, maxAgeS](QNetworkReply *reply) {
        m_fetching.remove(url);
        const std::optional<Device> device = reply->error() == QNetworkReply::NoError
                                               ? parseDeviceDescription(reply->readAll(), location)
                                               : std::nullopt;
        if (!device) {
            // Servers that announce themselves and then don't answer are
            // common (sleeping NAS); only say so for ones the user added.
            if (manual)
                Q_EMIT status(tr("%1 is not a media server: %2")
                                  .arg(location.toDisplayString(), reply->error() == QNetworkReply::NoError
                                                                       ? tr("no ContentDirectory service")
                                                                       : reply->errorString()));
            return;
        }
        const QString k = key(device->udn);
        ServerInfo &info = m_servers[k];
        const bool changed = info.device.udn.isEmpty() || info.location != location
                          || info.device.name != device->name || info.device.controlUrl != device->controlUrl;
        info.device = *device;
        info.location = location;
        info.manual = info.manual || manual;
        if (info.manual)
            m_expires.remove(k);
        else
            m_expires[k] = m_clock.elapsed() + qint64(maxAgeS) * 1000 + ExpiryGraceMs;
        if (changed) {
            Q_EMIT status(QString());
            Q_EMIT serversChanged();
            fetchSearchCapabilities(device->udn);
        }
    });
}

void DlnaClient::addServer(const QUrl &descriptionUrl)
{
    if (!descriptionUrl.isValid())
        return;
    QStringList manual = m_settings.value(ManualKey).toStringList();
    if (!manual.contains(descriptionUrl.toString())) {
        manual << descriptionUrl.toString();
        m_settings.setValue(ManualKey, manual);
    }
    fetchDescription(descriptionUrl, true, 0);
}

QList<DlnaClient::ServerInfo> DlnaClient::servers() const
{
    QList<ServerInfo> list = m_servers.values();
    std::sort(list.begin(), list.end(), [](const ServerInfo &a, const ServerInfo &b) {
        return a.device.name.localeAwareCompare(b.device.name) < 0;
    });
    return list;
}

const DlnaClient::ServerInfo *DlnaClient::server(const QString &udn) const
{
    const auto it = m_servers.constFind(key(udn));
    return it == m_servers.constEnd() ? nullptr : &*it;
}

void DlnaClient::fetchSearchCapabilities(const QString &udn)
{
    const ServerInfo *info = server(udn);
    if (!info)
        return;
    const QByteArray body = soapEnvelope(info->device.serviceType, QStringLiteral("GetSearchCapabilities"), {});
    call(udn, QStringLiteral("GetSearchCapabilities"), body, this, [this, udn](QNetworkReply *reply) {
        auto it = m_servers.find(key(udn));
        if (it == m_servers.end() || reply->error() != QNetworkReply::NoError)
            return; // no search, which is allowed
        it->searchCaps = parseSearchCapabilities(reply->readAll());
        Q_EMIT searchCapabilitiesChanged();
    });
}

void DlnaClient::call(const QString &udn, const QString &action, const QByteArray &body, QObject *context,
                      const std::function<void(QNetworkReply *reply)> &done)
{
    const ServerInfo *info = server(udn);
    if (!info)
        return;
    QNetworkRequest request(info->device.controlUrl);
    request.setTransferTimeout(RequestTimeoutMs);
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("text/xml; charset=\"utf-8\""));
    request.setHeader(QNetworkRequest::UserAgentHeader, UserAgent);
    request.setRawHeader("SOAPACTION", soapAction(info->device.serviceType, action));
    onFinished(m_network->post(request, body), context, done);
}

void DlnaClient::browse(const QString &udn, const QString &objectId, int start, int count, QObject *context,
                        const BrowseCallback &done)
{
    const ServerInfo *info = server(udn);
    if (!info) {
        Q_EMIT status(tr("That media server is no longer on the network."));
        done({});
        return;
    }
    const QString name = info->device.name;
    call(udn, QStringLiteral("Browse"), browseEnvelope(info->device.serviceType, objectId, start, count), context,
         [this, done, name](QNetworkReply *reply) {
             const QByteArray xml = reply->readAll();
             if (reply->error() != QNetworkReply::NoError) {
                 const QString fault = parseSoapFault(xml);
                 Q_EMIT status(tr("%1: %2").arg(name, fault.isEmpty() ? reply->errorString() : fault));
                 done({});
                 return;
             }
             const BrowseResult result = parseBrowseResponse(xml);
             if (!result.ok)
                 Q_EMIT status(tr("%1 sent a reply NewAmp can't read").arg(name));
             done(result);
         });
}

void DlnaClient::search(const QString &udn, const QString &containerId, const QString &criteria, int start,
                        int count, QObject *context, const BrowseCallback &done)
{
    const ServerInfo *info = server(udn);
    if (!info) {
        done({});
        return;
    }
    const QString name = info->device.name;
    call(udn, QStringLiteral("Search"),
         searchEnvelope(info->device.serviceType, containerId, criteria, start, count), context,
         [this, done, name](QNetworkReply *reply) {
             const QByteArray xml = reply->readAll();
             if (reply->error() != QNetworkReply::NoError) {
                 const QString fault = parseSoapFault(xml);
                 Q_EMIT status(tr("%1 could not search: %2").arg(name, fault.isEmpty() ? reply->errorString() : fault));
                 done({});
                 return;
             }
             done(parseBrowseResponse(xml));
         });
}

void DlnaClient::expire()
{
    const qint64 now = m_clock.elapsed();
    bool changed = false;
    for (auto it = m_expires.begin(); it != m_expires.end();) {
        if (it.value() < now) {
            m_servers.remove(it.key());
            it = m_expires.erase(it);
            changed = true;
        } else {
            ++it;
        }
    }
    if (changed)
        Q_EMIT serversChanged();
    // Servers repeat their announcements, but not every network passes
    // them on, so ask again now and then.
    sendSearch();
}

} // namespace newamp::dlna

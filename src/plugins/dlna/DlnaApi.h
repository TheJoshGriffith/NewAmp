#pragma once

#include "core/Track.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

// Pure helpers for UPnP AV / DLNA media servers: SSDP messages, device
// descriptions, ContentDirectory SOAP calls and DIDL-Lite. Nothing here
// touches the network, so all of it is unit-tested with fixtures.
namespace newamp::dlna {

inline constexpr const char *SourceId = "dlna";

// SSDP: discovery over multicast UDP.
inline constexpr const char *SsdpAddress = "239.255.255.250";
inline constexpr quint16 SsdpPort = 1900;
inline constexpr const char *MediaServerType = "urn:schemas-upnp-org:device:MediaServer:1";

// The M-SEARCH datagram that asks media servers to answer.
QByteArray searchRequest(int maxWaitS = 2);

// A search response or a NOTIFY announcement.
struct SsdpMessage
{
    QUrl location; // the device description
    QString udn;   // "uuid:...", from the USN
    QString type;  // ST (responses) or NT (announcements)
    bool byebye = false;
    int maxAgeS = 1800;
};

// Empty for anything that isn't an SSDP response or NOTIFY.
std::optional<SsdpMessage> parseSsdp(const QByteArray &datagram);
// Whether the message is about a media server rather than another device.
bool isMediaServer(const SsdpMessage &message);

// The part of a device description NewAmp needs: who the server is and
// where to send ContentDirectory calls.
struct Device
{
    QString udn;
    QString name;
    QUrl controlUrl;     // ContentDirectory control URL, absolute
    QString serviceType; // e.g. urn:schemas-upnp-org:service:ContentDirectory:1
};

// The first device (root or embedded) that offers a ContentDirectory.
// Relative URLs resolve against URLBase, or the description's location.
std::optional<Device> parseDeviceDescription(const QByteArray &xml, const QUrl &location);

// A SOAP request body for a ContentDirectory action.
QByteArray soapEnvelope(const QString &serviceType, const QString &action,
                        const QList<std::pair<QString, QString>> &arguments);
QByteArray browseEnvelope(const QString &serviceType, const QString &objectId, int start, int count);
QByteArray searchEnvelope(const QString &serviceType, const QString &containerId, const QString &criteria,
                          int start, int count);
// The SOAPACTION header value.
QByteArray soapAction(const QString &serviceType, const QString &action);

// A Browse or Search answer: the DIDL-Lite document and the counts.
struct BrowseResult
{
    QString didl;
    int returned = 0;
    int total = 0; // 0 when the server doesn't know
    bool ok = false;
};

BrowseResult parseBrowseResponse(const QByteArray &xml);
// The properties a server can search on; empty when it can't search.
QStringList parseSearchCapabilities(const QByteArray &xml);
// The fault's description, for the status line; empty when there's none.
QString parseSoapFault(const QByteArray &xml);

struct Resource
{
    QUrl url;
    QString mimeType; // from protocolInfo, e.g. audio/flac
    qint64 durationMs = -1;
};

// A container or item from DIDL-Lite.
struct DidlObject
{
    QString id;
    QString title;
    QString upnpClass; // e.g. object.item.audioItem.musicTrack
    QString artist;
    QString album;
    QString date;
    int childCount = -1;
    bool isContainer = false;
    QList<Resource> resources;
};

QList<DidlObject> parseDidl(const QString &didl);
// A playable audio item: an audioItem, or any item with an audio resource.
bool isAudioItem(const DidlObject &object);
// "H:MM:SS", "H:MM:SS.mmm" or "H:MM:SS.F/D"; -1 when unparsable.
qint64 parseDuration(const QString &text);

// Search criteria for tracks, albums and artists whose names contain
// query, using only the properties the server can search on. Empty when
// it can't search on any of them.
QString searchCriteria(const QString &query, const QStringList &capabilities);

// What a DLNA track's location encodes:
// dlna://<udn>?res=<url>&alt=<url>&host=<description host>
// The server's host is kept so the track still plays when the server gets
// a new address; alt is a second resource to fall back to.
struct TrackRef
{
    QString udn;
    QUrl resource;
    QUrl alternative;
    QString host; // the description's host when the track was listed
    bool isValid() const { return !udn.isEmpty() && resource.isValid(); }
};

QUrl trackLocation(const TrackRef &ref);
TrackRef parseTrackLocation(const QUrl &location);
// The resource at the server's current host, when it moved since.
QUrl relocate(const QUrl &resource, const QString &oldHost, const QString &newHost);

// A playable track from an audio item; empty when it has no resource.
std::optional<Track> trackFromItem(const DidlObject &item, const QString &udn, const QString &host);

// Media Library folder ids are "<udn>|<object id>"; object ids may contain
// anything, so only the first separator counts.
QString folderId(const QString &udn, const QString &objectId);
bool parseFolderId(const QString &id, QString *udn, QString *objectId);

} // namespace newamp::dlna

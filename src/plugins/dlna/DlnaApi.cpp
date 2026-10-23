#include "DlnaApi.h"

#include <QRegularExpression>
#include <QXmlStreamReader>

namespace newamp::dlna {

namespace {

const QString TrackClass = QStringLiteral("object.item.audioItem");
const QString AlbumClass = QStringLiteral("object.container.album.musicAlbum");
const QString ArtistClass = QStringLiteral("object.container.person.musicArtist");

QString escaped(const QString &text)
{
    return text.toHtmlEscaped(); // also escapes quotes, which SOAP doesn't mind
}

QString argumentXml(const QString &name, const QString &value)
{
    return QStringLiteral("<%1>%2</%1>").arg(name, escaped(value));
}

QString encoded(const QString &text)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(text));
}

} // namespace

QByteArray searchRequest(int maxWaitS)
{
    return QStringLiteral("M-SEARCH * HTTP/1.1\r\n"
                          "HOST: %1:%2\r\n"
                          "MAN: \"ssdp:discover\"\r\n"
                          "MX: %3\r\n"
                          "ST: %4\r\n"
                          "USER-AGENT: NewAmp/%5 UPnP/1.0 DLNADOC/1.50\r\n"
                          "\r\n")
        .arg(QLatin1String(SsdpAddress))
        .arg(SsdpPort)
        .arg(maxWaitS)
        .arg(QLatin1String(MediaServerType), QLatin1String(NEWAMP_VERSION))
        .toLatin1();
}

std::optional<SsdpMessage> parseSsdp(const QByteArray &datagram)
{
    const QList<QByteArray> lines = datagram.split('\n');
    if (lines.isEmpty())
        return std::nullopt;
    const QByteArray first = lines.first().trimmed().toUpper();
    const bool response = first.startsWith("HTTP/1.1 200") || first.startsWith("HTTP/1.0 200");
    const bool notify = first.startsWith("NOTIFY ");
    if (!response && !notify)
        return std::nullopt;

    SsdpMessage message;
    QString usn;
    for (int i = 1; i < lines.size(); ++i) {
        const QByteArray &line = lines[i];
        const int colon = line.indexOf(':');
        if (colon <= 0)
            continue;
        const QByteArray name = line.left(colon).trimmed().toUpper();
        const QString value = QString::fromUtf8(line.mid(colon + 1).trimmed());
        if (name == "LOCATION")
            message.location = QUrl(value);
        else if (name == "USN")
            usn = value;
        else if (name == (notify ? "NT" : "ST"))
            message.type = value;
        else if (name == "NTS")
            message.byebye = value.compare(QLatin1String("ssdp:byebye"), Qt::CaseInsensitive) == 0;
        else if (name == "CACHE-CONTROL") {
            static const QRegularExpression maxAge(QStringLiteral("max-age\\s*=\\s*(\\d+)"),
                                                   QRegularExpression::CaseInsensitiveOption);
            const QRegularExpressionMatch match = maxAge.match(value);
            if (match.hasMatch())
                message.maxAgeS = match.captured(1).toInt();
        }
    }
    // The USN is "<udn>" or "<udn>::<type>".
    message.udn = usn.section(QStringLiteral("::"), 0, 0).trimmed();
    if (message.udn.isEmpty())
        return std::nullopt;
    if (!message.byebye && !message.location.isValid())
        return std::nullopt;
    return message;
}

bool isMediaServer(const SsdpMessage &message)
{
    return message.type.contains(QLatin1String(":device:MediaServer:"), Qt::CaseInsensitive)
        || message.type.contains(QLatin1String(":service:ContentDirectory:"), Qt::CaseInsensitive);
}

std::optional<Device> parseDeviceDescription(const QByteArray &xml, const QUrl &location)
{
    struct Pending
    {
        QString udn;
        QString name;
        QString control;
        QString serviceType;
    };
    QList<Pending> devices; // the device elements we are inside, innermost last
    std::optional<Pending> found;
    QString urlBase;
    QString serviceType, controlUrl; // of the service being read
    bool inService = false;

    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QStringView name = reader.name();
            if (name == QLatin1String("device")) {
                devices.append(Pending{});
            } else if (name == QLatin1String("service")) {
                inService = true;
                serviceType.clear();
                controlUrl.clear();
            } else if (name == QLatin1String("URLBase")) {
                urlBase = reader.readElementText().trimmed();
            } else if (inService && name == QLatin1String("serviceType")) {
                serviceType = reader.readElementText().trimmed();
            } else if (inService && name == QLatin1String("controlURL")) {
                controlUrl = reader.readElementText().trimmed();
            } else if (!devices.isEmpty() && !inService && name == QLatin1String("UDN")) {
                devices.last().udn = reader.readElementText().trimmed();
            } else if (!devices.isEmpty() && !inService && name == QLatin1String("friendlyName")) {
                devices.last().name = reader.readElementText().trimmed();
            }
        } else if (reader.isEndElement()) {
            const QStringView name = reader.name();
            if (name == QLatin1String("service")) {
                inService = false;
                if (!devices.isEmpty() && devices.last().control.isEmpty() && !controlUrl.isEmpty()
                    && serviceType.contains(QLatin1String(":service:ContentDirectory:"))) {
                    devices.last().control = controlUrl;
                    devices.last().serviceType = serviceType;
                }
            } else if (name == QLatin1String("device") && !devices.isEmpty()) {
                const Pending device = devices.takeLast();
                if (!found && !device.control.isEmpty() && !device.udn.isEmpty())
                    found = device;
            }
        }
    }
    if (reader.hasError() || !found)
        return std::nullopt;

    const QUrl base = urlBase.isEmpty() ? location : QUrl(urlBase);
    Device device;
    device.udn = found->udn;
    device.name = found->name.isEmpty() ? location.host() : found->name;
    device.controlUrl = base.resolved(QUrl(found->control));
    device.serviceType = found->serviceType;
    return device;
}

QByteArray soapEnvelope(const QString &serviceType, const QString &action,
                        const QList<std::pair<QString, QString>> &arguments)
{
    QString body;
    for (const auto &[name, value] : arguments)
        body += argumentXml(name, value);
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"utf-8\"?>\n"
                          "<s:Envelope xmlns:s=\"http://schemas.xmlsoap.org/soap/envelope/\" "
                          "s:encodingStyle=\"http://schemas.xmlsoap.org/soap/encoding/\">"
                          "<s:Body><u:%1 xmlns:u=\"%2\">%3</u:%1></s:Body></s:Envelope>")
        .arg(action, escaped(serviceType), body)
        .toUtf8();
}

QByteArray browseEnvelope(const QString &serviceType, const QString &objectId, int start, int count)
{
    return soapEnvelope(serviceType, QStringLiteral("Browse"),
                        {{QStringLiteral("ObjectID"), objectId},
                         {QStringLiteral("BrowseFlag"), QStringLiteral("BrowseDirectChildren")},
                         {QStringLiteral("Filter"), QStringLiteral("*")},
                         {QStringLiteral("StartingIndex"), QString::number(start)},
                         {QStringLiteral("RequestedCount"), QString::number(count)},
                         {QStringLiteral("SortCriteria"), {}}});
}

QByteArray searchEnvelope(const QString &serviceType, const QString &containerId, const QString &criteria,
                          int start, int count)
{
    return soapEnvelope(serviceType, QStringLiteral("Search"),
                        {{QStringLiteral("ContainerID"), containerId},
                         {QStringLiteral("SearchCriteria"), criteria},
                         {QStringLiteral("Filter"), QStringLiteral("*")},
                         {QStringLiteral("StartingIndex"), QString::number(start)},
                         {QStringLiteral("RequestedCount"), QString::number(count)},
                         {QStringLiteral("SortCriteria"), {}}});
}

QByteArray soapAction(const QString &serviceType, const QString &action)
{
    return QStringLiteral("\"%1#%2\"").arg(serviceType, action).toUtf8();
}

BrowseResult parseBrowseResponse(const QByteArray &xml)
{
    BrowseResult result;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QStringView name = reader.name();
        if (name == QLatin1String("Result")) {
            result.didl = reader.readElementText();
            result.ok = true;
        } else if (name == QLatin1String("NumberReturned")) {
            result.returned = reader.readElementText().trimmed().toInt();
        } else if (name == QLatin1String("TotalMatches")) {
            result.total = reader.readElementText().trimmed().toInt();
        }
    }
    if (reader.hasError() && reader.error() != QXmlStreamReader::PrematureEndOfDocumentError)
        result.ok = false;
    return result;
}

QStringList parseSearchCapabilities(const QByteArray &xml)
{
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement() && reader.name() == QLatin1String("SearchCaps")) {
            QStringList caps;
            for (const QString &cap : reader.readElementText().split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                if (!cap.trimmed().isEmpty())
                    caps << cap.trimmed();
            }
            return caps;
        }
    }
    return {};
}

QString parseSoapFault(const QByteArray &xml)
{
    QString faultString, description, code;
    QXmlStreamReader reader(xml);
    while (!reader.atEnd()) {
        reader.readNext();
        if (!reader.isStartElement())
            continue;
        const QStringView name = reader.name();
        if (name == QLatin1String("faultstring"))
            faultString = reader.readElementText().trimmed();
        else if (name == QLatin1String("errorDescription"))
            description = reader.readElementText().trimmed();
        else if (name == QLatin1String("errorCode"))
            code = reader.readElementText().trimmed();
    }
    if (!description.isEmpty())
        return code.isEmpty() ? description : QStringLiteral("%1 (%2)").arg(description, code);
    if (!code.isEmpty())
        return QStringLiteral("UPnP error %1").arg(code);
    return faultString;
}

QList<DidlObject> parseDidl(const QString &didl)
{
    QList<DidlObject> objects;
    QXmlStreamReader reader(didl);
    std::optional<DidlObject> current;
    QString performer, anyArtist, creator;

    while (!reader.atEnd()) {
        reader.readNext();
        if (reader.isStartElement()) {
            const QStringView name = reader.name();
            const QXmlStreamAttributes attributes = reader.attributes();
            if (!current && (name == QLatin1String("container") || name == QLatin1String("item"))) {
                current = DidlObject{};
                current->id = attributes.value(QLatin1String("id")).toString();
                current->isContainer = name == QLatin1String("container");
                bool ok = false;
                const int children = attributes.value(QLatin1String("childCount")).toInt(&ok);
                current->childCount = ok ? children : -1;
                performer.clear();
                anyArtist.clear();
                creator.clear();
            } else if (!current) {
                continue;
            } else if (name == QLatin1String("title")) {
                current->title = reader.readElementText().trimmed();
            } else if (name == QLatin1String("class")) {
                current->upnpClass = reader.readElementText().trimmed();
            } else if (name == QLatin1String("artist")) {
                const QString role = attributes.value(QLatin1String("role")).toString();
                const QString artist = reader.readElementText().trimmed();
                if (anyArtist.isEmpty())
                    anyArtist = artist;
                if (performer.isEmpty() && (role.isEmpty() || role.compare(QLatin1String("Performer"), Qt::CaseInsensitive) == 0))
                    performer = artist;
            } else if (name == QLatin1String("creator")) {
                creator = reader.readElementText().trimmed();
            } else if (name == QLatin1String("album")) {
                current->album = reader.readElementText().trimmed();
            } else if (name == QLatin1String("date")) {
                current->date = reader.readElementText().trimmed();
            } else if (name == QLatin1String("res")) {
                const QStringList protocol = attributes.value(QLatin1String("protocolInfo")).toString().split(QLatin1Char(':'));
                Resource res;
                res.durationMs = parseDuration(attributes.value(QLatin1String("duration")).toString());
                res.url = QUrl(reader.readElementText().trimmed());
                // Only resources fetched over HTTP are playable here.
                const bool http = protocol.isEmpty() || protocol.first().isEmpty()
                               || protocol.first().compare(QLatin1String("http-get"), Qt::CaseInsensitive) == 0;
                if (protocol.size() > 2)
                    res.mimeType = protocol[2].trimmed().toLower();
                const QString scheme = res.url.scheme().toLower();
                if (http && (scheme == QLatin1String("http") || scheme == QLatin1String("https")))
                    current->resources << res;
            }
        } else if (reader.isEndElement() && current
                   && (reader.name() == QLatin1String("container") || reader.name() == QLatin1String("item"))) {
            current->artist = !performer.isEmpty() ? performer : !anyArtist.isEmpty() ? anyArtist : creator;
            objects << *std::exchange(current, std::nullopt);
        }
    }
    return objects;
}

bool isAudioItem(const DidlObject &object)
{
    if (object.isContainer || object.resources.isEmpty())
        return false;
    if (object.upnpClass.startsWith(TrackClass))
        return true;
    for (const Resource &res : object.resources) {
        if (res.mimeType.startsWith(QLatin1String("audio/")))
            return true;
    }
    return false;
}

qint64 parseDuration(const QString &text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\+?(\\d+):(\\d{1,2}):(\\d{1,2})(?:\\.(\\d+)(?:/(\\d+))?)?$"));
    const QRegularExpressionMatch match = pattern.match(text.trimmed());
    if (!match.hasMatch())
        return -1;
    qint64 ms = (match.captured(1).toLongLong() * 3600 + match.captured(2).toLongLong() * 60
                 + match.captured(3).toLongLong()) * 1000;
    const QString fraction = match.captured(4);
    if (!fraction.isEmpty()) {
        const QString denominator = match.captured(5);
        if (!denominator.isEmpty()) {
            const qint64 d = denominator.toLongLong();
            if (d > 0)
                ms += fraction.toLongLong() * 1000 / d;
        } else {
            ms += QStringView(fraction).left(3).toString().leftJustified(3, QLatin1Char('0')).toLongLong();
        }
    }
    return ms;
}

QString searchCriteria(const QString &query, const QStringList &capabilities)
{
    const QString text = query.simplified();
    if (text.isEmpty() || capabilities.isEmpty())
        return {};
    const bool all = capabilities.contains(QStringLiteral("*"));
    auto can = [&](const char *property) { return all || capabilities.contains(QLatin1String(property)); };

    QString value = text;
    value.replace(QLatin1Char('\\'), QLatin1String("\\\\"));
    value.replace(QLatin1Char('"'), QLatin1String("\\\""));
    const QString quoted = QLatin1Char('"') + value + QLatin1Char('"');

    QStringList matches;
    for (const char *property : {"dc:title", "upnp:artist", "dc:creator", "upnp:album"}) {
        if (can(property))
            matches << QStringLiteral("%1 contains %2").arg(QLatin1String(property), quoted);
    }
    if (matches.isEmpty())
        return {};
    // Without class filtering the library drops what isn't music itself.
    if (!can("upnp:class"))
        return matches.join(QLatin1String(" or "));

    QStringList parts;
    parts << QStringLiteral("(upnp:class derivedfrom \"%1\" and (%2))").arg(TrackClass, matches.join(QLatin1String(" or ")));
    if (can("dc:title")) {
        for (const QString &cls : {AlbumClass, ArtistClass})
            parts << QStringLiteral("(upnp:class derivedfrom \"%1\" and dc:title contains %2)").arg(cls, quoted);
    }
    return parts.join(QLatin1String(" or "));
}

QUrl trackLocation(const TrackRef &ref)
{
    QString query = QStringLiteral("udn=") + encoded(ref.udn) + QStringLiteral("&res=")
                  + encoded(ref.resource.toString(QUrl::FullyEncoded));
    if (ref.alternative.isValid())
        query += QStringLiteral("&alt=") + encoded(ref.alternative.toString(QUrl::FullyEncoded));
    if (!ref.host.isEmpty())
        query += QStringLiteral("&host=") + encoded(ref.host);
    QUrl url;
    url.setScheme(QString::fromLatin1(SourceId));
    url.setPath(QStringLiteral("track"));
    url.setQuery(query, QUrl::StrictMode);
    return url;
}

TrackRef parseTrackLocation(const QUrl &location)
{
    TrackRef ref;
    if (location.scheme() != QLatin1String(SourceId))
        return ref;
    const QString query = location.query(QUrl::FullyEncoded);
    for (const QString &pair : query.split(QLatin1Char('&'), Qt::SkipEmptyParts)) {
        const int eq = pair.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        const QString key = pair.left(eq);
        const QString value = QUrl::fromPercentEncoding(pair.mid(eq + 1).toLatin1());
        if (key == QLatin1String("udn"))
            ref.udn = value;
        else if (key == QLatin1String("res"))
            ref.resource = QUrl(value, QUrl::StrictMode);
        else if (key == QLatin1String("alt"))
            ref.alternative = QUrl(value, QUrl::StrictMode);
        else if (key == QLatin1String("host"))
            ref.host = value;
    }
    return ref;
}

QUrl relocate(const QUrl &resource, const QString &oldHost, const QString &newHost)
{
    if (oldHost.isEmpty() || newHost.isEmpty() || oldHost.compare(newHost, Qt::CaseInsensitive) == 0
        || resource.host().compare(oldHost, Qt::CaseInsensitive) != 0)
        return resource;
    QUrl moved = resource;
    moved.setHost(newHost);
    return moved;
}

std::optional<Track> trackFromItem(const DidlObject &item, const QString &udn, const QString &host)
{
    if (!isAudioItem(item))
        return std::nullopt;
    // The first audio resource is normally the original file; another one,
    // often a server-side MP3 conversion, is the fallback.
    const Resource *primary = nullptr;
    const Resource *alternative = nullptr;
    for (const Resource &res : item.resources) {
        const bool audio = res.mimeType.isEmpty() || res.mimeType.startsWith(QLatin1String("audio/"));
        if (!audio)
            continue;
        if (!primary)
            primary = &res;
        else if (!alternative && res.url != primary->url
                 && (res.mimeType != primary->mimeType || res.mimeType == QLatin1String("audio/mpeg")))
            alternative = &res;
    }
    if (!primary)
        primary = &item.resources.first();

    Track track;
    track.sourceId = QString::fromLatin1(SourceId);
    track.location = trackLocation({udn, primary->url, alternative ? alternative->url : QUrl(), host});
    track.title = item.title;
    track.artist = item.artist;
    track.album = item.album;
    track.durationMs = primary->durationMs;
    for (const Resource &res : item.resources) {
        if (track.durationMs > 0)
            break;
        track.durationMs = res.durationMs;
    }
    return track;
}

QString folderId(const QString &udn, const QString &objectId)
{
    return udn + QLatin1Char('|') + objectId;
}

bool parseFolderId(const QString &id, QString *udn, QString *objectId)
{
    const int bar = id.indexOf(QLatin1Char('|'));
    if (bar <= 0 || bar == id.size() - 1)
        return false;
    *udn = id.left(bar);
    *objectId = id.mid(bar + 1);
    return true;
}

} // namespace newamp::dlna

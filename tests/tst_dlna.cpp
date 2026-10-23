#include "DlnaApi.h"

#include <QFile>
#include <QTest>

using namespace newamp;
using namespace newamp::dlna;

namespace {
QByteArray fixture(const char *name)
{
    QFile file(QStringLiteral(NEWAMP_DLNA_FIXTURES "/") + QLatin1String(name));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("missing fixture %s", name);
    return file.readAll();
}

const QString Udn = QStringLiteral("uuid:4d696e69-444c-164e-9d41-b827eb000001");
} // namespace

class TestDlna : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void buildsSearchRequest()
    {
        const QByteArray request = searchRequest(3);
        QVERIFY(request.startsWith("M-SEARCH * HTTP/1.1\r\n"));
        QVERIFY(request.contains("HOST: 239.255.255.250:1900\r\n"));
        QVERIFY(request.contains("MAN: \"ssdp:discover\"\r\n"));
        QVERIFY(request.contains("MX: 3\r\n"));
        QVERIFY(request.contains("ST: urn:schemas-upnp-org:device:MediaServer:1\r\n"));
        QVERIFY(request.endsWith("\r\n\r\n"));
    }

    void parsesSearchResponses()
    {
        const auto message = parseSsdp(fixture("ssdp_response.txt"));
        QVERIFY(message);
        QCOMPARE(message->location, QUrl(QStringLiteral("http://192.168.1.20:8200/rootDesc.xml")));
        QCOMPARE(message->udn, Udn);
        QCOMPARE(message->maxAgeS, 1810);
        QVERIFY(!message->byebye);
        QVERIFY(isMediaServer(*message));
    }

    void parsesAnnouncements()
    {
        const auto bye = parseSsdp(fixture("ssdp_byebye.txt"));
        QVERIFY(bye);
        QVERIFY(bye->byebye);
        QCOMPARE(bye->udn, Udn);
        QVERIFY(isMediaServer(*bye)); // by its ContentDirectory service

        const auto renderer = parseSsdp(fixture("ssdp_renderer.txt"));
        QVERIFY(renderer);
        QVERIFY(!isMediaServer(*renderer));

        QVERIFY(!parseSsdp("M-SEARCH * HTTP/1.1\r\nST: ssdp:all\r\n\r\n")); // someone else searching
        QVERIFY(!parseSsdp("HTTP/1.1 200 OK\r\nUSN: uuid:x\r\n\r\n"));       // no location
        QVERIFY(!parseSsdp(""));
    }

    void findsEmbeddedContentDirectory()
    {
        const QUrl location(QStringLiteral("http://192.168.1.21:9000/desc.xml"));
        const auto device = parseDeviceDescription(fixture("description.xml"), location);
        QVERIFY(device);
        // The embedded media server, not the root device without one.
        QCOMPARE(device->udn, QStringLiteral("uuid:nas-music"));
        QCOMPARE(device->name, QStringLiteral("Home NAS: Music"));
        QCOMPARE(device->controlUrl, QUrl(QStringLiteral("http://192.168.1.21:9000/ctl/ContentDir")));
        QCOMPARE(device->serviceType, QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1"));
    }

    void resolvesControlUrlAgainstLocation()
    {
        const QUrl location(QStringLiteral("http://192.168.1.20:8200/rootDesc.xml"));
        const auto device = parseDeviceDescription(fixture("description_plain.xml"), location);
        QVERIFY(device);
        QCOMPARE(device->udn, Udn);
        QCOMPARE(device->controlUrl, QUrl(QStringLiteral("http://192.168.1.20:8200/ctl/ContentDir")));

        QVERIFY(!parseDeviceDescription("<root><device><UDN>uuid:x</UDN></device></root>", location));
        QVERIFY(!parseDeviceDescription("not xml", location));
    }

    void buildsSoapRequests()
    {
        const QString type = QStringLiteral("urn:schemas-upnp-org:service:ContentDirectory:1");
        const QByteArray browse = browseEnvelope(type, QStringLiteral("1$7&<x>"), 200, 100);
        QVERIFY(browse.contains("<u:Browse xmlns:u=\"urn:schemas-upnp-org:service:ContentDirectory:1\">"));
        QVERIFY(browse.contains("<ObjectID>1$7&amp;&lt;x&gt;</ObjectID>"));
        QVERIFY(browse.contains("<BrowseFlag>BrowseDirectChildren</BrowseFlag>"));
        QVERIFY(browse.contains("<StartingIndex>200</StartingIndex><RequestedCount>100</RequestedCount>"));
        QVERIFY(browse.contains("<SortCriteria></SortCriteria></u:Browse>"));

        const QByteArray search = searchEnvelope(type, QStringLiteral("0"), QStringLiteral("dc:title contains \"a\""), 0, 50);
        QVERIFY(search.contains("<ContainerID>0</ContainerID>"));
        QVERIFY(search.contains("<SearchCriteria>dc:title contains &quot;a&quot;</SearchCriteria>"));
        QCOMPARE(soapAction(type, QStringLiteral("Search")),
                 QByteArray("\"urn:schemas-upnp-org:service:ContentDirectory:1#Search\""));
    }

    void parsesBrowseResponses()
    {
        const BrowseResult result = parseBrowseResponse(fixture("browse.xml"));
        QVERIFY(result.ok);
        QCOMPARE(result.returned, 7);
        QCOMPARE(result.total, 250);
        QVERIFY(result.didl.startsWith(QLatin1String("<DIDL-Lite")));

        QVERIFY(!parseBrowseResponse(fixture("fault.xml")).ok);
        QCOMPARE(parseSoapFault(fixture("fault.xml")), QStringLiteral("No such object (701)"));
        QVERIFY(parseSoapFault(fixture("browse.xml")).isEmpty());
    }

    void parsesSearchCapabilities()
    {
        const QStringList caps = parseSearchCapabilities(fixture("search_caps.xml"));
        QCOMPARE(caps.size(), 11);
        QVERIFY(caps.contains(QStringLiteral("dc:title")));
        QVERIFY(caps.contains(QStringLiteral("upnp:class")));
        QVERIFY(parseSearchCapabilities("<SearchCaps></SearchCaps>").isEmpty());
    }

    void parsesDidl()
    {
        const QList<DidlObject> objects = parseDidl(parseBrowseResponse(fixture("browse.xml")).didl);
        QCOMPARE(objects.size(), 7);

        const DidlObject &album = objects[0];
        QVERIFY(album.isContainer);
        QCOMPARE(album.id, QStringLiteral("1$7$1"));
        QCOMPARE(album.title, QStringLiteral("Blue Train"));
        QCOMPARE(album.upnpClass, QStringLiteral("object.container.album.musicAlbum"));
        QCOMPARE(album.childCount, 12);
        QCOMPARE(album.date, QStringLiteral("1957-01-01"));
        QVERIFY(!isAudioItem(album));

        const DidlObject &track = objects[3];
        QVERIFY(!track.isContainer);
        QCOMPARE(track.title, QStringLiteral("Moment's Notice"));
        // The performer, not the album artist or the composer.
        QCOMPARE(track.artist, QStringLiteral("John Coltrane"));
        QCOMPARE(track.album, QStringLiteral("Blue Train"));
        QCOMPARE(track.resources.size(), 3);
        QCOMPARE(track.resources[0].mimeType, QStringLiteral("audio/flac"));
        QCOMPARE(track.resources[0].durationMs, qint64(550123));
        QCOMPARE(track.resources[1].url, QUrl(QStringLiteral("http://192.168.1.20:8200/Transcode/22.mp3?a=1&b=2")));
        QVERIFY(isAudioItem(track));

        QCOMPARE(objects[4].artist, QStringLiteral("John Coltrane")); // from dc:creator
        QVERIFY(!isAudioItem(objects[5]));                          // a video
        QVERIFY(objects[6].resources.isEmpty());                    // RTSP isn't fetched over HTTP
        QVERIFY(!isAudioItem(objects[6]));
    }

    void parsesDurations()
    {
        QCOMPARE(parseDuration(QStringLiteral("0:03:45")), qint64(225000));
        QCOMPARE(parseDuration(QStringLiteral("1:00:00.5")), qint64(3600500));
        QCOMPARE(parseDuration(QStringLiteral("0:00:01.250")), qint64(1250));
        QCOMPARE(parseDuration(QStringLiteral("0:00:10.1/4")), qint64(10250));
        QCOMPARE(parseDuration(QStringLiteral("")), qint64(-1));
        QCOMPARE(parseDuration(QStringLiteral("3:45")), qint64(-1));
    }

    void buildsSearchCriteria()
    {
        const QStringList caps = parseSearchCapabilities(fixture("search_caps.xml"));
        const QString criteria = searchCriteria(QStringLiteral(" blue  \"train\" "), caps);
        QCOMPARE(criteria,
                 QStringLiteral("(upnp:class derivedfrom \"object.item.audioItem\" and (dc:title contains \"blue \\\"train\\\"\""
                                " or upnp:artist contains \"blue \\\"train\\\"\" or dc:creator contains \"blue \\\"train\\\"\""
                                " or upnp:album contains \"blue \\\"train\\\"\"))"
                                " or (upnp:class derivedfrom \"object.container.album.musicAlbum\" and dc:title contains \"blue \\\"train\\\"\")"
                                " or (upnp:class derivedfrom \"object.container.person.musicArtist\" and dc:title contains \"blue \\\"train\\\"\")"));

        // Without upnp:class the server can't filter, so neither do we.
        QCOMPARE(searchCriteria(QStringLiteral("x"), {QStringLiteral("dc:title")}),
                 QStringLiteral("dc:title contains \"x\""));
        QVERIFY(searchCriteria(QStringLiteral("x"), {}).isEmpty());
        QVERIFY(searchCriteria(QStringLiteral("x"), {QStringLiteral("upnp:genre")}).isEmpty());
        QVERIFY(searchCriteria(QStringLiteral("  "), caps).isEmpty());
        QVERIFY(searchCriteria(QStringLiteral("x"), {QStringLiteral("*")}).contains(QLatin1String("derivedfrom")));
    }

    void makesTracks()
    {
        const QList<DidlObject> objects = parseDidl(parseBrowseResponse(fixture("browse.xml")).didl);
        const auto track = trackFromItem(objects[3], Udn, QStringLiteral("192.168.1.20"));
        QVERIFY(track);
        QCOMPARE(track->sourceId, QStringLiteral("dlna"));
        QCOMPARE(track->title, QStringLiteral("Moment's Notice"));
        QCOMPARE(track->durationMs, qint64(550123));

        const TrackRef ref = parseTrackLocation(track->location);
        QVERIFY(ref.isValid());
        QCOMPARE(ref.udn, Udn);
        QCOMPARE(ref.resource, QUrl(QStringLiteral("http://192.168.1.20:8200/MediaItems/22.flac")));
        // The MP3 conversion is the fallback; the cover art is not a track.
        QCOMPARE(ref.alternative, QUrl(QStringLiteral("http://192.168.1.20:8200/Transcode/22.mp3?a=1&b=2")));
        QCOMPARE(ref.host, QStringLiteral("192.168.1.20"));

        const auto single = trackFromItem(objects[4], Udn, {});
        QVERIFY(single);
        QVERIFY(!parseTrackLocation(single->location).alternative.isValid());
        QVERIFY(!trackFromItem(objects[0], Udn, {}));
        QVERIFY(!trackFromItem(objects[5], Udn, {}));
    }

    void roundTripsAwkwardLocations()
    {
        const TrackRef ref{QStringLiteral("uuid:a&b=c"), QUrl(QStringLiteral("http://h:1/x?y=1&z=%262#frag")),
                           QUrl(QStringLiteral("http://h:1/a b")), QStringLiteral("h")};
        const QUrl location = trackLocation(ref);
        QCOMPARE(location.scheme(), QStringLiteral("dlna"));
        // Through text, as a saved playlist stores it.
        const TrackRef back = parseTrackLocation(QUrl(location.toString(QUrl::FullyEncoded)));
        QCOMPARE(back.udn, ref.udn);
        QCOMPARE(back.resource, ref.resource);
        QCOMPARE(back.alternative, ref.alternative);
        QCOMPARE(back.host, ref.host);

        QVERIFY(!parseTrackLocation(QUrl(QStringLiteral("plex://abc/x"))).isValid());
    }

    void followsServersToNewAddresses()
    {
        const QUrl res(QStringLiteral("http://192.168.1.20:8200/MediaItems/22.flac"));
        QCOMPARE(relocate(res, QStringLiteral("192.168.1.20"), QStringLiteral("192.168.1.44")),
                 QUrl(QStringLiteral("http://192.168.1.44:8200/MediaItems/22.flac")));
        QCOMPARE(relocate(res, QStringLiteral("192.168.1.20"), QStringLiteral("192.168.1.20")), res);
        QCOMPARE(relocate(res, QStringLiteral("192.168.1.20"), {}), res);        // server not found yet
        QCOMPARE(relocate(res, QStringLiteral("10.0.0.5"), QStringLiteral("10.0.0.6")), res); // served elsewhere
    }

    void splitsFolderIds()
    {
        QString udn, object;
        QVERIFY(parseFolderId(folderId(Udn, QStringLiteral("1$7|odd")), &udn, &object));
        QCOMPARE(udn, Udn);
        QCOMPARE(object, QStringLiteral("1$7|odd"));
        QVERIFY(!parseFolderId(QString(), &udn, &object));
        QVERIFY(!parseFolderId(QStringLiteral("uuid:x|"), &udn, &object));
    }
};

QTEST_GUILESS_MAIN(TestDlna)
#include "tst_dlna.moc"

#include "audio/ReplayGain.h"
#include "audio/TagReader.h"
#include "audio/TrackMixer.h"
#include "core/LocalFileSource.h"
#include "core/MediaSource.h"
#include "core/PlaybackEngine.h"
#include "core/PlaybackSettings.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"

#include <QAudioDevice>
#include <QBuffer>
#include <QFile>
#include <QMediaDevices>
#include <QSettings>
#include <QSignalSpy>
#include <QTcpServer>
#include <QTcpSocket>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>
#include <vector>

using namespace newamp;

namespace {

// Mono frames numbered from `first`, so the output shows which source and
// which frame each sample came from.
class Ramp final : public PcmSource
{
public:
    Ramp(float first, qint64 frames, int channels = 1)
        : m_first(first)
        , m_frames(frames)
        , m_channels(channels)
    {
    }

    int channels() const override { return m_channels; }
    int sampleRate() const override { return 1000; }
    int sourceSampleRate() const override { return 1000; }
    int sourceChannels() const override { return m_channels; }
    qint64 lengthFrames() const override { return m_frames; }
    qint64 positionFrames() const override { return m_position; }
    qint64 read(float *out, qint64 frames) override
    {
        const qint64 n = qMin(frames, m_frames - m_position);
        for (qint64 i = 0; i < n; ++i) {
            for (int ch = 0; ch < m_channels; ++ch)
                out[i * m_channels + ch] = m_first + float(m_position + i);
        }
        m_position += n;
        return n;
    }
    bool finished() const override { return m_position >= m_frames; }
    bool seek(qint64 frame) override
    {
        m_position = qBound<qint64>(0, frame, m_frames);
        return true;
    }

private:
    float m_first;
    qint64 m_frames;
    int m_channels;
    qint64 m_position = 0;
};

// The same, a constant level: for checking fades and gains.
class Level final : public PcmSource
{
public:
    Level(float value, qint64 frames)
        : m_value(value)
        , m_frames(frames)
    {
    }

    int channels() const override { return 1; }
    int sampleRate() const override { return 1000; }
    int sourceSampleRate() const override { return 1000; }
    int sourceChannels() const override { return 1; }
    qint64 lengthFrames() const override { return m_frames; }
    qint64 positionFrames() const override { return m_position; }
    qint64 read(float *out, qint64 frames) override
    {
        const qint64 n = qMin(frames, m_frames - m_position);
        std::fill(out, out + n, m_value);
        m_position += n;
        return n;
    }
    bool finished() const override { return m_position >= m_frames; }
    bool seek(qint64 frame) override
    {
        m_position = qBound<qint64>(0, frame, m_frames);
        return true;
    }

private:
    float m_value;
    qint64 m_frames;
    qint64 m_position = 0;
};

std::vector<float> renderAll(TrackMixer &mixer, qint64 chunk)
{
    std::vector<float> all;
    std::vector<float> buffer(size_t(chunk) * size_t(mixer.channels()));
    for (int guard = 0; guard < 10000; ++guard) {
        const qint64 got = mixer.render(buffer.data(), chunk);
        all.insert(all.end(), buffer.begin(), buffer.begin() + got * mixer.channels());
        if (got < chunk)
            break;
    }
    return all;
}

QByteArray wav(int sampleRate, int frames)
{
    QByteArray pcm;
    for (int i = 0; i < frames; ++i) {
        const qint16 v = qint16(std::sin(i * 0.05) * 8000);
        pcm.append(reinterpret_cast<const char *>(&v), 2).append(reinterpret_cast<const char *>(&v), 2);
    }
    QByteArray out;
    auto u32 = [&](quint32 v) { out.append(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { out.append(reinterpret_cast<const char *>(&v), 2); };
    out.append("RIFF");
    u32(quint32(36 + pcm.size()));
    out.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(2);
    u32(quint32(sampleRate));
    u32(quint32(sampleRate * 4));
    u16(4);
    u16(16);
    out.append("data");
    u32(quint32(pcm.size()));
    return out + pcm;
}

QByteArray id3Frame(const char *id, const QByteArray &body)
{
    QByteArray f(id);
    const quint32 size = quint32(body.size());
    f.append(char(size >> 24)).append(char(size >> 16)).append(char(size >> 8)).append(char(size));
    f.append(2, '\0');
    return f + body;
}

QByteArray id3Tag(const QByteArray &frames)
{
    QByteArray tag("ID3\x03\x00\x00", 6);
    const int size = int(frames.size());
    tag.append(char((size >> 21) & 0x7f)).append(char((size >> 14) & 0x7f)).append(char((size >> 7) & 0x7f)).append(char(size & 0x7f));
    return tag + frames;
}

// Serves fixed files over plain HTTP, like a Plex server's part URLs.
class TinyHttpServer : public QTcpServer
{
public:
    QHash<QString, QByteArray> files;

    TinyHttpServer()
    {
        listen(QHostAddress::LocalHost);
        connect(this, &QTcpServer::newConnection, this, [this] {
            while (QTcpSocket *socket = nextPendingConnection()) {
                connect(socket, &QTcpSocket::readyRead, socket, [this, socket] {
                    const QByteArray head = socket->peek(4096);
                    if (!head.contains("\r\n\r\n"))
                        return;
                    socket->readAll();
                    const QString path = QString::fromLatin1(head.split(' ').value(1));
                    const auto it = files.constFind(path);
                    const QByteArray body = it == files.constEnd() ? QByteArray("not found") : *it;
                    socket->write((it == files.constEnd() ? "HTTP/1.1 404 Not Found" : "HTTP/1.1 200 OK")
                                  + QByteArray("\r\nContent-Length: ") + QByteArray::number(body.size())
                                  + "\r\nConnection: close\r\n\r\n" + body);
                    socket->disconnectFromHost();
                });
                connect(socket, &QTcpSocket::disconnected, socket, &QObject::deleteLater);
            }
        });
    }

    QUrl url(const QString &path) const
    {
        return QUrl(QStringLiteral("http://127.0.0.1:%1%2").arg(serverPort()).arg(path));
    }
};

} // namespace

class TestTransitions : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void gaplessJoinsSampleForSample()
    {
        TrackMixer mixer;
        mixer.configure(2);
        mixer.setCurrent(std::make_unique<Ramp>(0, 250, 2), 1.0f);
        mixer.setNext(std::make_unique<Ramp>(1000, 100, 2), 1.0f);

        // Chunks that don't divide either track: the join lands mid-buffer.
        const std::vector<float> out = renderAll(mixer, 64);
        QCOMPARE(out.size(), size_t(350 * 2));
        for (int i = 0; i < 250; ++i)
            QCOMPARE(out[size_t(i * 2 + 1)], float(i));
        for (int i = 0; i < 100; ++i)
            QCOMPARE(out[size_t((250 + i) * 2)], 1000.0f + float(i));
        QCOMPARE(mixer.transitionFrame(), 250);
        QVERIFY(!mixer.hasNext());
        // The finished track waits to be freed off the audio thread.
        QCOMPARE(mixer.takeRetired().size(), size_t(1));
    }

    void endsWhenNothingIsQueued()
    {
        TrackMixer mixer;
        mixer.configure(1);
        mixer.setCurrent(std::make_unique<Ramp>(0, 100), 1.0f);
        QCOMPARE(renderAll(mixer, 64).size(), size_t(100));
        QCOMPARE(mixer.transitionFrame(), -1);
    }

    void crossfadeOverlapsTheTracks()
    {
        TrackMixer mixer;
        mixer.configure(1);
        mixer.setCrossfadeFrames(100);
        mixer.setCurrent(std::make_unique<Level>(0.5f, 400), 1.0f);
        mixer.setNext(std::make_unique<Level>(0.5f, 300), 1.0f);

        const std::vector<float> out = renderAll(mixer, 64);
        // The fade takes the last 100 frames of the first track.
        QCOMPARE(out.size(), size_t(400 + 300 - 100));
        QCOMPARE(mixer.transitionFrame(), 300);
        QCOMPARE(out[299], 0.5f);
        // Equal power: the start and end of the fade match the tracks.
        QVERIFY(std::abs(out[300] - 0.5f) < 0.01f);
        QVERIFY(std::abs(out[399] - 0.5f) < 0.02f);
        // ...and the middle bulges by sqrt(2), as uncorrelated audio would not.
        QVERIFY(std::abs(out[350] - 0.5f * std::sqrt(2.0f)) < 0.01f);
        QCOMPARE(out[450], 0.5f);
    }

    void fadeInAndOutCurves()
    {
        // With a silent incoming track only the fade-out shows.
        TrackMixer mixer;
        mixer.configure(1);
        mixer.setCrossfadeFrames(100);
        mixer.setCurrent(std::make_unique<Level>(1.0f, 200), 1.0f);
        mixer.setNext(std::make_unique<Level>(0.0f, 200), 1.0f);
        const std::vector<float> out = renderAll(mixer, 1000);
        QVERIFY(out[100] > 0.99f);
        QVERIFY(std::abs(out[150] - std::cos(0.5 * 1.5707963)) < 0.01);
        QVERIFY(out[199] < 0.05f);
        QCOMPARE(out[200], 0.0f);
    }

    void gainsApplyPerTrack()
    {
        TrackMixer mixer;
        mixer.configure(1);
        mixer.setCurrent(std::make_unique<Level>(0.5f, 10), 0.5f);
        mixer.setNext(std::make_unique<Level>(0.5f, 10), 2.0f);
        const std::vector<float> out = renderAll(mixer, 4);
        QCOMPARE(out.size(), size_t(20));
        QCOMPARE(out[0], 0.25f);
        QCOMPARE(out[19], 1.0f);
    }

    void seekEndsAFade()
    {
        TrackMixer mixer;
        mixer.configure(1);
        mixer.setCrossfadeFrames(100);
        mixer.setCurrent(std::make_unique<Level>(1.0f, 150), 1.0f);
        mixer.setNext(std::make_unique<Level>(0.0f, 500), 1.0f);
        std::vector<float> buffer(80);
        mixer.render(buffer.data(), 80); // into the fade
        QVERIFY(mixer.transitionFrame() == 50);
        mixer.stopFade();
        mixer.render(buffer.data(), 80);
        QCOMPARE(buffer[0], 0.0f); // only the new (silent) track remains
    }

    void replayGainModes()
    {
        ReplayGainInfo info;
        QVERIFY(info.assign(QStringLiteral("replaygain_track_gain"), QStringLiteral("-6.00 dB")));
        QVERIFY(info.assign(QStringLiteral("REPLAYGAIN_TRACK_PEAK"), QStringLiteral("0.5")));
        QVERIFY(info.assign(QStringLiteral("REPLAYGAIN_ALBUM_GAIN"), QStringLiteral("+2.5 dB")));
        QVERIFY(info.assign(QStringLiteral("REPLAYGAIN_ALBUM_PEAK"), QStringLiteral("0.9")));
        QVERIFY(!info.assign(QStringLiteral("TITLE"), QStringLiteral("x")));

        PlaybackOptions options;
        QCOMPARE(replayGainFactor(info, options), 1.0f); // off by default

        options.replayGain = PlaybackOptions::ReplayGain::Track;
        QVERIFY(std::abs(replayGainFactor(info, options) - std::pow(10.0f, -6.0f / 20)) < 1e-4);
        options.replayGainPreampDb = 6.0;
        QVERIFY(std::abs(replayGainFactor(info, options) - 1.0f) < 1e-4);

        // Album gain would clip a 0.9 peak (+2.5 dB is 1.33x), so it stops at the peak.
        options.replayGain = PlaybackOptions::ReplayGain::Album;
        options.replayGainPreampDb = 0.0;
        QVERIFY(std::abs(replayGainFactor(info, options) - 1.0f / 0.9f) < 1e-4);
        options.preventClipping = false;
        QVERIFY(std::abs(replayGainFactor(info, options) - std::pow(10.0f, 2.5f / 20)) < 1e-4);

        // Album mode falls back to the track's gain.
        ReplayGainInfo trackOnly;
        trackOnly.assign(QStringLiteral("REPLAYGAIN_TRACK_GAIN"), QStringLiteral("-3 dB"));
        QVERIFY(std::abs(replayGainFactor(trackOnly, options) - std::pow(10.0f, -3.0f / 20)) < 1e-4);
        // Untagged tracks play as they are.
        QCOMPARE(replayGainFactor(ReplayGainInfo(), options), 1.0f);

        // Opus R128 gains: Q7.8 against -23 LUFS, so 5 dB lower than ReplayGain.
        ReplayGainInfo opus;
        opus.assign(QStringLiteral("R128_TRACK_GAIN"), QStringLiteral("-2560")); // -10 dB
        QVERIFY(std::abs(*opus.trackGain - -5.0) < 1e-9);
        // A ReplayGain tag wins over R128, in either order.
        opus.assign(QStringLiteral("REPLAYGAIN_TRACK_GAIN"), QStringLiteral("-7 dB"));
        opus.assign(QStringLiteral("R128_TRACK_GAIN"), QStringLiteral("0"));
        QCOMPARE(*opus.trackGain, -7.0);
    }

    void readsReplayGainTags()
    {
        // ID3v2 TXXX: description, NUL, value. One in Latin-1, one in UTF-16
        // with a BOM on each string, as foobar2000 writes them.
        QByteArray utf16("\x01", 1);
        auto addUtf16 = [&](const QString &text) {
            utf16.append("\xff\xfe", 2);
            for (QChar c : text)
                utf16.append(char(c.unicode() & 0xff)).append(char(c.unicode() >> 8));
        };
        addUtf16(QStringLiteral("REPLAYGAIN_ALBUM_GAIN"));
        utf16.append(2, '\0');
        addUtf16(QStringLiteral("-4.20 dB"));
        const QByteArray tag = id3Tag(id3Frame("TIT2", QByteArray("\x00Song", 5))
                                      + id3Frame("TXXX", QByteArray("\x00replaygain_track_gain\x00-8.5 dB", 30))
                                      + id3Frame("TXXX", utf16));
        Tags tags = TagReader::parseId3v2(tag);
        QCOMPARE(tags.title, QStringLiteral("Song"));
        QVERIFY(tags.replayGain.trackGain);
        QCOMPARE(*tags.replayGain.trackGain, -8.5);
        QVERIFY(tags.replayGain.albumGain);
        QCOMPARE(*tags.replayGain.albumGain, -4.2);

        // Vorbis comments.
        auto le = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
        const QByteArray c1("TITLE=Song"), c2("REPLAYGAIN_TRACK_GAIN=-1.5 dB"), c3("replaygain_track_peak=0.75");
        const QByteArray block = le(1) + "v" + le(3) + le(quint32(c1.size())) + c1 + le(quint32(c2.size())) + c2
            + le(quint32(c3.size())) + c3;
        tags = TagReader::parseVorbisComments(block);
        QCOMPARE(tags.title, QStringLiteral("Song"));
        QCOMPARE(*tags.replayGain.trackGain, -1.5);
        QCOMPARE(*tags.replayGain.trackPeak, 0.75);

        // Ogg Opus: comments after "OpusTags".
        QBuffer opus;
        const QByteArray r128("R128_TRACK_GAIN=256");
        opus.setData(QByteArray("OggS") + QByteArray(24, '\0') + "OpusHead" + QByteArray(11, '\0') + "OggS"
                     + QByteArray(24, '\0') + "OpusTags" + le(1) + "v" + le(1) + le(quint32(r128.size())) + r128);
        opus.open(QIODevice::ReadOnly);
        tags = TagReader::read(opus);
        QCOMPARE(*tags.replayGain.trackGain, 6.0);
    }

    void upcomingMatchesNext()
    {
        Playlist playlist;
        QList<Track> tracks;
        for (int i = 0; i < 8; ++i) {
            Track t;
            t.location = QUrl::fromLocalFile(QStringLiteral("/t%1.mp3").arg(i));
            tracks << t;
        }
        playlist.append(tracks);
        QCOMPARE(playlist.upcoming(), 1);
        playlist.setCurrentIndex(7);
        QCOMPARE(playlist.upcoming(), -1);
        playlist.setRepeat(true);
        QCOMPARE(playlist.upcoming(), 0);

        // Shuffle picks ahead, and next() keeps to the pick.
        playlist.setShuffle(true);
        for (int i = 0; i < 20; ++i) {
            const int planned = playlist.upcoming();
            QCOMPARE(playlist.upcoming(), planned);
            QVERIFY(planned != playlist.currentIndex());
            QVERIFY(playlist.next());
            QCOMPARE(playlist.currentIndex(), planned);
        }
    }

    void settingsRoundTrip()
    {
        QTemporaryDir dir;
        QSettings ini(dir.filePath(QStringLiteral("s.ini")), QSettings::IniFormat);
        PlaybackSettings settings;
        QSignalSpy changed(&settings, &PlaybackSettings::changed);
        settings.setCrossfadeMs(3000);
        settings.setGapless(false);
        settings.setReplayGain(PlaybackOptions::ReplayGain::Album);
        settings.setReplayGainPreamp(40); // clamped
        settings.setOutputDevice("speakers");
        settings.setCrossfadeMs(3000); // unchanged: no signal
        QCOMPARE(changed.size(), 5);
        settings.save(ini);

        PlaybackSettings loaded;
        loaded.load(ini);
        QCOMPARE(loaded.options().crossfadeMs, 3000);
        QCOMPARE(loaded.options().gapless, false);
        QCOMPARE(loaded.options().replayGain, PlaybackOptions::ReplayGain::Album);
        QCOMPARE(loaded.options().replayGainPreampDb, PlaybackSettings::MaxPreampDb);
        QCOMPARE(loaded.options().outputDevice, QByteArray("speakers"));
    }

    void playerMovesOnWithoutStopping()
    {
        if (QMediaDevices::defaultAudioOutput().isNull())
            QSKIP("No audio output to play through");
        QTemporaryDir dir;
        QStringList paths;
        for (int i = 0; i < 2; ++i) {
            const QString path = dir.filePath(QStringLiteral("%1.wav").arg(i));
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(wav(44100, 44100 / 2)); // half a second each
            paths << path;
        }

        LocalFileSource source;
        PlaybackEngine engine;
        engine.registerSource(&source);
        PlaybackSettings settings;
        engine.setPlaybackSettings(&settings);
        Playlist playlist;
        PlayerController controller(playlist, engine);
        QSignalSpy advanced(&engine, &PlaybackEngine::trackAdvanced);
        QSignalSpy ended(&engine, &PlaybackEngine::endOfTrack);
        QSignalSpy states(&engine, &PlaybackEngine::stateChanged);

        controller.openTracks(LocalFileSource::tracksForPaths(paths));
        QTRY_COMPARE_WITH_TIMEOUT(advanced.size(), 1, 5000);
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(ended.size(), 0);
        // It started playing once and never stopped in between.
        QCOMPARE(states.size(), 1);
        QCOMPARE(engine.state(), PlaybackEngine::State::Playing);
        // The second track's clock starts from zero.
        QVERIFY(engine.position() < 300);

        // Then the list runs out, the ordinary way.
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
        QCOMPARE(advanced.size(), 1);
    }

    void crossfadesRemoteTracks()
    {
        if (QMediaDevices::defaultAudioOutput().isNull())
            QSKIP("No audio output to play through");
        TinyHttpServer server;
        QVERIFY(server.isListening());
        server.files.insert(QStringLiteral("/library/parts/1/one.wav"), wav(44100, 44100));
        server.files.insert(QStringLiteral("/library/parts/2/two.wav"), wav(44100, 44100 / 2));
        struct Source : MediaSource
        {
            TinyHttpServer *server;
            QString id() const override { return QStringLiteral("test"); }
            QString displayName() const override { return id(); }
            QUrl streamUrl(const Track &track) const override { return server->url(track.location.path()); }
        } source;
        source.server = &server;

        PlaybackEngine engine;
        engine.registerSource(&source);
        PlaybackSettings settings;
        settings.setCrossfadeMs(400);
        engine.setPlaybackSettings(&settings);
        Playlist playlist;
        PlayerController controller(playlist, engine);
        QSignalSpy advanced(&engine, &PlaybackEngine::trackAdvanced);
        QSignalSpy ended(&engine, &PlaybackEngine::endOfTrack);
        QList<Track> tracks;
        for (const char *path : {"test:/library/parts/1/one.wav", "test:/library/parts/2/two.wav"}) {
            Track track;
            track.sourceId = source.id();
            track.location = QUrl(QString::fromLatin1(path));
            tracks << track;
        }
        controller.openTracks(tracks);

        // The second track is fetched ahead and takes over 400 ms before the
        // first one's end.
        QTRY_COMPARE_WITH_TIMEOUT(advanced.size(), 1, 5000);
        QCOMPARE(playlist.currentIndex(), 1);
        QCOMPARE(ended.size(), 0);
        QTRY_COMPARE_WITH_TIMEOUT(ended.size(), 1, 5000);
    }
};

QTEST_GUILESS_MAIN(TestTransitions)
#include "tst_transitions.moc"

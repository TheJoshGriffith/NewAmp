#include "audio/AudioDecoder.h"
#include "audio/DspChain.h"
#include "audio/QtDecoderSource.h"
#include "audio/Spectrum.h"
#include "audio/TagReader.h"

#include <QBuffer>
#include <QElapsedTimer>
#include <QTemporaryDir>
#include <QTest>

#include <cmath>

using namespace newamp;

namespace {

constexpr double Pi = 3.14159265358979323846;

// A 16-bit PCM WAV file in memory.
QByteArray makeWav(int sampleRate, int channels, int frames, double freq)
{
    QByteArray pcm;
    for (int i = 0; i < frames; ++i) {
        const qint16 v = qint16(std::sin(2 * Pi * freq * i / sampleRate) * 16000);
        for (int ch = 0; ch < channels; ++ch)
            pcm.append(reinterpret_cast<const char *>(&v), 2);
    }
    QByteArray wav;
    auto u32 = [&](quint32 v) { wav.append(reinterpret_cast<const char *>(&v), 4); };
    auto u16 = [&](quint16 v) { wav.append(reinterpret_cast<const char *>(&v), 2); };
    wav.append("RIFF");
    u32(quint32(36 + pcm.size()));
    wav.append("WAVEfmt ");
    u32(16);
    u16(1);
    u16(quint16(channels));
    u32(quint32(sampleRate));
    u32(quint32(sampleRate * channels * 2));
    u16(quint16(channels * 2));
    u16(16);
    wav.append("data");
    u32(quint32(pcm.size()));
    wav.append(pcm);
    return wav;
}

std::unique_ptr<QIODevice> buffer(const QByteArray &data)
{
    auto device = std::make_unique<QBuffer>();
    device->setData(data);
    device->open(QIODevice::ReadOnly);
    return device;
}

// RMS of a processed sine at `freq` after it settles.
double gainAt(DspChain &chain, double freq, int sampleRate = 44100)
{
    chain.reset();
    std::vector<float> s(size_t(sampleRate / 2));
    for (size_t i = 0; i < s.size(); ++i)
        s[i] = float(0.25 * std::sin(2 * Pi * freq * double(i) / sampleRate));
    chain.process(s.data(), qint64(s.size()));
    double in = 0, out = 0;
    for (size_t i = s.size() / 2; i < s.size(); ++i) {
        const double x = 0.25 * std::sin(2 * Pi * freq * double(i) / sampleRate);
        in += x * x;
        out += double(s[i]) * s[i];
    }
    return 10 * std::log10(out / in);
}

} // namespace

class TestAudio : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void decodesAndSeeksWav()
    {
        AudioDecoder decoder;
        QVERIFY(decoder.open(buffer(makeWav(22050, 2, 22050, 440))));
        QCOMPARE(decoder.channels(), 2);
        QCOMPARE(decoder.sampleRate(), 22050);
        QCOMPARE(decoder.lengthFrames(), 22050);

        std::vector<float> out(2048 * 2);
        QCOMPARE(decoder.read(out.data(), 2048), 2048);
        QVERIFY(std::abs(out[2 * 10] - std::sin(2 * Pi * 440 * 10 / 22050) * 16000 / 32768) < 0.01);

        QVERIFY(decoder.seek(22000));
        QCOMPARE(decoder.positionFrames(), 22000);
        QCOMPARE(decoder.read(out.data(), 2048), 50); // only 50 frames left
    }

    void downmixesAndResamples()
    {
        AudioDecoder decoder;
        QVERIFY(decoder.open(buffer(makeWav(22050, 2, 22050, 440)), 1, 44100));
        QCOMPARE(decoder.channels(), 1);
        QCOMPARE(decoder.sampleRate(), 44100);
        QCOMPARE(decoder.sourceChannels(), 2);
        QCOMPARE(decoder.sourceSampleRate(), 22050);
    }

    void rejectsGarbage()
    {
        AudioDecoder decoder;
        QVERIFY(!decoder.open(buffer(QByteArray(4096, 'x'))));
        QVERIFY(!decoder.isOpen());
        QVERIFY(AudioDecoder::canDecode(QStringLiteral("/a/b.FLAC")));
        QVERIFY(!AudioDecoder::canDecode(QStringLiteral("/a/b.m4a")));
    }

    void flatEqualizerIsTransparent()
    {
        DspChain chain;
        chain.configure(44100, 1);
        DspParams p;
        p.eqEnabled = true;
        chain.setParams(p);
        QVERIFY(std::abs(gainAt(chain, 1000)) < 0.05);
    }

    void bandBoostHitsItsCentreFrequency()
    {
        DspChain chain;
        chain.configure(44100, 1);
        DspParams p;
        p.eqEnabled = true;
        p.bandsDb[4] = 12; // 1 kHz
        chain.setParams(p);
        QVERIFY(std::abs(gainAt(chain, 1000) - 12) < 1.0);
        QVERIFY(gainAt(chain, 60) < 1.0); // far bands untouched

        p.eqEnabled = false;
        chain.setParams(p);
        QVERIFY(std::abs(gainAt(chain, 1000)) < 0.05);
    }

    void preampVolumeAndBalance()
    {
        DspChain chain;
        chain.configure(44100, 2);
        DspParams p;
        p.eqEnabled = true;
        p.preampDb = -6;
        p.volume = 0.5;
        p.balance = 0.5; // right: left side attenuated by half
        chain.setParams(p);
        float frame[2] = {0.5f, 0.5f};
        chain.process(frame, 1);
        const double preamp = std::pow(10.0, -6 / 20.0);
        QVERIFY(std::abs(frame[0] - 0.5 * preamp * 0.5 * 0.5) < 1e-4);
        QVERIFY(std::abs(frame[1] - 0.5 * preamp * 0.5) < 1e-4);
    }

    void spectrumPeaksAtTheToneBar()
    {
        std::vector<float> s(Spectrum::FftSize);
        for (int i = 0; i < Spectrum::FftSize; ++i)
            s[size_t(i)] = float(0.8 * std::sin(2 * Pi * 1000 * i / 44100));
        const std::vector<float> bars = Spectrum::bars(s.data(), 19);
        const auto loudest = std::max_element(bars.begin(), bars.end()) - bars.begin();
        // 1 kHz on a 40 Hz..16 kHz log scale lands just past the middle.
        QVERIFY2(loudest >= 9 && loudest <= 11, qPrintable(QString::number(loudest)));
        QVERIFY(bars.front() < bars[size_t(loudest)]);

        std::vector<float> silence(Spectrum::FftSize, 0.0f);
        for (float v : Spectrum::bars(silence.data(), 19))
            QCOMPARE(v, 0.0f);
    }

    void readsId3v2()
    {
        // ID3v2.3: TIT2 in UTF-16 with BOM, TPE1 in Latin-1, TALB in UTF-8 (v2.4 style).
        auto frame = [](const char *id, const QByteArray &body) {
            QByteArray f(id);
            const quint32 size = quint32(body.size());
            f.append(char(size >> 24)).append(char(size >> 16)).append(char(size >> 8)).append(char(size));
            f.append(2, '\0');
            return f + body;
        };
        QByteArray title("\x01\xff\xfe", 3);
        for (QChar c : QStringLiteral("Café"))
            title.append(char(c.unicode() & 0xff)).append(char(c.unicode() >> 8));
        QByteArray frames = frame("TIT2", title) + frame("TPE1", QByteArray("\x00" "Artist", 7))
            + frame("TALB", QByteArray("\x03\xc3\x84lbum", 7));
        QByteArray tag("ID3\x03\x00\x00", 6);
        const int size = int(frames.size()) + 10; // padding
        tag.append(char((size >> 21) & 0x7f)).append(char((size >> 14) & 0x7f)).append(char((size >> 7) & 0x7f)).append(char(size & 0x7f));
        tag += frames + QByteArray(10, '\0');

        const Tags tags = TagReader::parseId3v2(tag);
        QCOMPARE(tags.title, QStringLiteral("Café"));
        QCOMPARE(tags.artist, QStringLiteral("Artist"));
        QCOMPARE(tags.album, QStringLiteral("Älbum"));
    }

    void readsVorbisCommentsAndId3v1()
    {
        auto le = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
        const QByteArray c1("title=Song"), c2("ARTIST=Band"), c3("Album=Record");
        const QByteArray block = le(6) + "vendor" + le(3) + le(quint32(c1.size())) + c1 + le(quint32(c2.size())) + c2
            + le(quint32(c3.size())) + c3;
        const Tags tags = TagReader::parseVorbisComments(block);
        QCOMPARE(tags.title, QStringLiteral("Song"));
        QCOMPARE(tags.artist, QStringLiteral("Band"));
        QCOMPARE(tags.album, QStringLiteral("Record"));
        // Truncated blocks don't crash.
        TagReader::parseVorbisComments(block.left(20));

        QByteArray v1("TAG");
        v1 += QByteArray("Old Title").leftJustified(30, '\0') + QByteArray("Old Artist").leftJustified(30, '\0')
            + QByteArray("Old Album").leftJustified(30, '\0');
        v1 = v1.leftJustified(128, '\0');
        QCOMPARE(TagReader::parseId3v1(v1).title, QStringLiteral("Old Title"));
        QCOMPARE(TagReader::parseId3v1(v1).album, QStringLiteral("Old Album"));
    }

    void decodesThroughQtMultimedia()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("tone.wav"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(makeWav(22050, 1, 22050, 440));
        file.close();

        QtDecoderSource source;
        if (!source.open(path, 2, 44100))
            QSKIP("No Qt Multimedia decoder available here");
        QVERIFY(source.channels() >= 1 && source.channels() <= 2);
        const int rate = source.sampleRate();
        QVERIFY(rate > 0);

        // Read until the end, letting the decoder deliver as we go.
        const int channels = source.channels();
        std::vector<float> chunk(size_t(1024 * channels));
        qint64 total = 0;
        float peak = 0;
        QElapsedTimer timer;
        timer.start();
        while (!source.finished() && timer.elapsed() < 10000) {
            const qint64 got = source.read(chunk.data(), 1024);
            for (qint64 i = 0; i < got * channels; ++i)
                peak = std::max(peak, std::fabs(chunk[size_t(i)]));
            total += got;
            if (got == 0)
                QTest::qWait(5);
        }
        QVERIFY(source.finished());
        QVERIFY(qAbs(total - rate) < rate / 50); // one second
        QCOMPARE(source.lengthFrames(), total);
        QVERIFY(peak > 0.4f && peak < 0.6f);

        // Seeking works within decoded audio; release() frees it and seek(0)
        // decodes again.
        QVERIFY(source.seek(rate / 2));
        QCOMPARE(source.positionFrames(), qint64(rate / 2));
        source.release();
        QCOMPARE(source.lengthFrames(), total);
        QVERIFY(source.seek(0));
        QTRY_VERIFY(source.read(chunk.data(), 1024) > 0);
    }

    void readsMp4Metadata()
    {
        auto atom = [](const QByteArray &type, const QByteArray &payload) {
            const quint32 size = quint32(8 + payload.size());
            QByteArray a;
            a.append(char(size >> 24)).append(char(size >> 16)).append(char(size >> 8)).append(char(size));
            return a + type + payload;
        };
        auto item = [&](const QByteArray &type, quint8 kind, const QByteArray &value) {
            return atom(type, atom("data", QByteArray(3, '\0') + char(kind) + QByteArray(4, '\0') + value));
        };
        const QByteArray utf16Album("\x00\xc4\x00l\x00" "b\x00u\x00m", 10);
        const QByteArray ilst = atom("ilst",
                                     item("\xa9too", 1, "encoder") + item("covr", 13, QByteArray(64, 'x'))
                                         + item("\xa9nam", 1, "Caf\xc3\xa9") + item("\xa9" "ART", 1, " Band ")
                                         + item("\xa9" "alb", 2, utf16Album));
        const QByteArray hdlr = atom("hdlr", QByteArray(25, '\0'));
        auto file = [&](const QByteArray &meta) {
            // mdat comes first, with a 64-bit size, as in many encoders' output.
            QByteArray mdat("\x00\x00\x00\x01mdat\x00\x00\x00\x00\x00\x00\x00\x14", 16);
            mdat += QByteArray(4, '\x55');
            return atom("ftyp", "M4A ") + mdat
                + atom("moov", atom("mvhd", QByteArray(100, '\0')) + atom("udta", meta));
        };

        // ISO meta: a full box with version and flags.
        QBuffer iso;
        iso.setData(file(atom("meta", QByteArray(4, '\0') + hdlr + ilst)));
        iso.open(QIODevice::ReadOnly);
        Tags tags = TagReader::read(iso);
        QCOMPARE(tags.title, QStringLiteral("Café"));
        QCOMPARE(tags.artist, QStringLiteral("Band"));
        QCOMPARE(tags.album, QStringLiteral("Älbum"));

        // QuickTime meta: children straight after the header.
        QBuffer quickTime;
        quickTime.setData(file(atom("meta", hdlr + ilst)));
        quickTime.open(QIODevice::ReadOnly);
        QCOMPARE(TagReader::read(quickTime).title, QStringLiteral("Café"));

        // Truncated files read what they can and don't crash.
        const QByteArray whole = file(atom("meta", QByteArray(4, '\0') + hdlr + ilst));
        for (int cut = 8; cut < whole.size(); cut += 7) {
            QBuffer part;
            part.setData(whole.left(cut));
            part.open(QIODevice::ReadOnly);
            TagReader::read(part);
        }
    }
};

QTEST_GUILESS_MAIN(TestAudio)
#include "tst_audio.moc"

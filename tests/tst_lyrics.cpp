#include "audio/TagReader.h"
#include "core/LocalLyrics.h"
#include "core/Lyrics.h"
#include "core/LyricsProvider.h"
#include "core/LyricsService.h"
#include "skin/DefaultSkin.h"
#include "ui/LyricsWindow.h"

#include <QFile>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>

using namespace newamp;

namespace {

// Answers with fixed text, at once or on the next event loop turn.
class FakeProvider : public LyricsProvider
{
public:
    FakeProvider(QString name, QString text, bool async = false)
        : m_name(std::move(name))
        , m_text(std::move(text))
        , m_async(async)
    {
    }
    QString name() const override { return m_name; }
    bool canHandle(const Track &track) const override { return !track.location.isEmpty() && !skip; }
    void fetch(const Track &track, const Callback &done) override
    {
        asked.append(track.location);
        if (m_async)
            QTimer::singleShot(0, [done, text = m_text] { done(text); });
        else
            done(m_text);
    }
    QList<QUrl> asked;
    bool skip = false;

private:
    QString m_name, m_text;
    bool m_async;
};

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

QByteArray utf16le(const QString &text)
{
    QByteArray out("\xff\xfe", 2);
    for (QChar c : text)
        out.append(char(c.unicode() & 0xff)).append(char(c.unicode() >> 8));
    return out;
}

QByteArray be32(quint32 v)
{
    QByteArray b;
    b.append(char(v >> 24)).append(char(v >> 16)).append(char(v >> 8)).append(char(v));
    return b;
}

Track localTrack(const QString &path)
{
    Track track;
    track.location = QUrl::fromLocalFile(path);
    return track;
}

} // namespace

class TestLyrics : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void initTestCase()
    {
        // Keep the window's settings out of the real configuration, in a
        // file: on Windows the registry needs an organization to keep them.
        QStandardPaths::setTestModeEnabled(true);
        QCoreApplication::setOrganizationName(QStringLiteral("NewAmpTests"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
    }

    void parsesSyncedLrc()
    {
        const Lyrics lyrics = Lyrics::parse(QChar(0xfeff) + QStringLiteral(
            "[ar:Someone]\r\n[ti:Song]\r\n[00:12.00]First line\r\n[00:17.5]Second\r\n"
            "[00:05.123][01:02:50]Chorus\r\n[00:20.00]\r\nnot timed, dropped\r\n"));
        QVERIFY(lyrics.synced);
        QCOMPARE(lyrics.lines.size(), 5);
        QCOMPARE(lyrics.lines.at(0).startMs, 5123);
        QCOMPARE(lyrics.lines.at(0).text, QStringLiteral("Chorus"));
        QCOMPARE(lyrics.lines.at(1).startMs, 12000);
        QCOMPARE(lyrics.lines.at(1).text, QStringLiteral("First line"));
        QCOMPARE(lyrics.lines.at(2).startMs, 17500);
        QCOMPARE(lyrics.lines.at(3).startMs, 20000);
        QCOMPARE(lyrics.lines.at(3).text, QString()); // an instrumental gap
        QCOMPARE(lyrics.lines.at(4).startMs, 62500);
        QCOMPARE(lyrics.lines.at(4).text, QStringLiteral("Chorus"));
    }

    void appliesOffset()
    {
        // A positive offset shows lines earlier; times never go negative.
        const Lyrics lyrics = Lyrics::parse(QStringLiteral("[offset:+500]\n[00:00.20]a\n[00:10.00]b"));
        QCOMPARE(lyrics.lines.at(0).startMs, 0);
        QCOMPARE(lyrics.lines.at(1).startMs, 9500);
    }

    void parsesWordTimings()
    {
        const Lyrics lyrics = Lyrics::parse(QStringLiteral("[00:01.00]<00:01.00>Hello <00:01.50>big <00:02.00>world"));
        QCOMPARE(lyrics.lines.size(), 1);
        const LyricLine &line = lyrics.lines.first();
        QCOMPARE(line.text, QStringLiteral("Hello big world"));
        QCOMPARE(line.words.size(), 3);
        QCOMPARE(line.words.at(1).startMs, 1500);
        QCOMPARE(line.words.at(1).text, QStringLiteral("big "));
    }

    void parsesPlainText()
    {
        const Lyrics lyrics = Lyrics::parse(QStringLiteral("\n\nVerse one\nline two\n\nVerse two\n\n"));
        QVERIFY(!lyrics.synced);
        QCOMPARE(lyrics.lines.size(), 4);
        QCOMPARE(lyrics.lines.at(2).text, QString());
        QCOMPARE(lyrics.plainText(), QStringLiteral("Verse one\nline two\n\nVerse two"));
        QCOMPARE(lyrics.lineAt(5000), -1);
        QVERIFY(Lyrics::parse(QStringLiteral("[ar:Only tags]\n  \n")).isEmpty());
    }

    void findsCurrentLine()
    {
        const Lyrics lyrics = Lyrics::parse(QStringLiteral("[00:01.00]a\n[00:02.00]b\n[00:03.00]c"));
        QCOMPARE(lyrics.lineAt(0), -1);
        QCOMPARE(lyrics.lineAt(999), -1);
        QCOMPARE(lyrics.lineAt(1000), 0);
        QCOMPARE(lyrics.lineAt(2500), 1);
        QCOMPARE(lyrics.lineAt(100000), 2);
        QCOMPARE(Lyrics::lrcTime(62500), QStringLiteral("[01:02.50]"));
    }

    void readsId3Lyrics()
    {
        // USLT in UTF-16 with a descriptor, line breaks kept.
        QByteArray uslt("\x01" "eng", 4);
        uslt += utf16le(QStringLiteral("desc")) + QByteArray(2, '\0') + utf16le(QStringLiteral("Line one\nLine twö"));
        Tags tags = TagReader::parseId3v2(id3Tag(id3Frame("USLT", uslt)));
        QCOMPARE(tags.lyrics, QStringLiteral("Line one\nLine twö"));

        // SYLT in milliseconds wins over USLT and becomes LRC; entries
        // without a leading line break continue the line.
        QByteArray sylt("\x00" "eng\x02\x01", 6);
        sylt += QByteArray("\0", 1);
        sylt += QByteArray("Hello\0", 6) + be32(1000);
        sylt += QByteArray(" there\0", 7) + be32(1500);
        sylt += QByteArray("\nNext\0", 6) + be32(62500);
        tags = TagReader::parseId3v2(id3Tag(id3Frame("USLT", uslt) + id3Frame("SYLT", sylt)));
        QCOMPARE(tags.lyrics, QStringLiteral("[00:01.00]Hello there\n[01:02.50]Next"));
        const Lyrics lyrics = Lyrics::parse(tags.lyrics);
        QVERIFY(lyrics.synced);
        QCOMPARE(lyrics.lines.size(), 2);

        // Truncated frames don't crash.
        TagReader::parseId3v2(id3Tag(id3Frame("SYLT", sylt.left(9))));
        TagReader::parseId3v2(id3Tag(id3Frame("USLT", uslt.left(3))));
    }

    void readsVorbisLyrics()
    {
        auto le = [](quint32 v) { return QByteArray(reinterpret_cast<const char *>(&v), 4); };
        const QByteArray c1("TITLE=Song"), c2("LYRICS=[00:01.00]Hi\n[00:02.00]There");
        const QByteArray block = le(6) + "vendor" + le(2) + le(quint32(c1.size())) + c1 + le(quint32(c2.size())) + c2;
        const Tags tags = TagReader::parseVorbisComments(block);
        QCOMPARE(tags.lyrics, QStringLiteral("[00:01.00]Hi\n[00:02.00]There"));
    }

    void findsSidecarFiles()
    {
        QTemporaryDir dir;
        const QString song = dir.filePath(QStringLiteral("My Song.v2.mp3"));
        QFile(song).open(QIODevice::WriteOnly);
        SidecarLyrics sidecar;
        QVERIFY(sidecar.canHandle(localTrack(song)));
        Track remote;
        remote.location = QUrl(QStringLiteral("plex://server/1"));
        QVERIFY(!sidecar.canHandle(remote));

        QString got = QStringLiteral("unset");
        sidecar.fetch(localTrack(song), [&](const QString &text) { got = text; });
        QCOMPARE(got, QString());

        QFile lrc(dir.filePath(QStringLiteral("My Song.v2.lrc")));
        QVERIFY(lrc.open(QIODevice::WriteOnly));
        lrc.write("\xef\xbb\xbf[00:01.00]Caf\xc3\xa9");
        lrc.close();
        sidecar.fetch(localTrack(song), [&](const QString &text) { got = text; });
        QCOMPARE(got, QStringLiteral("[00:01.00]Café"));

        // Not UTF-8: read as Latin-1. UTF-16 with BOM is understood.
        QFile latin(dir.filePath(QStringLiteral("latin.txt")));
        QVERIFY(latin.open(QIODevice::WriteOnly));
        latin.write("Caf\xe9");
        latin.close();
        QCOMPARE(SidecarLyrics::readTextFile(latin.fileName()), QStringLiteral("Café"));
        QFile wide(dir.filePath(QStringLiteral("wide.lrc")));
        QVERIFY(wide.open(QIODevice::WriteOnly));
        wide.write(utf16le(QStringLiteral("Wide")));
        wide.close();
        QCOMPARE(SidecarLyrics::readTextFile(wide.fileName()), QStringLiteral("Wide"));
    }

    void serviceTriesProvidersInOrder()
    {
        FakeProvider none(QStringLiteral("None"), QString());
        FakeProvider skipped(QStringLiteral("Skipped"), QStringLiteral("never"));
        skipped.skip = true;
        FakeProvider later(QStringLiteral("Later"), QStringLiteral("[00:01.00]From later"), true);
        FakeProvider last(QStringLiteral("Last"), QStringLiteral("unused"));
        LyricsService service;
        service.addProvider(&none);
        service.addProvider(&skipped);
        service.addProvider(&later);
        service.addProvider(&last);

        service.request(localTrack(QStringLiteral("/a.mp3")));
        QCOMPARE(service.state(), LyricsService::State::Searching);
        QTRY_COMPARE(service.state(), LyricsService::State::Found);
        QCOMPARE(service.lyrics().source, QStringLiteral("Later"));
        QCOMPARE(service.lyrics().lines.first().text, QStringLiteral("From later"));
        QVERIFY(skipped.asked.isEmpty());
        QVERIFY(last.asked.isEmpty());

        // An answer for an earlier track is dropped.
        service.request(localTrack(QStringLiteral("/b.mp3")));
        service.setLyrics(QStringLiteral("Picked by hand"), QStringLiteral("picked.lrc"));
        QTest::qWait(10);
        QCOMPARE(service.lyrics().source, QStringLiteral("picked.lrc"));
        QCOMPARE(service.lyrics().lines.first().text, QStringLiteral("Picked by hand"));

        LyricsService empty;
        empty.addProvider(&none);
        empty.request(localTrack(QStringLiteral("/c.mp3")));
        QCOMPARE(empty.state(), LyricsService::State::NotFound);
        empty.clear();
        QCOMPARE(empty.state(), LyricsService::State::Idle);
        QVERIFY(!empty.hasTrack());
    }

    void windowFollowsTheSong()
    {
        const Skin skin = DefaultSkin::create();
        FakeProvider provider(QStringLiteral("File"),
                              QStringLiteral("[00:01.00]One\n[00:02.00]Two is a much longer line that has to wrap "
                                             "across the window\n[00:03.00]\n[00:04.00]Four"));
        LyricsService service;
        service.addProvider(&provider);
        LyricsWindow window(&skin, service);
        window.setOffsetMs(0);
        QVERIFY(!window.grab().isNull()); // nothing playing

        service.request(localTrack(QStringLiteral("/song.mp3")));
        window.setOffsetMs(0);
        window.setPosition(0);
        QCOMPARE(window.currentLine(), -1);
        window.setPosition(2100);
        QCOMPARE(window.currentLine(), 1);
        window.setPosition(4000);
        QCOMPARE(window.currentLine(), 3);
        for (int i = 0; i < 40; ++i)
            window.tick();
        QVERIFY(!window.grab().isNull());

        // Showing lyrics earlier moves the current line on; it's remembered
        // for this track only.
        window.setPosition(1600);
        QCOMPARE(window.currentLine(), 0);
        window.setOffsetMs(500);
        QCOMPARE(window.currentLine(), 1);
        service.request(localTrack(QStringLiteral("/other.mp3")));
        QCOMPARE(window.offsetMs(), 0);
        service.request(localTrack(QStringLiteral("/song.mp3")));
        QCOMPARE(window.offsetMs(), 500);
        window.setOffsetMs(0);

        window.setTextSize(16);
        window.resize(window.size() * 2);
        QVERIFY(!window.grab().isNull());

        // Plain lyrics have no current line.
        service.setLyrics(QStringLiteral("Just words\nand more"), QStringLiteral("Tags"));
        QCOMPARE(window.currentLine(), -1);
        QVERIFY(!window.grab().isNull());
    }
};

QTEST_MAIN(TestLyrics)
#include "tst_lyrics.moc"

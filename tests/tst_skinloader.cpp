#include "skin/DefaultSkin.h"
#include "skin/SkinLoader.h"
#include "skin/ZipReader.h"

#include <QBuffer>
#include <QDir>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <zlib.h>

using namespace newamp;
using sprites::Sheet;

namespace {

// Builds a ZIP in memory. Entries are deflated unless store is true.
class ZipWriter
{
public:
    void add(const QString &name, const QByteArray &data, bool store = false)
    {
        QByteArray payload = data;
        quint16 method = 0;
        if (!store) {
            payload.resize(int(compressBound(uLong(data.size()))) + 64);
            z_stream s{};
            deflateInit2(&s, Z_BEST_COMPRESSION, Z_DEFLATED, -MAX_WBITS, 8, Z_DEFAULT_STRATEGY);
            s.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(data.constData()));
            s.avail_in = uInt(data.size());
            s.next_out = reinterpret_cast<Bytef *>(payload.data());
            s.avail_out = uInt(payload.size());
            deflate(&s, Z_FINISH);
            payload.resize(int(s.total_out));
            deflateEnd(&s);
            method = 8;
        }
        const quint32 crc = quint32(crc32(0, reinterpret_cast<const Bytef *>(data.constData()), uInt(data.size())));
        const QByteArray nameBytes = name.toUtf8();

        Entry e{nameBytes, method, crc, quint32(payload.size()), quint32(data.size()), quint32(m_body.size())};
        put32(m_body, 0x04034b50);
        put16(m_body, 20); put16(m_body, 0x800); put16(m_body, method); put16(m_body, 0); put16(m_body, 0);
        put32(m_body, crc); put32(m_body, e.compressed); put32(m_body, e.size);
        put16(m_body, quint16(nameBytes.size())); put16(m_body, 0);
        m_body += nameBytes;
        m_body += payload;
        m_entries << e;
    }

    QByteArray finish() const
    {
        QByteArray out = m_body;
        const quint32 dirStart = quint32(out.size());
        for (const Entry &e : m_entries) {
            put32(out, 0x02014b50);
            put16(out, 20); put16(out, 20); put16(out, 0x800); put16(out, e.method); put16(out, 0); put16(out, 0);
            put32(out, e.crc); put32(out, e.compressed); put32(out, e.size);
            put16(out, quint16(e.name.size())); put16(out, 0); put16(out, 0); put16(out, 0); put16(out, 0);
            put32(out, 0); put32(out, e.offset);
            out += e.name;
        }
        const quint32 dirSize = quint32(out.size()) - dirStart;
        put32(out, 0x06054b50);
        put16(out, 0); put16(out, 0);
        put16(out, quint16(m_entries.size())); put16(out, quint16(m_entries.size()));
        put32(out, dirSize); put32(out, dirStart); put16(out, 0);
        return out;
    }

private:
    struct Entry
    {
        QByteArray name;
        quint16 method;
        quint32 crc, compressed, size, offset;
    };
    static void put16(QByteArray &b, quint16 v) { b.append(char(v & 0xff)).append(char(v >> 8)); }
    static void put32(QByteArray &b, quint32 v) { put16(b, quint16(v & 0xffff)); put16(b, quint16(v >> 16)); }

    QByteArray m_body;
    QList<Entry> m_entries;
};

QByteArray bmp(const QSize &size, const QColor &color)
{
    QImage image(size, QImage::Format_RGB32);
    image.fill(color);
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "BMP");
    return data;
}

QRgb pixel(const Skin &skin, Sheet sheet, int x = 0, int y = 0)
{
    return skin.sheet(sheet).toImage().pixel(x, y);
}

} // namespace

class TestSkinLoader : public QObject
{
    Q_OBJECT

private Q_SLOTS:
    void zipStoredAndDeflated()
    {
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), "hello", true);
        writer.add(QStringLiteral("dir/b.txt"), QByteArray(5000, 'x'));
        ZipReader zip;
        QVERIFY(zip.openData(writer.finish()));
        QCOMPARE(zip.entryNames(), QStringList({QStringLiteral("a.txt"), QStringLiteral("dir/b.txt")}));
        QCOMPARE(zip.read(QStringLiteral("a.txt")), QByteArray("hello"));
        QCOMPARE(zip.read(QStringLiteral("dir/b.txt")), QByteArray(5000, 'x'));
        QVERIFY(zip.read(QStringLiteral("missing")).isNull());
    }

    void zipRejectsGarbage()
    {
        ZipReader zip;
        QVERIFY(!zip.openData("not a zip at all"));
        QVERIFY(!zip.errorString().isEmpty());

        // Truncate a valid archive inside the entry data.
        ZipWriter writer;
        writer.add(QStringLiteral("a.txt"), QByteArray(1000, 'y'));
        QByteArray data = writer.finish();
        data.remove(40, 200);
        ZipReader truncated;
        if (truncated.openData(data))
            QVERIFY(truncated.read(QStringLiteral("a.txt")).isNull());
    }

    void loadsWszWithFallbacks()
    {
        const Skin fallback = DefaultSkin::create();
        ZipWriter writer;
        // Upper-case names inside a folder, as many real skins have.
        writer.add(QStringLiteral("MySkin/MAIN.BMP"), bmp(sprites::MainWindowSize, Qt::red));
        writer.add(QStringLiteral("MySkin/Volume.bmp"), bmp(sprites::sheetSize(Sheet::Volume), Qt::blue));
        writer.add(QStringLiteral("MySkin/pledit.txt"),
                   "[Text]\r\nNormal=#112233\r\nCurrent=445566\r\nFont=Tahoma\r\n");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("myskin.wsz"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(writer.finish());
        file.close();

        QString error;
        const std::optional<Skin> skin = SkinLoader::load(path, fallback, &error);
        QVERIFY2(skin, qPrintable(error));
        QCOMPARE(skin->name(), QStringLiteral("myskin"));
        QCOMPARE(QColor(pixel(*skin, Sheet::Main)), QColor(Qt::red));
        QCOMPARE(QColor(pixel(*skin, Sheet::Volume)), QColor(Qt::blue));
        // No balance.bmp: the volume bitmap stands in for it.
        QCOMPARE(QColor(pixel(*skin, Sheet::Balance)), QColor(Qt::blue));
        // No cbuttons.bmp: falls back to the default skin.
        QCOMPARE(pixel(*skin, Sheet::CButtons, 5, 5), pixel(fallback, Sheet::CButtons, 5, 5));

        const PlaylistStyle &style = skin->playlistStyle();
        QCOMPARE(style.normal, QColor(0x11, 0x22, 0x33));
        QCOMPARE(style.current, QColor(0x44, 0x55, 0x66));
        QCOMPARE(style.fontFamily, QStringLiteral("Tahoma"));
        QCOMPARE(style.normalBackground, fallback.playlistStyle().normalBackground);
    }

    void loadsUnpackedFolder()
    {
        QTemporaryDir dir;
        QFile file(dir.filePath(QStringLiteral("Main.bmp")));
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(bmp(sprites::MainWindowSize, Qt::green));
        file.close();
        const std::optional<Skin> skin = SkinLoader::load(dir.path(), DefaultSkin::create());
        QVERIFY(skin);
        QCOMPARE(QColor(pixel(*skin, Sheet::Main)), QColor(Qt::green));
    }

    void rejectsArchiveWithoutMain()
    {
        ZipWriter writer;
        writer.add(QStringLiteral("readme.txt"), "not a skin");
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("bad.wsz"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(writer.finish());
        file.close();
        QString error;
        QVERIFY(!SkinLoader::load(path, DefaultSkin::create(), &error));
        QVERIFY(!error.isEmpty());
    }
};

QTEST_MAIN(TestSkinLoader)
#include "tst_skinloader.moc"

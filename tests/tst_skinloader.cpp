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

QByteArray bmp(const QImage &image)
{
    QByteArray data;
    QBuffer buffer(&data);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "BMP");
    return data;
}

void le16(QByteArray &b, quint16 v) { b.append(char(v & 0xff)).append(char(v >> 8)); }
void le32(QByteArray &b, quint32 v) { le16(b, quint16(v & 0xffff)); le16(b, quint16(v >> 16)); }

// A one-image .cur file with a BITMAPINFOHEADER image: `pixels` are the
// colour rows top down, `mask` the transparency rows top down (one byte per
// row is enough for the 8 px wide images used here).
QByteArray cursorFile(int bpp, int width, const QList<QByteArray> &pixels, const QList<quint8> &mask,
                      const QPoint &hotspot, const QList<QRgb> &palette = {})
{
    const int height = int(pixels.size());
    QByteArray image;
    le32(image, 40);
    le32(image, quint32(width));
    le32(image, quint32(height * 2));
    le16(image, 1);
    le16(image, quint16(bpp));
    for (int i = 0; i < 6; ++i)
        le32(image, i == 4 ? quint32(palette.size()) : 0);
    for (const QRgb c : palette)
        image.append(char(qBlue(c))).append(char(qGreen(c))).append(char(qRed(c))).append(char(0));
    const int stride = ((width * bpp + 31) / 32) * 4;
    for (int y = height - 1; y >= 0; --y) // bottom up
        image += pixels.at(y).leftJustified(stride, '\0');
    for (int y = height - 1; y >= 0; --y)
        image += QByteArray(1, char(mask.at(y))).leftJustified(4, '\0');

    QByteArray file;
    le16(file, 0);
    le16(file, 2);
    le16(file, 1);
    file.append(char(width)).append(char(height)).append(char(0)).append(char(0));
    le16(file, quint16(hotspot.x()));
    le16(file, quint16(hotspot.y()));
    le32(file, quint32(image.size()));
    le32(file, 22);
    return file + image;
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

    void parsesRegions()
    {
        const SkinRegions regions = SkinLoader::parseRegions(
            "; shapes\r\n[Normal]\r\nNumPoints=4, 3 ; two polygons\r\n"
            "PointList=0,0, 275,0, 275,14, 0,14,\r\n 0,14 10,116 0,116\r\n"
            "[Equalizer]\r\nNumPoints=5\r\nPointList=0,0, 1,1\r\n"); // too few points
        QCOMPARE(regions.size(), 1);
        const QList<QPolygon> normal = regions.value(QStringLiteral("normal"));
        QCOMPARE(normal.size(), 2);
        QCOMPARE(normal.at(0), QPolygon({QPoint(0, 0), QPoint(275, 0), QPoint(275, 14), QPoint(0, 14)}));
        QCOMPARE(normal.at(1), QPolygon({QPoint(0, 14), QPoint(10, 116), QPoint(0, 116)}));
        QVERIFY(SkinLoader::parseRegions(QByteArray()).isEmpty());
    }

    void decodesPalettedCursor()
    {
        // 8x2, 1 bpp: row 0 alternates white/black, row 1 is all white; the
        // mask hides the right half of row 1.
        const QByteArray file = cursorFile(1, 8, {QByteArray(1, char(0xaa)), QByteArray(1, char(0xff))},
                                           {0x00, 0x0f}, QPoint(3, 1), {qRgb(0, 0, 0), qRgb(255, 255, 255)});
        QPoint hotspot;
        const QImage image = SkinLoader::parseCursor(file, &hotspot);
        QCOMPARE(image.size(), QSize(8, 2));
        QCOMPARE(hotspot, QPoint(3, 1));
        QCOMPARE(image.pixel(0, 0), qRgb(255, 255, 255));
        QCOMPARE(image.pixel(1, 0), qRgb(0, 0, 0));
        QCOMPARE(qAlpha(image.pixel(3, 1)), 255);
        QCOMPARE(qAlpha(image.pixel(4, 1)), 0);
    }

    void decodesTrueColourCursor()
    {
        // 32 bpp with alpha: the alpha channel wins over the mask.
        QByteArray row;
        for (int x = 0; x < 2; ++x)
            row.append(char(0x30)).append(char(0x20)).append(char(0x10)).append(char(x == 0 ? 0xff : 0x00));
        const QImage image = SkinLoader::parseCursor(cursorFile(32, 2, {row}, {0xff}, QPoint(9, 9)));
        QCOMPARE(image.size(), QSize(2, 1));
        QCOMPARE(image.pixel(0, 0), qRgba(0x10, 0x20, 0x30, 0xff));
        QCOMPARE(qAlpha(image.pixel(1, 0)), 0);
    }

    void rejectsBrokenCursors()
    {
        QVERIFY(SkinLoader::parseCursor(QByteArray()).isNull());
        QVERIFY(SkinLoader::parseCursor("RIFF....ACON").isNull()); // animated .ani isn't supported
        QByteArray truncated = cursorFile(1, 8, {QByteArray(1, 0)}, {0}, QPoint());
        truncated.chop(6);
        QVERIFY(SkinLoader::parseCursor(truncated).isNull());
    }

    void loadsShapesCursorsAndLibraryColours()
    {
        QImage genex(194, 109, QImage::Format_RGB32);
        genex.fill(Qt::black);
        const auto swatch = [&genex](int x, QRgb c) { genex.setPixel(x, 0, c); genex.setPixel(x + 1, 0, c); };
        swatch(48, qRgb(1, 2, 3));   // item background
        swatch(50, qRgb(4, 5, 6));   // item foreground
        swatch(84, qRgb(7, 8, 9));   // highlighted text
        swatch(90, qRgb(10, 11, 12)); // selected background

        ZipWriter writer;
        writer.add(QStringLiteral("main.bmp"), bmp(sprites::MainWindowSize, Qt::red));
        writer.add(QStringLiteral("region.txt"), "[Normal]\nNumPoints=3\nPointList=0,0,275,0,0,116\n");
        writer.add(QStringLiteral("Normal.cur"),
                   cursorFile(1, 8, {QByteArray(1, 0)}, {0}, QPoint(2, 0), {qRgb(0, 0, 0), qRgb(255, 255, 255)}));
        writer.add(QStringLiteral("genex.bmp"), bmp(genex));
        writer.add(QStringLiteral("numbers.bmp"), bmp(sprites::sheetSize(Sheet::Numbers), Qt::blue));
        writer.add(QStringLiteral("nums_ex.bmp"), bmp(QSize(108, 13), Qt::green));
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("shaped.wsz"));
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(writer.finish());
        file.close();

        const Skin fallback = DefaultSkin::create();
        const std::optional<Skin> skin = SkinLoader::load(path, fallback);
        QVERIFY(skin);
        QCOMPARE(skin->regions().value(QStringLiteral("normal")).size(), 1);
        QVERIFY(skin->cursor(QStringLiteral("normal")));
        QCOMPARE(skin->cursor(QStringLiteral("normal"))->hotSpot(), QPoint(2, 0));
        QVERIFY(!skin->cursor(QStringLiteral("titlebar")));
        // nums_ex.bmp wins over numbers.bmp.
        QCOMPARE(QColor(pixel(*skin, Sheet::Numbers)), QColor(Qt::green));

        const PlaylistStyle &library = skin->libraryStyle();
        QCOMPARE(library.normalBackground, QColor(1, 2, 3));
        QCOMPARE(library.normal, QColor(4, 5, 6));
        QCOMPARE(library.current, QColor(7, 8, 9));
        QCOMPARE(library.selectedBackground, QColor(10, 11, 12));
        // The playlist keeps its own colours, and skins without genex.bmp
        // use those for the library too.
        QCOMPARE(skin->playlistStyle().normal, fallback.playlistStyle().normal);
        QCOMPARE(fallback.libraryStyle().normal, fallback.playlistStyle().normal);
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

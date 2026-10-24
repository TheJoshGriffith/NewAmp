#include "skin/SkinLoader.h"

#include "skin/ZipReader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>
#include <QRegularExpression>
#include <QtEndian>

#include <functional>

namespace newamp {

using sprites::Sheet;

namespace {

// File names per sheet, in order of preference.
QStringList fileNames(Sheet sheet)
{
    switch (sheet) {
    case Sheet::Main: return {QStringLiteral("main.bmp")};
    case Sheet::TitleBar: return {QStringLiteral("titlebar.bmp")};
    case Sheet::CButtons: return {QStringLiteral("cbuttons.bmp")};
    // nums_ex.bmp wins when both exist: it adds a real minus sign.
    case Sheet::Numbers: return {QStringLiteral("nums_ex.bmp"), QStringLiteral("numbers.bmp")};
    case Sheet::Text: return {QStringLiteral("text.bmp")};
    case Sheet::PosBar: return {QStringLiteral("posbar.bmp")};
    case Sheet::Volume: return {QStringLiteral("volume.bmp")};
    case Sheet::PlayPause: return {QStringLiteral("playpaus.bmp")};
    case Sheet::ShufRep: return {QStringLiteral("shufrep.bmp")};
    case Sheet::MonoSter: return {QStringLiteral("monoster.bmp")};
    // Skins without balance.bmp use the volume bitmap, like the original.
    case Sheet::Balance: return {QStringLiteral("balance.bmp"), QStringLiteral("volume.bmp")};
    case Sheet::EqMain: return {QStringLiteral("eqmain.bmp")};
    case Sheet::PlEdit: return {QStringLiteral("pledit.bmp")};
    case Sheet::Gen: return {QStringLiteral("gen.bmp")};
    case Sheet::Count: break;
    }
    return {};
}

// Reads a file by case-insensitive base name, ignoring any folder inside
// the skin (many archives wrap everything in one directory).
using FileReader = std::function<QByteArray(const QString &lowerName)>;

FileReader zipReader(const QString &path, QString *error)
{
    auto zip = std::make_shared<ZipReader>();
    if (!zip->open(path)) {
        if (error)
            *error = zip->errorString();
        return {};
    }
    auto index = std::make_shared<QHash<QString, QString>>();
    for (const QString &name : zip->entryNames()) {
        const QString base = name.section(QLatin1Char('/'), -1).section(QLatin1Char('\\'), -1).toLower();
        if (!index->contains(base))
            index->insert(base, name);
    }
    return [zip, index](const QString &lowerName) {
        const QString name = index->value(lowerName);
        return name.isEmpty() ? QByteArray() : zip->read(name);
    };
}

FileReader folderReader(const QString &path)
{
    QHash<QString, QString> index;
    QDirIterator it(path, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString file = it.next();
        const QString base = QFileInfo(file).fileName().toLower();
        if (!index.contains(base))
            index.insert(base, file);
    }
    return [index](const QString &lowerName) {
        QFile file(index.value(lowerName));
        if (file.fileName().isEmpty() || file.size() > ZipReader::MaxEntrySize || !file.open(QIODevice::ReadOnly))
            return QByteArray();
        return file.readAll();
    };
}

QColor parseColor(QString value)
{
    value = value.trimmed();
    if (!value.startsWith(QLatin1Char('#')))
        value.prepend(QLatin1Char('#'));
    const QColor color = QColor::fromString(value);
    return color;
}

} // namespace

PlaylistStyle SkinLoader::parsePlaylistStyle(const QByteArray &text, const PlaylistStyle &fallback)
{
    PlaylistStyle style = fallback;
    for (const QByteArray &rawLine : text.split('\n')) {
        const QString line = QString::fromLatin1(rawLine).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || line.startsWith(QLatin1Char(';')) || line.startsWith(QLatin1Char('[')))
            continue;
        const QString key = line.left(eq).trimmed().toLower();
        const QString value = line.mid(eq + 1).trimmed();
        QColor *target = nullptr;
        if (key == QLatin1String("normal"))
            target = &style.normal;
        else if (key == QLatin1String("current"))
            target = &style.current;
        else if (key == QLatin1String("normalbg"))
            target = &style.normalBackground;
        else if (key == QLatin1String("selectedbg"))
            target = &style.selectedBackground;
        else if (key == QLatin1String("font"))
            style.fontFamily = value;
        if (target) {
            const QColor color = parseColor(value);
            if (color.isValid())
                *target = color;
        }
    }
    return style;
}

VisColors SkinLoader::parseVisColors(const QByteArray &text, const VisColors &fallback)
{
    VisColors colors = fallback;
    int index = 0;
    for (const QByteArray &rawLine : text.split('\n')) {
        if (index >= int(colors.size()))
            break;
        // "24,33,41, // comment" -> first three numbers.
        const QByteArray line = rawLine.left(rawLine.indexOf("//") >= 0 ? rawLine.indexOf("//") : rawLine.size());
        const QList<QByteArray> parts = line.split(',');
        if (parts.size() < 3)
            continue;
        bool ok[3];
        const int r = parts.at(0).trimmed().toInt(&ok[0]);
        const int g = parts.at(1).trimmed().toInt(&ok[1]);
        const int b = parts.at(2).trimmed().toInt(&ok[2]);
        if (ok[0] && ok[1] && ok[2])
            colors[size_t(index++)] = QColor(qBound(0, r, 255), qBound(0, g, 255), qBound(0, b, 255));
    }
    return colors;
}

SkinRegions SkinLoader::parseRegions(const QByteArray &text)
{
    SkinRegions regions;
    QString section;
    QList<int> counts;
    QList<int> coords;
    bool inPointList = false;
    auto finish = [&] {
        // Polygons whose points ran short are dropped, not guessed at.
        QList<QPolygon> polygons;
        int next = 0;
        for (const int count : std::as_const(counts)) {
            if (count < 3 || next + count * 2 > coords.size())
                break;
            QPolygon polygon;
            for (int i = 0; i < count; ++i, next += 2)
                polygon << QPoint(coords.at(next), coords.at(next + 1));
            polygons << polygon;
        }
        if (!section.isEmpty() && !polygons.isEmpty())
            regions.insert(section, polygons);
        counts.clear();
        coords.clear();
    };
    static const QRegularExpression number(QStringLiteral("-?\\d+"));
    auto numbers = [](const QString &value) {
        QList<int> out;
        for (auto it = number.globalMatch(value); it.hasNext();)
            out << it.next().captured().toInt();
        return out;
    };
    for (const QByteArray &rawLine : text.split('\n')) {
        QString line = QString::fromLatin1(rawLine);
        const int comment = line.indexOf(QLatin1Char(';'));
        if (comment >= 0)
            line.truncate(comment);
        line = line.trimmed();
        if (line.startsWith(QLatin1Char('[')) && line.endsWith(QLatin1Char(']'))) {
            finish();
            inPointList = false;
            section = line.mid(1, line.size() - 2).trimmed().toLower();
            continue;
        }
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0) {
            // Long point lists are sometimes wrapped onto following lines.
            if (inPointList)
                coords += numbers(line);
            continue;
        }
        const QString key = line.left(eq).trimmed().toLower();
        inPointList = key == QLatin1String("pointlist");
        if (key == QLatin1String("numpoints"))
            counts = numbers(line.mid(eq + 1));
        else if (inPointList)
            coords += numbers(line.mid(eq + 1));
    }
    finish();
    return regions;
}

PlaylistStyle SkinLoader::parseGenExColors(const QImage &genex, const PlaylistStyle &fallback)
{
    // Each colour is a 2 px swatch on the top row, starting at x = 48.
    PlaylistStyle style = fallback;
    if (genex.width() < 92 || genex.height() < 1)
        return style;
    auto at = [&genex](int x) { return QColor(genex.pixel(x, 0)); };
    style.normalBackground = at(48);   // item background
    style.normal = at(50);             // item foreground
    style.current = at(84);            // highlighted list text
    style.selectedBackground = at(90); // selected list background
    return style;
}

QImage SkinLoader::parseCursor(const QByteArray &data, QPoint *hotspot)
{
    auto u16 = [&data](qsizetype at) { return qFromLittleEndian<quint16>(data.constData() + at); };
    auto u32 = [&data](qsizetype at) { return qFromLittleEndian<quint32>(data.constData() + at); };
    // ICONDIR, then the first ICONDIRENTRY: cursors hold one image.
    if (data.size() < 22 || u16(0) != 0 || (u16(2) != 1 && u16(2) != 2) || u16(4) == 0)
        return {};
    const bool isCursor = u16(2) == 2;
    const QPoint spot(isCursor ? u16(10) : 0, isCursor ? u16(12) : 0);
    const qsizetype size = u32(14);
    const qsizetype offset = u32(18);
    if (offset < 22 || offset >= data.size() || size > data.size() - offset)
        return {};
    const QByteArray image = data.mid(offset, size);

    QImage out;
    if (image.startsWith("\x89PNG")) {
        out.loadFromData(image, "PNG");
    } else if (image.size() >= 40) {
        // A BITMAPINFOHEADER image whose height counts the colour bitmap
        // and the 1-bit transparency mask under it.
        auto i32 = [&image](qsizetype at) { return qFromLittleEndian<qint32>(image.constData() + at); };
        auto b16 = [&image](qsizetype at) { return qFromLittleEndian<quint16>(image.constData() + at); };
        const qsizetype header = i32(0);
        const int width = i32(4);
        const int height = qAbs(i32(8)) / 2;
        const int bpp = b16(14);
        const quint32 compression = quint32(i32(16));
        quint32 colours = quint32(i32(32));
        if (header < 40 || width <= 0 || width > 256 || height <= 0 || height > 256 || compression != 0)
            return {};
        if (bpp != 1 && bpp != 4 && bpp != 8 && bpp != 24 && bpp != 32)
            return {};
        if (bpp <= 8 && (colours == 0 || colours > (1u << bpp)))
            colours = 1u << bpp;
        if (bpp > 8)
            colours = 0;
        const qsizetype palette = header;
        const qsizetype pixels = palette + qsizetype(colours) * 4;
        const qsizetype stride = ((width * bpp + 31) / 32) * 4;
        const qsizetype maskStride = ((width + 31) / 32) * 4;
        const qsizetype mask = pixels + stride * height;
        if (mask + maskStride * height > image.size())
            return {};
        auto byte = [&image](qsizetype at) { return quint8(image.at(at)); };

        out = QImage(width, height, QImage::Format_ARGB32);
        bool anyAlpha = false;
        for (int y = 0; y < height; ++y) {
            const qsizetype row = pixels + stride * (height - 1 - y); // stored bottom up
            for (int x = 0; x < width; ++x) {
                QRgb rgb = 0;
                if (bpp == 32) {
                    const qsizetype p = row + x * 4;
                    rgb = qRgba(byte(p + 2), byte(p + 1), byte(p), byte(p + 3));
                    anyAlpha |= byte(p + 3) != 0;
                } else if (bpp == 24) {
                    const qsizetype p = row + x * 3;
                    rgb = qRgb(byte(p + 2), byte(p + 1), byte(p));
                } else {
                    const int perByte = 8 / bpp;
                    const int shift = 8 - bpp * (x % perByte + 1);
                    const int index = (byte(row + x / perByte) >> shift) & ((1 << bpp) - 1);
                    const qsizetype entry = palette + index * 4;
                    rgb = qRgb(byte(entry + 2), byte(entry + 1), byte(entry));
                }
                out.setPixel(x, y, rgb);
            }
        }
        // Without an alpha channel the mask decides: a set bit is see-through.
        // (Windows inverts the screen where a masked pixel isn't black; that
        // can't be drawn, so those pixels are see-through too.)
        if (!anyAlpha) {
            for (int y = 0; y < height; ++y) {
                const qsizetype row = mask + maskStride * (height - 1 - y);
                for (int x = 0; x < width; ++x) {
                    const bool clear = byte(row + x / 8) & (0x80 >> (x % 8));
                    const QRgb rgb = out.pixel(x, y);
                    out.setPixel(x, y, clear ? qRgba(0, 0, 0, 0) : (rgb | 0xff000000u));
                }
            }
        }
    }
    if (out.isNull())
        return {};
    if (hotspot)
        *hotspot = QPoint(qBound(0, spot.x(), out.width() - 1), qBound(0, spot.y(), out.height() - 1));
    return out;
}

const QStringList &SkinLoader::cursorNames()
{
    static const QStringList names = {
        QStringLiteral("close"),    QStringLiteral("eqclose"),  QStringLiteral("eqnormal"),
        QStringLiteral("eqslid"),   QStringLiteral("eqtitle"),  QStringLiteral("mainmenu"),
        QStringLiteral("mmenu"),    QStringLiteral("min"),      QStringLiteral("normal"),
        QStringLiteral("pclose"),   QStringLiteral("pnormal"),  QStringLiteral("posbar"),
        QStringLiteral("psize"),    QStringLiteral("ptbar"),    QStringLiteral("pvscroll"),
        QStringLiteral("pwinbut"),  QStringLiteral("pwsnorm"),  QStringLiteral("pwssize"),
        QStringLiteral("songname"), QStringLiteral("titlebar"), QStringLiteral("volbal"),
        QStringLiteral("winbut"),   QStringLiteral("wsclose"),  QStringLiteral("wsmin"),
        QStringLiteral("wsnormal"), QStringLiteral("wsposbar"),
    };
    return names;
}

std::optional<Skin> SkinLoader::load(const QString &path, const Skin &fallback, QString *error)
{
    const QFileInfo info(path);
    const FileReader read = info.isDir() ? folderReader(path) : zipReader(path, error);
    if (!read)
        return std::nullopt;

    Skin skin = fallback;
    skin.setName(info.completeBaseName());
    bool hasMain = false;
    for (int i = 0; i < int(Sheet::Count); ++i) {
        const Sheet sheet = Sheet(i);
        for (const QString &name : fileNames(sheet)) {
            const QByteArray data = read(name);
            if (data.isEmpty())
                continue;
            QImage image;
            if (!image.loadFromData(data))
                continue;
            skin.setSheet(sheet, QPixmap::fromImage(image));
            hasMain |= sheet == Sheet::Main;
            break;
        }
    }
    if (!hasMain) {
        if (error && error->isEmpty())
            *error = QStringLiteral("No main.bmp found; this does not look like a classic skin");
        return std::nullopt;
    }

    const QByteArray pledit = read(QStringLiteral("pledit.txt"));
    if (!pledit.isEmpty())
        skin.setPlaylistStyle(parsePlaylistStyle(pledit, fallback.playlistStyle()));
    const QByteArray viscolor = read(QStringLiteral("viscolor.txt"));
    if (!viscolor.isEmpty())
        skin.setVisColors(parseVisColors(viscolor, fallback.visColors()));
    skin.setRegions(parseRegions(read(QStringLiteral("region.txt"))));

    QImage genex;
    if (genex.loadFromData(read(QStringLiteral("genex.bmp"))))
        skin.setLibraryStyle(parseGenExColors(genex, skin.playlistStyle()));
    // The library keeps the playlist font; genex.bmp has no font of its own.

    QHash<QString, QCursor> cursors;
    for (const QString &name : cursorNames()) {
        QPoint hotspot;
        const QImage image = parseCursor(read(name + QStringLiteral(".cur")), &hotspot);
        if (!image.isNull())
            cursors.insert(name, QCursor(QPixmap::fromImage(image), hotspot.x(), hotspot.y()));
    }
    skin.setCursors(cursors);
    return skin;
}

} // namespace newamp

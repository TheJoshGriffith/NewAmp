#include "skin/SkinLoader.h"

#include "skin/ZipReader.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QImage>

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
    case Sheet::Numbers: return {QStringLiteral("numbers.bmp"), QStringLiteral("nums_ex.bmp")};
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
    return skin;
}

} // namespace newamp

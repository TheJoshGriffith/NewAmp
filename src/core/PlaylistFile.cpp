#include "core/PlaylistFile.h"

#include "core/LocalFileSource.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringDecoder>

namespace newamp {

namespace {

const QString &localId()
{
    static const QString id = QString::fromLatin1(LocalFileSource::Id);
    return id;
}

bool isWebScheme(const QString &scheme)
{
    static const QStringList schemes = {
        QStringLiteral("http"), QStringLiteral("https"), QStringLiteral("rtsp"),
        QStringLiteral("rtmp"), QStringLiteral("mms"),   QStringLiteral("mmsh"),
    };
    return schemes.contains(scheme.toLower());
}

Track webTrack(const QUrl &url)
{
    Track track;
    track.sourceId = localId();
    track.location = url;
    // Until the stream says what it is, show where it is.
    track.title = url.toString(QUrl::RemoveUserInfo);
    return track;
}

// Playlists in the wild are UTF-8 (M3U8, and most M3U today) or the
// Windows code page of old players, which Latin-1 is close enough to.
QString decode(QByteArray data)
{
    if (data.startsWith("\xEF\xBB\xBF"))
        data.remove(0, 3);
    QStringDecoder utf8(QStringDecoder::Utf8);
    const QString text = utf8(data);
    return utf8.hasError() ? QString::fromLatin1(data) : text;
}

QStringList lines(const QByteArray &data)
{
    static const QRegularExpression Breaks(QStringLiteral("[\r\n]+"));
    return decode(data).split(Breaks, Qt::SkipEmptyParts);
}

// "Artist - Title" as M3U and PLS write it; everything is the title when
// there's no separator.
void applyDisplayTitle(Track &track, const QString &text)
{
    const QString display = text.trimmed();
    if (display.isEmpty())
        return;
    const int dash = display.indexOf(QLatin1String(" - "));
    if (dash > 0) {
        track.artist = display.left(dash).trimmed();
        track.title = display.mid(dash + 3).trimmed();
    } else {
        track.artist.clear();
        track.title = display;
    }
}

void applySeconds(Track &track, const QString &seconds)
{
    bool ok = false;
    const qint64 s = seconds.trimmed().toLongLong(&ok);
    if (ok && s > 0)
        track.durationMs = s * 1000;
}

PlaylistFile::Format sniff(const QByteArray &data)
{
    const QByteArray start = data.left(512).trimmed();
    if (start.startsWith('{'))
        return PlaylistFile::Format::Native;
    if (start.toLower().contains("[playlist]"))
        return PlaylistFile::Format::PLS;
    return PlaylistFile::Format::M3U;
}

QList<Track> parseM3U(const QByteArray &data, const QUrl &base)
{
    QList<Track> tracks;
    QString seconds;
    QString title;
    bool hasInfo = false;
    for (const QString &raw : lines(data)) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (line.startsWith(QLatin1String("#EXTINF:"), Qt::CaseInsensitive)) {
            // #EXTINF:<seconds>[ attributes],<display title>
            const QString info = line.mid(8);
            const int comma = info.indexOf(QLatin1Char(','));
            seconds = info.left(comma).section(QLatin1Char(' '), 0, 0);
            title = comma >= 0 ? info.mid(comma + 1) : QString();
            hasInfo = true;
            continue;
        }
        if (line.startsWith(QLatin1Char('#')))
            continue;
        Track track = PlaylistFile::trackForEntry(line, base);
        if (hasInfo) {
            applySeconds(track, seconds);
            applyDisplayTitle(track, title);
            hasInfo = false;
        }
        tracks << track;
    }
    return tracks;
}

QList<Track> parsePLS(const QByteArray &data, const QUrl &base)
{
    struct Entry
    {
        QString file, title, length;
    };
    QMap<int, Entry> entries;
    static const QRegularExpression Key(QStringLiteral("^(file|title|length)(\\d+)$"),
                                        QRegularExpression::CaseInsensitiveOption);
    for (const QString &line : lines(data)) {
        const int equals = line.indexOf(QLatin1Char('='));
        if (equals <= 0)
            continue;
        const auto match = Key.match(line.left(equals).trimmed());
        if (!match.hasMatch())
            continue;
        Entry &entry = entries[match.captured(2).toInt()];
        const QString key = match.captured(1).toLower();
        const QString value = line.mid(equals + 1).trimmed();
        if (key == QLatin1String("file"))
            entry.file = value;
        else if (key == QLatin1String("title"))
            entry.title = value;
        else
            entry.length = value;
    }
    QList<Track> tracks;
    for (const Entry &entry : std::as_const(entries)) {
        if (entry.file.isEmpty())
            continue;
        Track track = PlaylistFile::trackForEntry(entry.file, base);
        applySeconds(track, entry.length);
        applyDisplayTitle(track, entry.title);
        tracks << track;
    }
    return tracks;
}

QList<Track> parseNative(const QByteArray &data, const QUrl &base)
{
    QList<Track> tracks;
    const QJsonArray items = QJsonDocument::fromJson(data).object().value(QStringLiteral("tracks")).toArray();
    for (const QJsonValue &value : items) {
        const QJsonObject o = value.toObject();
        const QString location = o.value(QStringLiteral("location")).toString();
        if (location.isEmpty())
            continue;
        const QString source = o.value(QStringLiteral("source")).toString();
        Track track;
        if (source.isEmpty() || source == localId()) {
            track = PlaylistFile::trackForEntry(location, base);
        } else {
            // A plugin's own location, kept exactly as the plugin made it.
            track.sourceId = source;
            track.location = QUrl(location);
        }
        const auto text = [&o](const char *key) { return o.value(QLatin1String(key)).toString(); };
        if (!text("title").isEmpty())
            track.title = text("title");
        track.artist = text("artist");
        track.album = text("album");
        const qint64 duration = qint64(o.value(QStringLiteral("durationMs")).toDouble(-1));
        track.durationMs = duration > 0 ? duration : -1;
        tracks << track;
    }
    return tracks;
}

// A local file relative to the playlist when it's beside or below it, so
// a folder of music and its playlists can move together.
QString localEntry(const Track &track, const QString &baseDir)
{
    const QString path = track.location.toLocalFile();
    if (!baseDir.isEmpty()) {
        const QString relative = QDir(baseDir).relativeFilePath(path);
        if (!relative.startsWith(QLatin1String("..")) && QDir::isRelativePath(relative))
            return QDir::toNativeSeparators(relative);
    }
    return QDir::toNativeSeparators(path);
}

QString entryFor(const Track &track, const QString &baseDir)
{
    return track.location.isLocalFile() ? localEntry(track, baseDir) : track.location.toString(QUrl::FullyEncoded);
}

QString seconds(const Track &track)
{
    return QString::number(track.durationMs > 0 ? (track.durationMs + 500) / 1000 : -1);
}

} // namespace

PlaylistFile::Format PlaylistFile::formatForPath(const QString &path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("nap"))
        return Format::Native;
    if (suffix == QLatin1String("m3u") || suffix == QLatin1String("m3u8"))
        return Format::M3U;
    if (suffix == QLatin1String("pls"))
        return Format::PLS;
    return Format::Unknown;
}

QStringList PlaylistFile::nameFilters()
{
    return {QObject::tr("NewAmp playlist (*.nap)"), QObject::tr("M3U playlist (*.m3u8 *.m3u)"),
            QObject::tr("PLS playlist (*.pls)")};
}

bool PlaylistFile::isPortable(const Track &track)
{
    return track.sourceId == localId()
        && (track.location.isLocalFile() || isWebScheme(track.location.scheme()));
}

bool PlaylistFile::isHlsStream(const QByteArray &data)
{
    return data.contains("#EXT-X-");
}

Track PlaylistFile::trackForEntry(const QString &raw, const QUrl &base)
{
    const QString entry = raw.trimmed();
    // At least two letters before the colon: "C:" is a Windows drive.
    static const QRegularExpression Scheme(QStringLiteral("^[A-Za-z][A-Za-z0-9+.-]+:"));
    if (Scheme.match(entry).hasMatch()) {
        const QUrl url(entry);
        if (url.isLocalFile())
            return LocalFileSource::trackForFile(url.toLocalFile());
        if (isWebScheme(url.scheme()))
            return webTrack(url);
        // Another source's location (plex:, spotify:, ...): its scheme is the
        // source's id. If that source isn't loaded, the track is skipped.
        Track track;
        track.sourceId = url.scheme().toLower();
        track.location = url;
        return track;
    }
    if (base.isValid() && !base.isLocalFile())
        return webTrack(base.resolved(QUrl(entry)));

    // Windows playlists use backslashes; Qt takes '/' everywhere.
    QString path = entry;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    if (QDir::isRelativePath(path) && base.isLocalFile())
        path = QDir(base.toLocalFile()).absoluteFilePath(path);
    return LocalFileSource::trackForFile(QDir::cleanPath(path));
}

QList<Track> PlaylistFile::parse(const QByteArray &data, Format format, const QUrl &base)
{
    if (format == Format::Unknown)
        format = sniff(data);
    switch (format) {
    case Format::Native: return parseNative(data, base);
    case Format::PLS: return parsePLS(data, base);
    case Format::M3U:
    case Format::Unknown: break;
    }
    return parseM3U(data, base);
}

QByteArray PlaylistFile::write(const QList<Track> &tracks, Format format, const QString &baseDir, int *skipped)
{
    int left = 0;
    QByteArray out;
    if (format == Format::Native) {
        QJsonArray items;
        for (const Track &track : tracks) {
            QJsonObject o;
            o.insert(QStringLiteral("source"), track.sourceId);
            o.insert(QStringLiteral("location"),
                     track.sourceId == localId() ? entryFor(track, baseDir)
                                                 : track.location.toString(QUrl::FullyEncoded));
            const auto put = [&o](const char *key, const QString &value) {
                if (!value.isEmpty())
                    o.insert(QLatin1String(key), value);
            };
            put("title", track.title);
            put("artist", track.artist);
            put("album", track.album);
            if (track.durationMs > 0)
                o.insert(QStringLiteral("durationMs"), double(track.durationMs));
            items.append(o);
        }
        QJsonObject root;
        root.insert(QStringLiteral("newamp"), QStringLiteral("playlist"));
        root.insert(QStringLiteral("version"), 1);
        root.insert(QStringLiteral("tracks"), items);
        out = QJsonDocument(root).toJson();
    } else if (format == Format::PLS) {
        QStringList text = {QStringLiteral("[playlist]")};
        int n = 0;
        for (const Track &track : tracks) {
            if (!isPortable(track)) {
                ++left;
                continue;
            }
            ++n;
            text << QStringLiteral("File%1=%2").arg(n).arg(entryFor(track, baseDir))
                 << QStringLiteral("Title%1=%2").arg(n).arg(track.displayTitle())
                 << QStringLiteral("Length%1=%2").arg(n).arg(seconds(track));
        }
        text << QStringLiteral("NumberOfEntries=%1").arg(n) << QStringLiteral("Version=2");
        out = (text.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    } else {
        QStringList text = {QStringLiteral("#EXTM3U")};
        for (const Track &track : tracks) {
            if (!isPortable(track)) {
                ++left;
                continue;
            }
            text << QStringLiteral("#EXTINF:%1,%2").arg(seconds(track), track.displayTitle())
                 << entryFor(track, baseDir);
        }
        out = (text.join(QLatin1Char('\n')) + QLatin1Char('\n')).toUtf8();
    }
    if (skipped)
        *skipped = left;
    return out;
}

QList<Track> PlaylistFile::read(const QString &path, QString *error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (error)
            *error = file.errorString();
        return {};
    }
    const QByteArray data = file.readAll();
    Format format = formatForPath(path);
    if (format == Format::Unknown)
        format = sniff(data);
    if (format == Format::Native && !QJsonDocument::fromJson(data).isObject()) {
        if (error)
            *error = QObject::tr("%1 is not a NewAmp playlist").arg(QFileInfo(path).fileName());
        return {};
    }
    const QUrl base = QUrl::fromLocalFile(QFileInfo(path).absolutePath() + QLatin1Char('/'));
    return parse(data, format, base);
}

bool PlaylistFile::save(const QString &path, const QList<Track> &tracks, QString *error, int *skipped)
{
    const Format format = formatForPath(path);
    if (format == Format::Unknown) {
        if (error)
            *error = QObject::tr("Playlists are saved as .nap, .m3u8, .m3u or .pls");
        return false;
    }
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        if (error)
            *error = file.errorString();
        return false;
    }
    file.write(write(tracks, format, QFileInfo(path).absolutePath(), skipped));
    if (!file.commit()) {
        if (error)
            *error = file.errorString();
        return false;
    }
    return true;
}

} // namespace newamp

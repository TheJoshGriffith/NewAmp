#pragma once

#include "core/Track.h"

#include <QList>
#include <QStringList>
#include <QUrl>

namespace newamp {

// Reading and writing playlist files.
//
// M3U, M3U8 and PLS hold local files and web streams, so other players can
// use them. NewAmp's own format (.nap, JSON) also keeps tracks from plugin
// sources (plex:, jellyfin:, dlna:, spotify:) with their tags, so a list can
// mix sources; a track whose source isn't available is skipped at play time.
class PlaylistFile
{
public:
    enum class Format { Unknown, Native, M3U, PLS };

    static Format formatForPath(const QString &path);
    static bool isPlaylistFile(const QString &path) { return formatForPath(path) != Format::Unknown; }
    // For file dialogs; the native format comes first.
    static QStringList nameFilters();
    // Native for anything that isn't a web or local file, so nothing is lost.
    static bool isPortable(const Track &track);

    // Relative entries resolve against base: a local directory as a file URL
    // ending in '/', or the URL a remote playlist came from.
    static QList<Track> parse(const QByteArray &data, Format format, const QUrl &base);
    // Local entries are written relative to baseDir when they are inside it.
    // M3U and PLS leave out tracks that aren't portable; skipped counts them.
    static QByteArray write(const QList<Track> &tracks, Format format, const QString &baseDir,
                            int *skipped = nullptr);

    static QList<Track> read(const QString &path, QString *error = nullptr);
    static bool save(const QString &path, const QList<Track> &tracks, QString *error = nullptr,
                     int *skipped = nullptr);

    // An HLS stream's .m3u8 is something to play, not a list to open.
    static bool isHlsStream(const QByteArray &data);
    // One playlist entry: a path, a file or web URL, or a plugin location.
    static Track trackForEntry(const QString &entry, const QUrl &base);
};

} // namespace newamp

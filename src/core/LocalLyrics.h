#pragma once

#include "core/LyricsProvider.h"

namespace newamp {

// A .lrc (or .txt) file with the same name as a local track, the way most
// players and Plex look for them.
class SidecarLyrics : public LyricsProvider
{
public:
    QString name() const override;
    bool canHandle(const Track &track) const override;
    void fetch(const Track &track, const Callback &done) override;

    // The sidecar file for a track's path, or empty when there is none.
    static QString sidecarPath(const QString &trackPath);
    // Reads a lyrics file: UTF-8 (with or without BOM), UTF-16 with BOM,
    // otherwise Latin-1.
    static QString readTextFile(const QString &path);
};

// Lyrics saved in a local file's tags (ID3 USLT/SYLT, Vorbis LYRICS, MP4).
class TagLyrics : public LyricsProvider
{
public:
    QString name() const override;
    bool canHandle(const Track &track) const override;
    void fetch(const Track &track, const Callback &done) override;
};

} // namespace newamp

#pragma once

#include <QString>

class QIODevice;

namespace newamp {

struct Tags
{
    QString title;
    QString artist;
    QString album;
    // Synced lyrics as LRC when the file has them (ID3 SYLT), else the
    // lyrics text (USLT, LYRICS, \xa9lyr), which may itself be LRC.
    QString lyrics;
};

// Reads title, artist, album and lyrics from ID3v2/ID3v1 (MP3), FLAC and Ogg Vorbis
// comments, and MP4/M4A ilst atoms. Only the start (and, for ID3v1, the end)
// of the file is read, apart from MP4, whose atoms are walked by seeking.
class TagReader
{
public:
    static Tags read(const QString &path);
    static Tags read(QIODevice &device);

    // Exposed for tests.
    static Tags parseId3v2(const QByteArray &data);
    static Tags parseId3v1(const QByteArray &last128);
    static Tags parseVorbisComments(const QByteArray &block);
};

} // namespace newamp

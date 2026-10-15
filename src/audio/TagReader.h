#pragma once

#include <QString>

class QIODevice;

namespace newamp {

struct Tags
{
    QString title;
    QString artist;
    QString album;
};

// Reads title, artist and album from ID3v2/ID3v1 (MP3), FLAC and Ogg Vorbis
// comments. Only the start (and, for ID3v1, the end) of the file is read.
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

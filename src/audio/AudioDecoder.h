#pragma once

#include <QString>

#include <memory>

class QIODevice;

namespace newamp {

// Decodes MP3, FLAC, WAV and Ogg Vorbis to interleaved float samples, with
// sample-accurate seeking. Reads through a QIODevice, so the same code works
// for local files and, later, buffered network streams.
class AudioDecoder
{
public:
    AudioDecoder();
    ~AudioDecoder();
    AudioDecoder(const AudioDecoder &) = delete;
    AudioDecoder &operator=(const AudioDecoder &) = delete;

    static bool canDecode(const QString &path);

    // Takes ownership of the device. maxChannels > 0 downmixes surround
    // sources; sampleRate > 0 resamples. Returns false if it can't decode.
    bool open(std::unique_ptr<QIODevice> device, int maxChannels = 2, int sampleRate = 0);
    void close();
    bool isOpen() const;

    int channels() const { return m_channels; }
    int sampleRate() const { return m_sampleRate; }
    // Native rate of the file, before any resampling.
    int sourceSampleRate() const { return m_sourceSampleRate; }
    int sourceChannels() const { return m_sourceChannels; }
    qint64 lengthFrames() const { return m_lengthFrames; } // -1 if unknown
    qint64 positionFrames() const { return m_position; }

    // Returns frames read; fewer than asked means end of stream.
    qint64 read(float *out, qint64 frames);
    bool seek(qint64 frame);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    int m_channels = 0;
    int m_sampleRate = 0;
    int m_sourceChannels = 0;
    int m_sourceSampleRate = 0;
    qint64 m_lengthFrames = -1;
    qint64 m_position = 0;
};

} // namespace newamp

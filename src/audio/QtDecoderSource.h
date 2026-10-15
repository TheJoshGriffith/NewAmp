#pragma once

#include "audio/PcmSource.h"

#include <QObject>
#include <QString>

#include <memory>
#include <mutex>
#include <vector>

class QAudioBuffer;
class QAudioDecoder;

namespace newamp {

// Decodes whatever Qt Multimedia can (AAC/M4A, ALAC, Opus...) for DspBackend.
// QAudioDecoder pushes buffers through signals on this object's thread and
// can't seek, so the whole track is decoded, as fast as the platform allows,
// into memory as 16-bit samples (about 10 MB a minute for stereo 44.1 kHz).
// read() serves what has arrived so far and may run on the audio thread.
// Seeking reaches only audio decoded so far; a seek further ahead stops at
// the decoded end, which on a local file is rarely more than a moment away.
class QtDecoderSource final : public QObject, public PcmSource
{
    Q_OBJECT

public:
    explicit QtDecoderSource(QObject *parent = nullptr);
    ~QtDecoderSource() override;

    // Starts decoding and runs a local event loop until the first audio
    // arrives (or timeoutMs passes). Asks the platform for float samples at
    // `sampleRate` with `channels` channels; it resamples and downmixes, and
    // whatever it actually delivers is what channels() and sampleRate() then
    // report (never more than `channels`). Leaving the format at 0 asks for
    // the file's own, which some backends (GStreamer in Qt 6.4) can't do.
    // Returns false if the file can't be decoded.
    bool open(const QString &path, int channels, int sampleRate, int timeoutMs = 5000);

    int channels() const override { return m_channels; }
    int sampleRate() const override { return m_sampleRate; }
    // Qt doesn't say what the file itself holds, so these match the output.
    int sourceSampleRate() const override { return m_sampleRate; }
    int sourceChannels() const override { return m_channels; }
    qint64 lengthFrames() const override;
    qint64 positionFrames() const override;

    qint64 read(float *out, qint64 frames) override;
    bool finished() const override;
    bool seek(qint64 frame) override;
    // Drops the decoded audio; seek(0) decodes the file again.
    void release() override;

Q_SIGNALS:
    // The length became known or more exact.
    void lengthChanged();

private:
    bool startAndWait(int timeoutMs);
    void takeBuffers();
    void append(const QAudioBuffer &buffer);
    void onFinished();
    void onError();
    void reset();

    std::unique_ptr<QAudioDecoder> m_decoder;
    int m_channels = 0;
    int m_sampleRate = 0;
    int m_maxChannels = 0;
    bool m_released = false;
    bool m_failed = false;

    mutable std::mutex m_mutex; // guards everything below
    qint64 m_durationMs = -1; // as the decoder reports it, until it's done
    std::vector<std::unique_ptr<qint16[]>> m_chunks; // ChunkFrames frames each
    qint64 m_decodedFrames = 0;
    qint64 m_position = 0;
    qint64 m_lengthFrames = -1;
    bool m_done = false;
};

} // namespace newamp

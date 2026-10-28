#pragma once

#include "audio/AudioBackend.h"
#include "audio/PcmSource.h"

#include <QAudioFormat>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <vector>

class QAudioSink;
class QIODevice;
class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

namespace newamp {

class DspRack;
class DspStream;
struct Tags;

// NewAmp's own pipeline for local files and remote tracks (fetched whole first):
// PcmSource -> DspRack (plugin effects) -> DspChain (EQ, preamp, balance,
// volume) -> QAudioSink.
// MP3, FLAC, WAV and Ogg come from AudioDecoder (miniaudio); anything else
// the platform can decode, such as AAC, from QtDecoderSource.
// The sink pulls audio through DspStream, possibly from its own thread, so
// the source and DSP state are guarded by one mutex.
class DspBackend final : public AudioBackend
{
    Q_OBJECT

public:
    explicit DspBackend(QObject *parent = nullptr);
    ~DspBackend() override;

    static bool canPlay(const QUrl &url);

    bool load(const QUrl &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    qint64 position() const override;
    qint64 duration() const override;
    void setDspParams(const DspParams &params) override;
    bool supportsEffects() const override { return true; }
    int visualizerSamples(float *out, int count) const override;

    // Plugin effects to run before the equalizer; not owned.
    void setDspRack(DspRack *rack) { m_rack = rack; }

Q_SIGNALS:
    // A remote track arrived but couldn't be decoded here; `play` says whether
    // it should start once another backend has it.
    void cannotDecode(const QUrl &url, bool play);

private:
    using DeviceFactory = std::function<std::unique_ptr<QIODevice>()>;

    friend class DspStream;

    // Called by DspStream; fills `bytes` of output. Returns bytes written.
    qint64 render(char *out, qint64 bytes);
    void setState(State state);
    void startSink();
    void markPosition(qint64 positionMs);
    void emitDuration();
    // name picks the decoder by suffix; for a file it is also the path.
    std::unique_ptr<PcmSource> decode(const QString &name, const DeviceFactory &openDevice, bool isFile);
    std::unique_ptr<PcmSource> decodeOnce(const QString &name, const DeviceFactory &openDevice, bool isFile);
    bool loadDeferred();
    bool install(std::unique_ptr<PcmSource> source, const Tags &tags, qint64 size);
    void startDownload(const QUrl &url);
    void cancelDownload();

    mutable std::mutex m_mutex; // guards source, chain, scratch buffer
    std::unique_ptr<PcmSource> m_source;
    DspChain m_chain;
    DspRack *m_rack = nullptr;
    std::vector<float> m_scratch;
    QAudioFormat m_format;
    qint64 m_fileSize = 0;

    mutable std::mutex m_visMutex;
    std::vector<float> m_visRing; // mono, newest at m_visWrite - 1
    size_t m_visWrite = 0;

    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<DspStream> m_stream;
    QTimer *m_positionTimer;
    QNetworkAccessManager *m_network = nullptr; // created on first remote track
    QNetworkReply *m_download = nullptr;        // the track being fetched
    bool m_playWhenReady = false;
    bool m_decoding = false;
    std::optional<QUrl> m_deferred; // asked for while decoding
    std::atomic<bool> m_atEnd{false};
    State m_state = State::Stopped;
    DspParams m_params;
    // Position = m_baseMs + audio the device has played since m_baseProcessedUs.
    qint64 m_baseMs = 0;
    qint64 m_baseProcessedUs = 0;
};

} // namespace newamp

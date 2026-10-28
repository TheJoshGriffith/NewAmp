#pragma once

#include "audio/AudioBackend.h"
#include "audio/PcmSource.h"
#include "audio/TagReader.h"
#include "audio/TrackMixer.h"

#include <QAudioDevice>
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

// NewAmp's own pipeline for local files and remote tracks (fetched whole first):
// PcmSource -> TrackMixer (ReplayGain, gapless, crossfade) -> DspRack (plugin
// effects) -> DspChain (EQ, preamp, balance, volume) -> QAudioSink.
// MP3, FLAC, WAV and Ogg come from AudioDecoder (miniaudio); anything else
// the platform can decode, such as AAC, from QtDecoderSource. The next track
// is prepared shortly before the current one ends, in the current output
// format, so the mixer can move on to it without restarting the sink.
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
    void setPlaybackOptions(const PlaybackOptions &options) override;
    void setNext(const QUrl &url) override;
    int visualizerSamples(float *out, int count) const override;

    // Plugin effects to run before the equalizer; not owned.
    void setDspRack(DspRack *rack) { m_rack = rack; }

Q_SIGNALS:
    // A remote track arrived but couldn't be decoded here; `play` says whether
    // it should start once another backend has it.
    void cannotDecode(const QUrl &url, bool play);

private:
    using DeviceFactory = std::function<std::unique_ptr<QIODevice>()>;

    // What the UI hears about a track once it plays.
    struct TrackInfo
    {
        Tags tags;
        qint64 size = 0;
        int sourceRate = 0;
        int sourceChannels = 0;
    };
    enum class NextState { None, Waiting, Loading, Ready, Failed };

    friend class DspStream;

    // Called by DspStream; fills `bytes` of output. Returns bytes written.
    qint64 render(char *out, qint64 bytes);
    void setState(State state);
    void startSink();
    void createSink();
    QAudioDevice outputDevice() const;
    void markPosition(qint64 positionMs);
    void emitDuration();
    // name picks the decoder by suffix; for a file it is also the path.
    // A valid `target` asks for exactly that rate and channel count.
    std::unique_ptr<PcmSource> decode(const QString &name, const DeviceFactory &openDevice, bool isFile,
                                      const QAudioFormat &target = {});
    std::unique_ptr<PcmSource> decodeOnce(const QString &name, const DeviceFactory &openDevice, bool isFile,
                                          const QAudioFormat &target = {});
    bool loadDeferred();
    bool install(std::unique_ptr<PcmSource> source, const Tags &tags, qint64 size);
    void startDownload(const QUrl &url);
    void cancelDownload();
    // The next track: fetched and decoded once the current one nears its end.
    void preloadIfDue();
    void preload();
    void preloaded(std::unique_ptr<PcmSource> source, const Tags &tags, qint64 size);
    void clearNext();
    // Hands over to the next track once the device plays its first frame;
    // `now` does it at once (before a seek or stop).
    void checkTransition(bool now = false);
    void freeRetired();
    float gainFor(const Tags &tags) const;

    mutable std::mutex m_mutex; // guards mixer, chain, scratch buffer
    TrackMixer m_mixer;
    DspChain m_chain;
    DspRack *m_rack = nullptr;
    std::vector<float> m_scratch;
    QAudioFormat m_format;
    TrackInfo m_info;
    TrackInfo m_nextInfo;
    PlaybackOptions m_options;

    mutable std::mutex m_visMutex;
    std::vector<float> m_visRing; // mono, newest at m_visWrite - 1
    size_t m_visWrite = 0;

    std::unique_ptr<QAudioSink> m_sink;
    std::unique_ptr<DspStream> m_stream;
    QTimer *m_positionTimer;
    QNetworkAccessManager *m_network = nullptr; // created on first remote track
    QNetworkReply *m_download = nullptr;        // the track being fetched
    QNetworkReply *m_nextDownload = nullptr;    // the next track being fetched
    QUrl m_nextUrl;
    NextState m_nextState = NextState::None;
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

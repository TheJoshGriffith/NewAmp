#include "audio/DspBackend.h"

#include "audio/AudioDecoder.h"
#include "audio/QtDecoderSource.h"
#include "audio/TagReader.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QTimer>

#include <algorithm>
#include <cstring>

namespace newamp {

namespace {
constexpr int VisRingSize = 4096;
constexpr int BufferMs = 150;

QAudioFormat floatFormat(int sampleRate, int channels)
{
    QAudioFormat format;
    format.setSampleRate(sampleRate);
    format.setChannelCount(channels);
    format.setSampleFormat(QAudioFormat::Float);
    return format;
}
} // namespace

// The QIODevice the sink pulls from. Endless until the source runs dry.
class DspStream : public QIODevice
{
public:
    explicit DspStream(DspBackend &backend)
        : m_backend(backend)
    {
    }

    bool isSequential() const override { return true; }
    qint64 bytesAvailable() const override
    {
        return m_backend.m_atEnd ? QIODevice::bytesAvailable() : 1 << 20;
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override { return m_backend.render(data, maxSize); }
    qint64 writeData(const char *, qint64) override { return -1; }

private:
    DspBackend &m_backend;
};

DspBackend::DspBackend(QObject *parent)
    : AudioBackend(parent)
    , m_visRing(VisRingSize, 0.0f)
    , m_stream(std::make_unique<DspStream>(*this))
    , m_positionTimer(new QTimer(this))
{
    m_stream->open(QIODevice::ReadOnly);
    m_positionTimer->setInterval(100);
    connect(m_positionTimer, &QTimer::timeout, this, [this] { Q_EMIT positionChanged(position()); });
}

DspBackend::~DspBackend()
{
    if (m_sink)
        m_sink->stop();
}

bool DspBackend::canPlay(const QUrl &url)
{
    return url.isLocalFile();
}

bool DspBackend::load(const QUrl &url)
{
    if (!canPlay(url))
        return false;
    stop();
    // Free the previous track before decoding the next one.
    {
        std::lock_guard lock(m_mutex);
        m_source.reset();
    }
    m_sink.reset();

    const QString path = url.toLocalFile();
    Tags tags;
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        tags = TagReader::read(file);
        m_fileSize = file.size();
    }

    // miniaudio first; then whatever the platform's decoder handles.
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    std::unique_ptr<PcmSource> source;
    if (AudioDecoder::canDecode(path)) {
        auto decoder = std::make_unique<AudioDecoder>();
        auto open = [&](int maxChannels, int sampleRate) {
            auto file = std::make_unique<QFile>(path);
            return file->open(QIODevice::ReadOnly) && decoder->open(std::move(file), maxChannels, sampleRate);
        };
        // Ask for the file's own format; if the output device can't take it,
        // let the decoder resample and downmix to what the device prefers.
        bool ok = open(0, 0);
        if (ok && !device.isNull()
            && !device.isFormatSupported(floatFormat(decoder->sampleRate(), decoder->channels()))) {
            const QAudioFormat preferred = device.preferredFormat();
            ok = open(qMin(2, preferred.channelCount()), preferred.sampleRate());
        }
        if (ok)
            source = std::move(decoder);
    }
    if (!source) {
        // Qt can't always decode to the file's own format (GStreamer in
        // Qt 6.4 fails to), so ask for what the device prefers.
        const QAudioFormat preferred = device.preferredFormat();
        const int channels = preferred.channelCount() > 0 ? qMin(2, preferred.channelCount()) : 2;
        const int sampleRate = preferred.sampleRate() > 0 ? preferred.sampleRate() : 44100;
        auto decoder = std::make_unique<QtDecoderSource>();
        if (decoder->open(path, channels, sampleRate)) {
            // Queued: the source may report from inside a call we make under m_mutex.
            connect(decoder.get(), &QtDecoderSource::lengthChanged, this, &DspBackend::emitDuration,
                    Qt::QueuedConnection);
            source = std::move(decoder);
        }
    }
    if (!source)
        return false;

    QAudioFormat format = floatFormat(source->sampleRate(), source->channels());
    if (!device.isNull() && !device.isFormatSupported(format))
        format.setSampleFormat(QAudioFormat::Int16);
    const int sourceRate = source->sourceSampleRate();
    const int sourceChannels = source->sourceChannels();
    {
        std::lock_guard lock(m_mutex);
        m_source = std::move(source);
        m_format = format;
        m_chain.configure(m_format.sampleRate(), m_format.channelCount());
        m_chain.setParams(m_params);
    }

    m_sink = std::make_unique<QAudioSink>(device, m_format);
    m_sink->setBufferSize(m_format.bytesForDuration(BufferMs * 1000));
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        // The stream returns no data once the source is done; the sink then
        // drains and goes idle, which is the real end of the track.
        if (state == QAudio::IdleState && m_atEnd && m_state == State::Playing) {
            stop();
            Q_EMIT endOfTrack();
        }
    });
    m_atEnd = false;

    emitDuration();
    Q_EMIT metaDataChanged(tags.title, tags.artist, tags.album);
    Q_EMIT formatChanged(sourceRate, sourceChannels);
    return true;
}

void DspBackend::emitDuration()
{
    const qint64 durationMs = duration();
    Q_EMIT durationChanged(durationMs);
    Q_EMIT bitrateChanged(durationMs > 0 ? int(m_fileSize * 8 / durationMs) : 0);
}

void DspBackend::startSink()
{
    m_stream->close();
    m_stream->open(QIODevice::ReadOnly);
    m_sink->start(m_stream.get());
}

void DspBackend::markPosition(qint64 positionMs)
{
    m_baseMs = positionMs;
    m_baseProcessedUs = m_sink ? m_sink->processedUSecs() : 0;
}

void DspBackend::play()
{
    if (!m_sink)
        return;
    if (m_state == State::Paused) {
        m_sink->resume();
    } else if (m_state == State::Stopped) {
        {
            std::lock_guard lock(m_mutex);
            if (m_source)
                m_source->seek(0);
            m_chain.reset();
        }
        m_atEnd = false;
        startSink();
        markPosition(0);
    }
    m_positionTimer->start();
    setState(State::Playing);
}

void DspBackend::pause()
{
    if (m_state != State::Playing || !m_sink)
        return;
    m_sink->suspend();
    m_positionTimer->stop();
    setState(State::Paused);
}

void DspBackend::stop()
{
    if (m_sink)
        m_sink->stop();
    m_positionTimer->stop();
    {
        std::lock_guard lock(m_mutex);
        if (m_source)
            m_source->release();
    }
    setState(State::Stopped);
}

void DspBackend::seek(qint64 positionMs)
{
    // Play always starts from the top, and a stopped source may have
    // released its audio, so there's nothing to seek in.
    if (m_state == State::Stopped)
        return;
    qint64 reachedMs = 0;
    {
        std::lock_guard lock(m_mutex);
        if (!m_source)
            return;
        const int rate = m_source->sampleRate();
        m_source->seek(qMax<qint64>(0, positionMs) * rate / 1000);
        m_chain.reset();
        // A source may stop short of the target (QtDecoderSource only
        // reaches what it has decoded so far).
        reachedMs = m_source->positionFrames() * 1000 / rate;
    }
    const bool wasAtEnd = m_atEnd.exchange(false);
    // A sink that already drained needs restarting to pull again.
    if (wasAtEnd && m_state == State::Playing && m_sink)
        startSink();
    markPosition(reachedMs);
    Q_EMIT positionChanged(position());
}

qint64 DspBackend::position() const
{
    if (m_state == State::Stopped || !m_sink)
        return 0;
    // Count what the device has actually played, not what we've decoded,
    // so the clock matches what you hear.
    const qint64 played = m_baseMs + (m_sink->processedUSecs() - m_baseProcessedUs) / 1000;
    const qint64 length = duration();
    return length > 0 ? qMin(played, length) : played;
}

qint64 DspBackend::duration() const
{
    std::lock_guard lock(m_mutex);
    if (!m_source || m_source->lengthFrames() < 0 || m_source->sampleRate() <= 0)
        return -1;
    return m_source->lengthFrames() * 1000 / m_source->sampleRate();
}

void DspBackend::setDspParams(const DspParams &params)
{
    std::lock_guard lock(m_mutex);
    m_params = params;
    m_chain.setParams(params);
}

qint64 DspBackend::render(char *out, qint64 bytes)
{
    const int channels = m_format.channelCount();
    const int bytesPerFrame = m_format.bytesPerFrame();
    if (bytesPerFrame <= 0 || m_atEnd)
        return 0;
    const qint64 frames = bytes / bytesPerFrame;

    qint64 got = 0;
    {
        std::lock_guard lock(m_mutex);
        m_scratch.resize(size_t(frames * channels));
        got = m_source ? m_source->read(m_scratch.data(), frames) : 0;
        if (got < frames && m_source && !m_source->finished()) {
            // The source is still decoding: play silence, don't end.
            std::fill(m_scratch.begin() + got * channels, m_scratch.end(), 0.0f);
            got = frames;
        } else if (got < frames) {
            m_atEnd = true;
        }
        m_chain.process(m_scratch.data(), got);
    }

    const qint64 samples = got * channels;
    if (m_format.sampleFormat() == QAudioFormat::Float) {
        std::memcpy(out, m_scratch.data(), size_t(samples) * sizeof(float));
    } else {
        auto *dst = reinterpret_cast<qint16 *>(out);
        for (qint64 i = 0; i < samples; ++i)
            dst[i] = qint16(m_scratch[size_t(i)] * 32767.0f);
    }

    // Keep a mono copy for the visualizer.
    std::lock_guard lock(m_visMutex);
    for (qint64 frame = 0; frame < got; ++frame) {
        float sum = 0;
        for (int ch = 0; ch < channels; ++ch)
            sum += m_scratch[size_t(frame * channels + ch)];
        m_visRing[m_visWrite] = sum / channels;
        m_visWrite = (m_visWrite + 1) % m_visRing.size();
    }
    return got * bytesPerFrame;
}

int DspBackend::visualizerSamples(float *out, int count) const
{
    if (m_state != State::Playing)
        return 0;
    std::lock_guard lock(m_visMutex);
    count = qMin(count, int(m_visRing.size()));
    // The ring holds audio slightly ahead of the speaker; that small lead is fine.
    size_t at = (m_visWrite + m_visRing.size() - size_t(count)) % m_visRing.size();
    for (int i = 0; i < count; ++i) {
        out[i] = m_visRing[at];
        at = (at + 1) % m_visRing.size();
    }
    return count;
}

void DspBackend::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

} // namespace newamp

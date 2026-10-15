#include "audio/DspBackend.h"

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
} // namespace

// The QIODevice the sink pulls from. Endless until the decoder runs dry.
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
    return url.isLocalFile() && AudioDecoder::canDecode(url.toLocalFile());
}

bool DspBackend::load(const QUrl &url)
{
    if (!canPlay(url))
        return false;
    stop();

    const QString path = url.toLocalFile();
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly))
        return false;
    const Tags tags = TagReader::read(*file);
    const qint64 fileSize = file->size();

    // Ask for the file's own format; if the output device can't take it,
    // let the decoder resample and downmix to what the device prefers.
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    {
        std::lock_guard lock(m_mutex);
        if (!m_decoder.open(std::move(file)))
            return false;
        QAudioFormat format;
        format.setSampleRate(m_decoder.sampleRate());
        format.setChannelCount(m_decoder.channels());
        format.setSampleFormat(QAudioFormat::Float);
        if (!device.isNull() && !device.isFormatSupported(format)) {
            const QAudioFormat preferred = device.preferredFormat();
            auto reopen = std::make_unique<QFile>(path);
            if (!reopen->open(QIODevice::ReadOnly)
                || !m_decoder.open(std::move(reopen), qMin(2, preferred.channelCount()), preferred.sampleRate()))
                return false;
            format.setSampleRate(m_decoder.sampleRate());
            format.setChannelCount(m_decoder.channels());
            if (!device.isFormatSupported(format))
                format.setSampleFormat(QAudioFormat::Int16);
        }
        m_format = format;
        m_chain.configure(m_format.sampleRate(), m_format.channelCount());
        m_chain.setParams(m_params);
    }

    m_sink = std::make_unique<QAudioSink>(device, m_format);
    m_sink->setBufferSize(m_format.bytesForDuration(BufferMs * 1000));
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        // The stream returns no data once the decoder is done; the sink then
        // drains and goes idle, which is the real end of the track.
        if (state == QAudio::IdleState && m_atEnd && m_state == State::Playing) {
            stop();
            Q_EMIT endOfTrack();
        }
    });
    m_atEnd = false;

    const qint64 durationMs = duration();
    Q_EMIT durationChanged(durationMs);
    Q_EMIT metaDataChanged(tags.title, tags.artist, tags.album);
    Q_EMIT formatChanged(m_decoder.sourceSampleRate(), m_decoder.sourceChannels());
    Q_EMIT bitrateChanged(durationMs > 0 ? int(fileSize * 8 / durationMs) : 0);
    return true;
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
            m_decoder.seek(0);
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
    setState(State::Stopped);
}

void DspBackend::seek(qint64 positionMs)
{
    {
        std::lock_guard lock(m_mutex);
        if (!m_decoder.isOpen())
            return;
        m_decoder.seek(positionMs * m_decoder.sampleRate() / 1000);
        m_chain.reset();
    }
    const bool wasAtEnd = m_atEnd.exchange(false);
    // A sink that already drained needs restarting to pull again.
    if (wasAtEnd && m_state == State::Playing && m_sink)
        startSink();
    markPosition(qMax<qint64>(0, positionMs));
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
    if (!m_decoder.isOpen() || m_decoder.lengthFrames() < 0 || m_decoder.sampleRate() <= 0)
        return -1;
    return m_decoder.lengthFrames() * 1000 / m_decoder.sampleRate();
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
        got = m_decoder.read(m_scratch.data(), frames);
        m_chain.process(m_scratch.data(), got);
    }
    if (got < frames)
        m_atEnd = true;

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

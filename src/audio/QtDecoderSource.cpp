#include "audio/QtDecoderSource.h"

#include <QAudioBuffer>
#include <QAudioDecoder>
#include <QEventLoop>
#include <QFileInfo>
#include <QTimer>
#include <QUrl>

#include <algorithm>

namespace newamp {

namespace {
constexpr qint64 ChunkFrames = 1 << 15;
} // namespace

QtDecoderSource::QtDecoderSource(QObject *parent)
    : QObject(parent)
{
}

QtDecoderSource::~QtDecoderSource()
{
    // Our slots lock m_mutex, which is destroyed before m_decoder.
    if (m_decoder) {
        m_decoder->disconnect(this);
        m_decoder->stop();
    }
}

void QtDecoderSource::reset()
{
    if (m_decoder) {
        m_decoder->disconnect(this);
        m_decoder->stop();
    }
    m_decoder.reset();
    m_channels = m_sampleRate = m_maxChannels = 0;
    m_released = m_failed = false;
    std::lock_guard lock(m_mutex);
    m_chunks.clear();
    m_decodedFrames = m_position = 0;
    m_lengthFrames = m_durationMs = -1;
    m_done = false;
}

bool QtDecoderSource::open(const QString &path, int channels, int sampleRate, int timeoutMs)
{
    reset();
    m_decoder = std::make_unique<QAudioDecoder>();
    if (!m_decoder->isSupported())
        return false;
    connect(m_decoder.get(), &QAudioDecoder::bufferReady, this, &QtDecoderSource::takeBuffers);
    connect(m_decoder.get(), &QAudioDecoder::finished, this, &QtDecoderSource::onFinished);
    connect(m_decoder.get(), qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), this, &QtDecoderSource::onError);
    connect(m_decoder.get(), &QAudioDecoder::durationChanged, this, [this](qint64 ms) {
        {
            std::lock_guard lock(m_mutex);
            if (m_done || ms <= 0)
                return;
            m_durationMs = ms;
        }
        Q_EMIT lengthChanged();
    });
    m_decoder->setSource(QUrl::fromLocalFile(QFileInfo(path).absoluteFilePath()));
    if (channels > 0 && sampleRate > 0) {
        QAudioFormat format;
        format.setSampleFormat(QAudioFormat::Float);
        format.setSampleRate(sampleRate);
        format.setChannelCount(channels);
        m_decoder->setAudioFormat(format);
    }
    m_maxChannels = channels;
    return startAndWait(timeoutMs);
}

bool QtDecoderSource::startAndWait(int timeoutMs)
{
    m_failed = false;
    auto ready = [this] {
        std::lock_guard lock(m_mutex);
        return m_decodedFrames > 0 || m_done || m_failed;
    };
    // Signals reach us through the event loop, so run one until the first
    // audio or an error. User input waits, so the UI can't re-enter here.
    QEventLoop loop;
    QTimer timer;
    timer.setSingleShot(true);
    connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    connect(m_decoder.get(), &QAudioDecoder::bufferReady, &loop, &QEventLoop::quit);
    connect(m_decoder.get(), &QAudioDecoder::finished, &loop, &QEventLoop::quit);
    connect(m_decoder.get(), qOverload<QAudioDecoder::Error>(&QAudioDecoder::error), &loop, &QEventLoop::quit);
    timer.start(timeoutMs);
    m_decoder->start();
    while (!ready() && timer.isActive())
        loop.exec(QEventLoop::ExcludeUserInputEvents);

    bool ok;
    {
        std::lock_guard lock(m_mutex);
        ok = !m_failed && m_decodedFrames > 0;
    }
    if (!ok)
        m_decoder->stop(); // outside the lock: it may call back into us
    return ok;
}

void QtDecoderSource::takeBuffers()
{
    while (m_decoder && m_decoder->bufferAvailable() && !m_failed)
        append(m_decoder->read());
}

void QtDecoderSource::append(const QAudioBuffer &buffer)
{
    const QAudioFormat format = buffer.format();
    const int inChannels = format.channelCount();
    const qint64 frames = buffer.frameCount();
    if (!buffer.isValid() || inChannels <= 0 || frames <= 0)
        return;
    if (m_sampleRate == 0) {
        // The first buffer sets the format. The platform may not have given
        // exactly what we asked for; any rate will do, but we play at most
        // the channels asked for.
        m_sampleRate = format.sampleRate();
        m_channels = m_maxChannels > 0 ? qMin(inChannels, m_maxChannels) : inChannels;
    } else if (format.sampleRate() != m_sampleRate) {
        // The format changed midway; we can't resample, so stop there.
        m_decoder->stop();
        onFinished();
        return;
    }

    // Whatever sample type arrives is stored as 16-bit. If the platform
    // ignored the channel count we asked for, map channels simply here.
    const auto *data = buffer.constData<char>();
    const int bytesPerSample = format.bytesPerSample();
    auto sample = [&](qint64 frame, int ch) {
        return format.normalizedSampleValue(data + (frame * inChannels + ch) * bytesPerSample);
    };
    const int channels = m_channels;
    std::lock_guard lock(m_mutex);
    for (qint64 i = 0; i < frames; ++i) {
        const qint64 at = m_decodedFrames + i;
        if (at / ChunkFrames >= qint64(m_chunks.size()))
            m_chunks.push_back(std::make_unique<qint16[]>(size_t(ChunkFrames * channels)));
        qint16 *out = m_chunks[size_t(at / ChunkFrames)].get() + (at % ChunkFrames) * channels;
        for (int ch = 0; ch < channels; ++ch) {
            float v = 0;
            if (channels == 1 && inChannels > 1) {
                for (int c = 0; c < inChannels; ++c)
                    v += sample(i, c);
                v /= float(inChannels);
            } else {
                v = sample(i, qMin(ch, inChannels - 1));
            }
            out[ch] = qint16(std::clamp(v * 32768.0f, -32768.0f, 32767.0f));
        }
    }
    m_decodedFrames += frames;
}

void QtDecoderSource::onFinished()
{
    takeBuffers();
    {
        std::lock_guard lock(m_mutex);
        if (m_done)
            return;
        m_done = true;
        m_lengthFrames = m_decodedFrames;
    }
    Q_EMIT lengthChanged();
}

void QtDecoderSource::onError()
{
    bool any;
    {
        std::lock_guard lock(m_mutex);
        any = m_decodedFrames > 0;
    }
    // A file that breaks partway plays up to the break.
    if (any) {
        onFinished();
        return;
    }
    m_failed = true;
    std::lock_guard lock(m_mutex);
    m_done = true;
}

qint64 QtDecoderSource::lengthFrames() const
{
    std::lock_guard lock(m_mutex);
    if (m_lengthFrames >= 0)
        return m_lengthFrames;
    if (m_durationMs > 0 && m_sampleRate > 0)
        return m_durationMs * m_sampleRate / 1000;
    return -1;
}

qint64 QtDecoderSource::positionFrames() const
{
    std::lock_guard lock(m_mutex);
    return m_position;
}

qint64 QtDecoderSource::read(float *out, qint64 frames)
{
    std::lock_guard lock(m_mutex);
    const int channels = m_channels;
    const qint64 n = qBound<qint64>(0, frames, m_decodedFrames - m_position);
    qint64 done = 0;
    while (done < n) {
        const qint64 at = m_position + done;
        const qint64 offset = at % ChunkFrames;
        const qint64 count = qMin(n - done, ChunkFrames - offset);
        const qint16 *in = m_chunks[size_t(at / ChunkFrames)].get() + offset * channels;
        float *dst = out + done * channels;
        for (qint64 i = 0; i < count * channels; ++i)
            dst[i] = float(in[i]) / 32768.0f;
        done += count;
    }
    m_position += n;
    return n;
}

bool QtDecoderSource::finished() const
{
    std::lock_guard lock(m_mutex);
    return m_done && m_position >= m_decodedFrames;
}

bool QtDecoderSource::seek(qint64 frame)
{
    if (!m_decoder)
        return false;
    if (m_released) {
        // Decode again from the start; the reader gets silence until it arrives.
        m_released = false;
        m_decoder->start();
    }
    std::lock_guard lock(m_mutex);
    m_position = qBound<qint64>(0, frame, m_decodedFrames);
    return true;
}

void QtDecoderSource::release()
{
    if (!m_decoder || m_released)
        return;
    m_decoder->stop();
    m_released = true;
    std::lock_guard lock(m_mutex);
    m_chunks.clear();
    m_chunks.shrink_to_fit();
    m_decodedFrames = m_position = 0;
    m_done = false;
}

} // namespace newamp

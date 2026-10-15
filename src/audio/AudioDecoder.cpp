#include "audio/AudioDecoder.h"

#include <QFileInfo>
#include <QIODevice>

#include "miniaudio.h"

namespace newamp {

struct AudioDecoder::Impl
{
    std::unique_ptr<QIODevice> device;
    ma_decoder decoder{};
    bool open = false;

    static ma_result onRead(ma_decoder *decoder, void *buffer, size_t bytes, size_t *bytesRead)
    {
        auto *self = static_cast<Impl *>(decoder->pUserData);
        const qint64 n = self->device->read(static_cast<char *>(buffer), qint64(bytes));
        *bytesRead = n > 0 ? size_t(n) : 0;
        if (n < 0)
            return MA_IO_ERROR;
        return n == 0 && bytes > 0 ? MA_AT_END : MA_SUCCESS;
    }

    static ma_result onSeek(ma_decoder *decoder, ma_int64 offset, ma_seek_origin origin)
    {
        auto *self = static_cast<Impl *>(decoder->pUserData);
        qint64 target = offset;
        if (origin == ma_seek_origin_current)
            target += self->device->pos();
        else if (origin == ma_seek_origin_end)
            target += self->device->size();
        return self->device->seek(target) ? MA_SUCCESS : MA_IO_ERROR;
    }

    bool init(int channels, int sampleRate)
    {
        ma_decoder_config config = ma_decoder_config_init(ma_format_f32, ma_uint32(channels), ma_uint32(sampleRate));
        device->seek(0);
        open = ma_decoder_init(onRead, onSeek, this, &config, &decoder) == MA_SUCCESS;
        return open;
    }

    void uninit()
    {
        if (open)
            ma_decoder_uninit(&decoder);
        open = false;
    }
};

AudioDecoder::AudioDecoder()
    : d(std::make_unique<Impl>())
{
}

AudioDecoder::~AudioDecoder()
{
    close();
}

bool AudioDecoder::canDecode(const QString &path)
{
    static const QStringList Supported = {QStringLiteral("mp3"), QStringLiteral("flac"), QStringLiteral("wav"),
                                          QStringLiteral("ogg")};
    return Supported.contains(QFileInfo(path).suffix().toLower());
}

bool AudioDecoder::open(std::unique_ptr<QIODevice> device, int maxChannels, int sampleRate)
{
    close();
    if (!device || !device->isOpen())
        return false;
    d->device = std::move(device);

    // Probe the native format first, then reopen with the output format.
    if (!d->init(0, 0)) {
        close();
        return false;
    }
    ma_format format;
    ma_uint32 channels = 0;
    ma_uint32 rate = 0;
    ma_decoder_get_data_format(&d->decoder, &format, &channels, &rate, nullptr, 0);
    m_sourceChannels = int(channels);
    m_sourceSampleRate = int(rate);

    const int wantChannels = (maxChannels > 0 && int(channels) > maxChannels) ? maxChannels : int(channels);
    const int wantRate = sampleRate > 0 ? sampleRate : int(rate);
    if (wantChannels != int(channels) || wantRate != int(rate)) {
        d->uninit();
        if (!d->init(wantChannels, wantRate)) {
            close();
            return false;
        }
    }
    m_channels = wantChannels;
    m_sampleRate = wantRate;

    ma_uint64 length = 0;
    m_lengthFrames = ma_decoder_get_length_in_pcm_frames(&d->decoder, &length) == MA_SUCCESS && length > 0
        ? qint64(length)
        : -1;
    m_position = 0;
    m_ended = false;
    return true;
}

void AudioDecoder::close()
{
    d->uninit();
    d->device.reset();
    m_channels = m_sampleRate = m_sourceChannels = m_sourceSampleRate = 0;
    m_lengthFrames = -1;
    m_position = 0;
    m_ended = false;
}

bool AudioDecoder::isOpen() const
{
    return d->open;
}

qint64 AudioDecoder::read(float *out, qint64 frames)
{
    if (!d->open || frames <= 0)
        return 0;
    ma_uint64 read = 0;
    ma_decoder_read_pcm_frames(&d->decoder, out, ma_uint64(frames), &read);
    m_position += qint64(read);
    if (qint64(read) < frames)
        m_ended = true;
    return qint64(read);
}

bool AudioDecoder::seek(qint64 frame)
{
    if (!d->open)
        return false;
    frame = qMax<qint64>(0, m_lengthFrames > 0 ? qMin(frame, m_lengthFrames) : frame);
    if (ma_decoder_seek_to_pcm_frame(&d->decoder, ma_uint64(frame)) != MA_SUCCESS)
        return false;
    m_position = frame;
    m_ended = false;
    return true;
}

} // namespace newamp

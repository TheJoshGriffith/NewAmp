#include "audio/DspBackend.h"

#include "audio/AudioDecoder.h"
#include "audio/DspRack.h"
#include "audio/QtDecoderSource.h"
#include "audio/TagReader.h"

#include <QAudioDevice>
#include <QAudioSink>
#include <QBuffer>
#include <QFile>
#include <QFileInfo>
#include <QMediaDevices>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QTimer>

#include <algorithm>
#include <cstring>
#include <utility>

namespace newamp {

namespace {

// A file name for a downloaded track whose suffix picks the decoder.
QString downloadName(const QUrl &url, const QString &contentType)
{
    const QString name = url.fileName();
    if (AudioDecoder::canDecode(name))
        return name;
    static const std::pair<const char *, const char *> Types[] = {
        {"audio/flac", "flac"}, {"audio/x-flac", "flac"}, {"audio/mpeg", "mp3"}, {"audio/mp3", "mp3"},
        {"audio/wav", "wav"},   {"audio/x-wav", "wav"},   {"audio/wave", "wav"}, {"audio/ogg", "ogg"},
        {"application/ogg", "ogg"},
    };
    const QString type = contentType.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
    for (const auto &[mime, suffix] : Types) {
        if (type == QLatin1String(mime))
            return QStringLiteral("track.") + QLatin1String(suffix);
    }
    return name;
}


// Why a download failed, in words that never repeat the URL (it carries a token).
QString downloadFailure(QNetworkReply *reply)
{
    const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    if (status > 0) {
        return DspBackend::tr("the server replied %1 %2")
            .arg(status)
            .arg(reply->attribute(QNetworkRequest::HttpReasonPhraseAttribute).toString())
            .trimmed();
    }
    if (reply->error() == QNetworkReply::OperationCanceledError)
        return DspBackend::tr("the server stopped sending data");
    return reply->errorString();
}

constexpr int VisRingSize = 4096;
constexpr int BufferMs = 150;
constexpr int DownloadTimeoutMs = 30000; // with no data arriving

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
    cancelDownload();
    if (m_sink)
        m_sink->stop();
}

bool DspBackend::canPlay(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (!url.isLocalFile() && scheme != QLatin1String("http") && scheme != QLatin1String("https"))
        return false;
    // HLS and DASH playlists describe live streams, not files to fetch whole.
    const QString suffix = QFileInfo(url.path()).suffix().toLower();
    return suffix != QLatin1String("m3u8") && suffix != QLatin1String("mpd");
}

bool DspBackend::load(const QUrl &url)
{
    if (!canPlay(url))
        return false;
    if (m_decoding) {
        // Decoding can run an event loop, which may ask for another track;
        // that one replaces the track being decoded once decoding returns.
        m_deferred = url;
        return true;
    }
    stop();
    cancelDownload();
    m_playWhenReady = false;
    // Free the previous track before decoding the next one.
    {
        std::lock_guard lock(m_mutex);
        m_source.reset();
    }
    m_sink.reset();

    if (!url.isLocalFile()) {
        // Remote tracks (Plex) are fetched whole, then decoded like a file.
        startDownload(url);
        return true;
    }

    const QString path = url.toLocalFile();
    Tags tags;
    qint64 size = 0;
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly))
            return false;
        tags = TagReader::read(file);
        size = file.size();
    }
    auto openFile = [path]() -> std::unique_ptr<QIODevice> {
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly))
            return nullptr;
        return file;
    };
    auto source = decodeOnce(path, openFile, true);
    if (m_deferred)
        return loadDeferred();
    return install(std::move(source), tags, size);
}

std::unique_ptr<PcmSource> DspBackend::decodeOnce(const QString &name, const DeviceFactory &openDevice, bool isFile)
{
    m_decoding = true;
    auto source = decode(name, openDevice, isFile);
    m_decoding = false;
    return source;
}

bool DspBackend::loadDeferred()
{
    const QUrl url = *std::exchange(m_deferred, std::nullopt);
    const bool play = m_playWhenReady;
    if (!load(url)) {
        Q_EMIT cannotDecode(url, play);
        return true; // this backend is still the one that answered
    }
    if (play)
        this->play();
    return true;
}

std::unique_ptr<PcmSource> DspBackend::decode(const QString &name, const DeviceFactory &openDevice, bool isFile)
{
    // miniaudio first; then whatever the platform's decoder handles.
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
    if (AudioDecoder::canDecode(name)) {
        auto decoder = std::make_unique<AudioDecoder>();
        auto open = [&](int maxChannels, int sampleRate) {
            auto input = openDevice();
            return input && decoder->open(std::move(input), maxChannels, sampleRate);
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
            return decoder;
    }

    // Qt can't always decode to the file's own format (GStreamer in
    // Qt 6.4 fails to), so ask for what the device prefers.
    const QAudioFormat preferred = device.preferredFormat();
    const int channels = preferred.channelCount() > 0 ? qMin(2, preferred.channelCount()) : 2;
    const int sampleRate = preferred.sampleRate() > 0 ? preferred.sampleRate() : 44100;
    auto decoder = std::make_unique<QtDecoderSource>();
    const bool ok = isFile ? decoder->open(name, channels, sampleRate)
                           : decoder->open(openDevice(), channels, sampleRate);
    if (!ok)
        return nullptr;
    // Queued: the source may report from inside a call we make under m_mutex.
    connect(decoder.get(), &QtDecoderSource::lengthChanged, this, &DspBackend::emitDuration, Qt::QueuedConnection);
    return decoder;
}

bool DspBackend::install(std::unique_ptr<PcmSource> source, const Tags &tags, qint64 size)
{
    if (!source)
        return false;
    const QAudioDevice device = QMediaDevices::defaultAudioOutput();
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
        if (m_rack)
            m_rack->reset();
    }
    m_fileSize = size;

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

void DspBackend::startDownload(const QUrl &url)
{
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setTransferTimeout(DownloadTimeoutMs);
    m_download = m_network->get(request);
    QNetworkReply *reply = m_download;
    connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
        reply->deleteLater();
        if (reply != m_download)
            return; // cancelled
        m_download = nullptr;
        if (reply->error() != QNetworkReply::NoError) {
            m_playWhenReady = false;
            qWarning("NewAmp: download failed (%d): %s", int(reply->error()),
                     qPrintable(downloadFailure(reply)));
            Q_EMIT errorOccurred(tr("Could not load %1: %2").arg(url.fileName(), downloadFailure(reply)));
            return;
        }
        // QBuffer shares the bytes, so each decoder attempt costs no copy.
        const QByteArray data = reply->readAll();
        auto openData = [data]() -> std::unique_ptr<QIODevice> {
            auto buffer = std::make_unique<QBuffer>();
            buffer->setData(data);
            buffer->open(QIODevice::ReadOnly);
            return buffer;
        };
        Tags tags;
        if (auto input = openData())
            tags = TagReader::read(*input);
        // Plex part URLs end in the file name, so the suffix picks the
        // decoder; other servers (DLNA) may only say it in the content type.
        auto source = decodeOnce(downloadName(url, reply->header(QNetworkRequest::ContentTypeHeader).toString()),
                                 openData, false);
        if (m_deferred) {
            loadDeferred();
            return;
        }
        if (!install(std::move(source), tags, data.size())) {
            const bool play = std::exchange(m_playWhenReady, false);
            Q_EMIT cannotDecode(url, play);
            return;
        }
        if (std::exchange(m_playWhenReady, false))
            play();
    });
}

void DspBackend::cancelDownload()
{
    if (QNetworkReply *reply = std::exchange(m_download, nullptr))
        reply->abort(); // finished() fires, sees it's no longer current, and frees it
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
    if (m_download || m_decoding) {
        m_playWhenReady = true; // starts once the track is ready
        return;
    }
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
            if (m_rack)
                m_rack->reset();
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
    m_playWhenReady = false;
    if (m_state != State::Playing || !m_sink)
        return;
    m_sink->suspend();
    m_positionTimer->stop();
    setState(State::Paused);
}

void DspBackend::stop()
{
    m_playWhenReady = false;
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
        if (m_rack)
            m_rack->reset();
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
        if (m_rack && got > 0)
            m_rack->process(m_scratch.data(), got, m_format.sampleRate(), channels);
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

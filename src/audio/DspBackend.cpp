#include "audio/DspBackend.h"

#include "audio/AudioDecoder.h"
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
// How long before the end of a track the next one is fetched and decoded.
constexpr qint64 PreloadLeadMs = 30000;

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
    connect(m_positionTimer, &QTimer::timeout, this, [this] {
        checkTransition();
        preloadIfDue();
        Q_EMIT positionChanged(position());
    });
}

DspBackend::~DspBackend()
{
    cancelDownload();
    clearNext();
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
    {
        // A new track replaces one that was taking over, without announcing it.
        std::lock_guard lock(m_mutex);
        m_mixer.clearTransition();
    }
    stop();
    cancelDownload();
    clearNext();
    m_playWhenReady = false;
    // Free the previous track before decoding the next one.
    {
        std::lock_guard lock(m_mutex);
        m_mixer.setCurrent(nullptr, 1.0f);
    }
    freeRetired();
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

std::unique_ptr<PcmSource> DspBackend::decodeOnce(const QString &name, const DeviceFactory &openDevice, bool isFile,
                                                  const QAudioFormat &target)
{
    m_decoding = true;
    auto source = decode(name, openDevice, isFile, target);
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

std::unique_ptr<PcmSource> DspBackend::decode(const QString &name, const DeviceFactory &openDevice, bool isFile,
                                              const QAudioFormat &target)
{
    // miniaudio first; then whatever the platform's decoder handles.
    const QAudioDevice device = outputDevice();
    const bool exact = target.isValid();
    if (AudioDecoder::canDecode(name)) {
        auto decoder = std::make_unique<AudioDecoder>();
        auto open = [&](int maxChannels, int sampleRate) {
            auto input = openDevice();
            return input && decoder->open(std::move(input), maxChannels, sampleRate, exact);
        };
        // Ask for the file's own format; if the output device can't take it,
        // let the decoder resample and downmix to what the device prefers.
        bool ok = exact ? open(target.channelCount(), target.sampleRate()) : open(0, 0);
        if (ok && !exact && !device.isNull()
            && !device.isFormatSupported(floatFormat(decoder->sampleRate(), decoder->channels()))) {
            const QAudioFormat preferred = device.preferredFormat();
            ok = open(qMin(2, preferred.channelCount()), preferred.sampleRate());
        }
        if (ok)
            return decoder;
    }

    // Qt can't always decode to the file's own format (GStreamer in
    // Qt 6.4 fails to), so ask for what the device prefers.
    const QAudioFormat preferred = exact ? target : device.preferredFormat();
    const int channels = preferred.channelCount() > 0 ? qMin(2, preferred.channelCount()) : 2;
    const int sampleRate = preferred.sampleRate() > 0 ? preferred.sampleRate() : 44100;
    auto decoder = std::make_unique<QtDecoderSource>();
    const bool ok = isFile ? decoder->open(name, channels, sampleRate)
                           : decoder->open(openDevice(), channels, sampleRate);
    // A next track must match the sink it joins.
    if (!ok || (exact && (decoder->channels() != channels || decoder->sampleRate() != sampleRate)))
        return nullptr;
    // Queued: the source may report from inside a call we make under m_mutex.
    connect(decoder.get(), &QtDecoderSource::lengthChanged, this, &DspBackend::emitDuration, Qt::QueuedConnection);
    return decoder;
}

bool DspBackend::install(std::unique_ptr<PcmSource> source, const Tags &tags, qint64 size)
{
    if (!source)
        return false;
    const QAudioDevice device = outputDevice();
    QAudioFormat format = floatFormat(source->sampleRate(), source->channels());
    if (!device.isNull() && !device.isFormatSupported(format))
        format.setSampleFormat(QAudioFormat::Int16);
    m_info = {tags, size, source->sourceSampleRate(), source->sourceChannels()};
    {
        std::lock_guard lock(m_mutex);
        m_format = format;
        m_mixer.configure(format.channelCount());
        m_mixer.setCurrent(std::move(source), gainFor(tags));
        m_mixer.setCrossfadeFrames(qint64(m_options.crossfadeMs) * format.sampleRate() / 1000);
        m_chain.configure(m_format.sampleRate(), m_format.channelCount());
        m_chain.setParams(m_params);
    }
    freeRetired();
    createSink();
    m_atEnd = false;

    emitDuration();
    Q_EMIT metaDataChanged(tags.title, tags.artist, tags.album);
    Q_EMIT formatChanged(m_info.sourceRate, m_info.sourceChannels);
    return true;
}

void DspBackend::createSink()
{
    m_sink = std::make_unique<QAudioSink>(outputDevice(), m_format);
    m_sink->setBufferSize(m_format.bytesForDuration(BufferMs * 1000));
    connect(m_sink.get(), &QAudioSink::stateChanged, this, [this](QAudio::State state) {
        // The stream returns no data once the source is done; the sink then
        // drains and goes idle, which is the real end of the track.
        if (state == QAudio::IdleState && m_atEnd && m_state == State::Playing) {
            stop();
            Q_EMIT endOfTrack();
        }
    });
}

QAudioDevice DspBackend::outputDevice() const
{
    if (!m_options.outputDevice.isEmpty()) {
        const QList<QAudioDevice> devices = QMediaDevices::audioOutputs();
        for (const QAudioDevice &device : devices) {
            if (device.id() == m_options.outputDevice)
                return device;
        }
    }
    // Unplugged, or never chosen: follow the system.
    return QMediaDevices::defaultAudioOutput();
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
    Q_EMIT bitrateChanged(durationMs > 0 ? int(m_info.size * 8 / durationMs) : 0);
}

void DspBackend::startSink()
{
    checkTransition(true);
    {
        std::lock_guard lock(m_mutex);
        m_mixer.resetClock();
    }
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
            if (PcmSource *source = m_mixer.current())
                source->seek(0);
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
    checkTransition(true);
    if (m_sink)
        m_sink->stop();
    m_positionTimer->stop();
    {
        std::lock_guard lock(m_mutex);
        m_mixer.stopFade();
        if (PcmSource *source = m_mixer.current())
            source->release();
    }
    freeRetired();
    setState(State::Stopped);
}

void DspBackend::seek(qint64 positionMs)
{
    // Play always starts from the top, and a stopped source may have
    // released its audio, so there's nothing to seek in.
    if (m_state == State::Stopped)
        return;
    // A track that has just taken over is the one being seeked in.
    checkTransition(true);
    qint64 reachedMs = 0;
    {
        std::lock_guard lock(m_mutex);
        PcmSource *source = m_mixer.current();
        if (!source)
            return;
        m_mixer.stopFade();
        const int rate = source->sampleRate();
        source->seek(qMax<qint64>(0, positionMs) * rate / 1000);
        m_chain.reset();
        // A source may stop short of the target (QtDecoderSource only
        // reaches what it has decoded so far).
        reachedMs = source->positionFrames() * 1000 / rate;
    }
    freeRetired();
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
    const PcmSource *source = m_mixer.current();
    if (!source || source->lengthFrames() < 0 || source->sampleRate() <= 0)
        return -1;
    return source->lengthFrames() * 1000 / source->sampleRate();
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
        // The mixer plays silence while a source is still decoding, and
        // moves on to the next track by itself.
        got = m_mixer.render(m_scratch.data(), frames);
        if (got < frames)
            m_atEnd = true;
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

void DspBackend::setPlaybackOptions(const PlaybackOptions &options)
{
    const bool deviceChanged = options.outputDevice != m_options.outputDevice;
    m_options = options;
    {
        std::lock_guard lock(m_mutex);
        m_mixer.setCrossfadeFrames(qint64(options.crossfadeMs) * m_format.sampleRate() / 1000);
        m_mixer.setCurrentGain(gainFor(m_info.tags));
        m_mixer.setNextGain(gainFor(m_nextInfo.tags));
    }
    if (!options.transitions() && m_nextState != NextState::None) {
        const QUrl next = m_nextUrl;
        clearNext();
        m_nextUrl = next; // kept, in case they come back on
    } else if (options.transitions() && m_nextState == NextState::None && !m_nextUrl.isEmpty()) {
        m_nextState = NextState::Waiting;
    }

    if (deviceChanged && m_sink) {
        // Move to the new device where playback is now.
        const State state = m_state;
        const qint64 at = position();
        checkTransition(true);
        m_sink->stop();
        createSink();
        if (state != State::Stopped) {
            {
                std::lock_guard lock(m_mutex);
                if (PcmSource *source = m_mixer.current())
                    source->seek(at * source->sampleRate() / 1000);
                m_mixer.stopFade();
                m_chain.reset();
            }
            m_atEnd = false;
            startSink();
            markPosition(at);
            if (state == State::Paused)
                m_sink->suspend();
        }
    }
}

float DspBackend::gainFor(const Tags &tags) const
{
    return replayGainFactor(tags.replayGain, m_options);
}

void DspBackend::setNext(const QUrl &url)
{
    if (url == m_nextUrl)
        return;
    clearNext();
    m_nextUrl = canPlay(url) ? url : QUrl();
    // The position timer starts it when it's due.
    if (!m_nextUrl.isEmpty() && m_options.transitions())
        m_nextState = NextState::Waiting;
}

void DspBackend::clearNext()
{
    if (QNetworkReply *reply = std::exchange(m_nextDownload, nullptr))
        reply->abort();
    {
        std::lock_guard lock(m_mutex);
        m_mixer.clearNext();
    }
    freeRetired();
    m_nextUrl.clear();
    m_nextInfo = {};
    m_nextState = NextState::None;
}

void DspBackend::preloadIfDue()
{
    if (m_nextState != NextState::Waiting || m_state != State::Playing || m_decoding || !m_sink)
        return;
    // Close enough to the end, or of unknown length: get it ready now.
    const qint64 length = duration();
    if (length > 0 && length - position() > PreloadLeadMs + m_options.crossfadeMs)
        return;
    preload();
}

void DspBackend::preload()
{
    m_nextState = NextState::Loading;
    const QUrl url = m_nextUrl;
    if (!url.isLocalFile()) {
        if (!m_network)
            m_network = new QNetworkAccessManager(this);
        QNetworkRequest request(url);
        request.setTransferTimeout(DownloadTimeoutMs);
        m_nextDownload = m_network->get(request);
        QNetworkReply *reply = m_nextDownload;
        connect(reply, &QNetworkReply::finished, this, [this, reply, url] {
            reply->deleteLater();
            if (reply != m_nextDownload)
                return; // cancelled
            m_nextDownload = nullptr;
            if (reply->error() != QNetworkReply::NoError) {
                // It gets a second chance, with the error shown, when the
                // player gets to it the ordinary way.
                m_nextState = NextState::Failed;
                return;
            }
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
            const QString name = downloadName(url, reply->header(QNetworkRequest::ContentTypeHeader).toString());
            preloaded(decodeOnce(name, openData, false, m_format), tags, data.size());
        });
        return;
    }

    const QString path = url.toLocalFile();
    Tags tags;
    qint64 size = 0;
    {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            m_nextState = NextState::Failed;
            return;
        }
        tags = TagReader::read(file);
        size = file.size();
    }
    auto openFile = [path]() -> std::unique_ptr<QIODevice> {
        auto file = std::make_unique<QFile>(path);
        if (!file->open(QIODevice::ReadOnly))
            return nullptr;
        return file;
    };
    preloaded(decodeOnce(path, openFile, true, m_format), tags, size);
}

void DspBackend::preloaded(std::unique_ptr<PcmSource> source, const Tags &tags, qint64 size)
{
    // Decoding may have run an event loop: another track may have been asked
    // for (which replaces everything), or play pressed.
    if (m_deferred) {
        loadDeferred();
        return;
    }
    if (m_nextState == NextState::Loading) {
        if (source) {
            m_nextInfo = {tags, size, source->sourceSampleRate(), source->sourceChannels()};
            std::lock_guard lock(m_mutex);
            m_mixer.setNext(std::move(source), gainFor(tags));
            m_nextState = NextState::Ready;
        } else {
            m_nextState = NextState::Failed;
        }
    }
    if (std::exchange(m_playWhenReady, false))
        play();
}

void DspBackend::checkTransition(bool now)
{
    qint64 at = -1;
    {
        std::lock_guard lock(m_mutex);
        at = m_mixer.transitionFrame();
    }
    const int rate = m_format.sampleRate();
    if (at < 0 || rate <= 0 || !m_sink)
        return;
    const qint64 atUs = at * 1000000 / rate;
    const qint64 playedUs = m_sink->processedUSecs();
    if (!now && playedUs < atUs)
        return;
    {
        std::lock_guard lock(m_mutex);
        m_mixer.clearTransition();
    }
    freeRetired();
    // The new track started playing at atUs.
    m_baseMs = 0;
    m_baseProcessedUs = qMin(atUs, playedUs);
    m_info = std::exchange(m_nextInfo, {});
    m_nextUrl.clear();
    m_nextState = NextState::None;

    Q_EMIT advancedToNext();
    emitDuration();
    Q_EMIT metaDataChanged(m_info.tags.title, m_info.tags.artist, m_info.tags.album);
    Q_EMIT formatChanged(m_info.sourceRate, m_info.sourceChannels);
}

void DspBackend::freeRetired()
{
    std::vector<std::unique_ptr<PcmSource>> retired;
    {
        std::lock_guard lock(m_mutex);
        retired = m_mixer.takeRetired();
    }
    // Destroyed here, on this thread, outside the lock.
}

void DspBackend::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

} // namespace newamp

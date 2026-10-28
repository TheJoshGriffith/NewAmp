#include "core/PlaybackEngine.h"

#include "audio/DspBackend.h"
#include "audio/IcyReader.h"
#include "audio/MediaPlayerBackend.h"
#include "audio/RemoteBackend.h"
#include "core/EqualizerSettings.h"
#include "core/LocalFileSource.h"
#include "core/MediaSource.h"
#include "core/PlaybackSettings.h"

#include <QRegularExpression>

#include <utility>

namespace {

// Stream URLs can carry access tokens in their query; error text from Qt
// and the platform often quotes the URL, so blank any token value (Plex's
// X-Plex-Token, Jellyfin's api_key).
QString redactTokens(QString message)
{
    static const QRegularExpression Token(QStringLiteral("([?&][^=&\\s]*(?:token|api_key)=)[^&\\s]+"),
                                          QRegularExpression::CaseInsensitiveOption);
    return message.replace(Token, QStringLiteral("\\1(hidden)"));
}

} // namespace

namespace newamp {

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent)
{
    auto *dsp = new DspBackend(this);
    dsp->setDspRack(&m_rack);
    m_dsp = dsp;
    m_media = new MediaPlayerBackend(this);
    m_remote = new RemoteBackend(this);
    m_icy = new IcyReader(this);
    connectBackend(m_dsp);
    connectBackend(m_media);
    connectBackend(m_remote);
    // A remote track is only known to be undecodable once it has arrived.
    connect(dsp, &DspBackend::cannotDecode, this, [this](const QUrl &url, bool play) {
        if (m_active != m_dsp)
            return;
        useMediaPlayer(url);
        if (play)
            m_active->play();
    });
    // Radio titles arrive on their own connection, between the player's tags.
    connect(m_icy, &IcyReader::metaData, this, [this](const QString &title, const QString &station) {
        if (m_active == m_media)
            Q_EMIT metaDataChanged(title.isEmpty() ? station : title, QString(), station);
    });
    applyDsp();
}

PlaybackEngine::~PlaybackEngine()
{
    // Its audio thread uses the rack, which goes before child objects do.
    delete std::exchange(m_dsp, nullptr);
}

void PlaybackEngine::connectBackend(AudioBackend *backend)
{
    // Only the active backend speaks for the engine.
    auto active = [this, backend] { return backend == m_active; };
    connect(backend, &AudioBackend::stateChanged, this, [this, active](AudioBackend::State s) {
        if (!active())
            return;
        switch (s) {
        case AudioBackend::State::Playing: setState(State::Playing); break;
        case AudioBackend::State::Paused: setState(State::Paused); break;
        case AudioBackend::State::Stopped: setState(State::Stopped); break;
        }
    });
    connect(backend, &AudioBackend::positionChanged, this, [this, active](qint64 ms) {
        if (active())
            Q_EMIT positionChanged(ms);
    });
    connect(backend, &AudioBackend::durationChanged, this, [this, active](qint64 ms) {
        if (active())
            Q_EMIT durationChanged(ms);
    });
    connect(backend, &AudioBackend::metaDataChanged, this,
            [this, active](const QString &title, const QString &artist, const QString &album) {
                if (active())
                    Q_EMIT metaDataChanged(title, artist, album);
            });
    connect(backend, &AudioBackend::bitrateChanged, this, [this, active](int kbps) {
        if (active())
            Q_EMIT bitrateChanged(kbps);
    });
    connect(backend, &AudioBackend::formatChanged, this, [this, active](int rate, int channels) {
        if (active())
            Q_EMIT formatChanged(rate, channels);
    });
    connect(backend, &AudioBackend::endOfTrack, this, [this, active] {
        if (active())
            Q_EMIT endOfTrack();
    });
    connect(backend, &AudioBackend::advancedToNext, this, [this, active] {
        // A new track being started replaces whatever was taking over.
        if (!active() || m_loading || !m_queued)
            return;
        m_fallback = std::exchange(m_queuedFallback, QUrl());
        m_firstError.clear();
        m_queued.reset();
        Q_EMIT trackAdvanced();
    });
    connect(backend, &AudioBackend::errorOccurred, this, [this, active](const QString &message) {
        if (active())
            failed(message);
    });
}

void PlaybackEngine::registerSource(MediaSource *source)
{
    m_sources.insert(source->id(), source);
}

void PlaybackEngine::play(const Track &track)
{
    // Opening some files runs an event loop until the first audio arrives;
    // a play request from in there waits until this one is set up.
    if (m_loading) {
        m_pending = track;
        return;
    }
    m_icy->stop();
    MediaSource *source = m_sources.value(track.sourceId);
    const QUrl url = source ? source->streamUrl(track) : QUrl();
    RemotePlayer *remotePlayer = source ? source->remotePlayer() : nullptr;
    if (url.isEmpty()) {
        // Its plugin isn't enabled, or it's signed out or can't reach its
        // server: say so, and let the playlist move on.
        if (m_active)
            m_active->stop();
        Q_EMIT errorOccurred(source ? tr("%1 is not available").arg(source->displayName())
                                    : tr("The %1 source is not available; is its plugin enabled?").arg(track.sourceId));
        return;
    }
    m_remotePlayer = remotePlayer;
    m_fallback = source->fallbackUrl(track);
    m_firstError.clear();
    // Web addresses the user opened are usually radio: endless, so never
    // fetched whole, and with titles in the stream.
    m_radio = track.sourceId == QLatin1String(LocalFileSource::Id) && !url.isLocalFile();

    m_loading = true;
    if (m_active)
        m_active->stop();
    start(url);
    m_loading = false;

    if (m_pending) {
        const Track next = *std::exchange(m_pending, std::nullopt);
        play(next);
    }
}

void PlaybackEngine::queueNext(const Track &track)
{
    if (m_queued && m_queued->sourceId == track.sourceId && m_queued->location == track.location)
        return;
    m_queued = track;
    MediaSource *source = m_sources.value(track.sourceId);
    // Only NewAmp's own pipeline joins tracks; a remote player plays its own.
    if (!m_active || !source || source->remotePlayer()) {
        m_queuedFallback.clear();
        if (m_active)
            m_active->setNext({});
        return;
    }
    m_queuedFallback = source->fallbackUrl(track);
    m_active->setNext(source->streamUrl(track));
}

void PlaybackEngine::clearNext()
{
    m_queued.reset();
    m_queuedFallback.clear();
    if (m_active)
        m_active->setNext({});
}

void PlaybackEngine::start(const QUrl &url)
{
    // Loading forgets the next track; the controller queues it again.
    m_queued.reset();
    m_queuedFallback.clear();
    // Sources NewAmp can't decode play through their own player.
    if (m_remotePlayer) {
        m_remote->setPlayer(m_remotePlayer);
        m_active = m_remote;
        Q_EMIT formatChanged(0, 0);
        Q_EMIT bitrateChanged(0);
        m_remote->load(url);
        applyDsp();
        m_active->play();
        return;
    }
    if (m_radio) {
        useMediaPlayer(url);
        m_icy->start(url);
        m_active->play();
        return;
    }
    // Prefer our own pipeline; fall back to Qt Multimedia for the rest.
    m_active = m_dsp;
    if (!m_dsp->load(url))
        useMediaPlayer(url);
    applyDsp();
    m_active->play();
}

void PlaybackEngine::failed(const QString &message)
{
    // One retry through the source's fallback, unless a load is in progress
    // (that load has replaced the track that failed).
    if (!m_fallback.isEmpty() && !m_loading) {
        m_firstError = redactTokens(message);
        qWarning("NewAmp: %s; trying the source's fallback", qPrintable(m_firstError));
        m_active->stop();
        m_loading = true;
        start(std::exchange(m_fallback, QUrl()));
        m_loading = false;
        if (m_pending)
            play(*std::exchange(m_pending, std::nullopt));
        return;
    }
    const QString text = redactTokens(message);
    Q_EMIT errorOccurred(m_firstError.isEmpty() ? text
                                                : tr("%1, and the fallback failed too: %2").arg(m_firstError, text));
}

void PlaybackEngine::useMediaPlayer(const QUrl &url)
{
    m_active = m_media;
    Q_EMIT formatChanged(0, 0);
    m_media->load(url);
    applyDsp();
}

void PlaybackEngine::resume()
{
    if (m_active)
        m_active->play();
}

void PlaybackEngine::pause()
{
    if (m_active)
        m_active->pause();
}

void PlaybackEngine::stop()
{
    m_icy->stop();
    if (m_active)
        m_active->stop();
}

void PlaybackEngine::seek(qint64 positionMs)
{
    if (m_active)
        m_active->seek(positionMs);
}

qint64 PlaybackEngine::position() const
{
    return m_active ? m_active->position() : 0;
}

qint64 PlaybackEngine::duration() const
{
    return m_active ? m_active->duration() : -1;
}

void PlaybackEngine::setVolume(float volume)
{
    volume = qBound(0.0f, volume, 1.0f);
    if (qFuzzyCompare(volume + 1.0f, m_volume + 1.0f))
        return;
    m_volume = volume;
    applyDsp();
    Q_EMIT volumeChanged(m_volume);
}

void PlaybackEngine::setBalance(double balance)
{
    balance = qBound(-1.0, balance, 1.0);
    if (balance == m_balance)
        return;
    m_balance = balance;
    applyDsp();
    Q_EMIT balanceChanged(m_balance);
}

void PlaybackEngine::setEqualizer(EqualizerSettings *equalizer)
{
    if (m_equalizer)
        disconnect(m_equalizer, nullptr, this, nullptr);
    m_equalizer = equalizer;
    if (m_equalizer)
        connect(m_equalizer, &EqualizerSettings::changed, this, &PlaybackEngine::applyDsp);
    applyDsp();
}

void PlaybackEngine::setPlaybackSettings(PlaybackSettings *settings)
{
    if (m_playback)
        disconnect(m_playback, nullptr, this, nullptr);
    m_playback = settings;
    if (m_playback)
        connect(m_playback, &PlaybackSettings::changed, this, &PlaybackEngine::applyPlayback);
    applyPlayback();
}

void PlaybackEngine::applyPlayback()
{
    const PlaybackOptions options = m_playback ? m_playback->options() : PlaybackOptions();
    m_dsp->setPlaybackOptions(options);
    m_media->setPlaybackOptions(options);
    m_remote->setPlaybackOptions(options);
}

void PlaybackEngine::applyDsp()
{
    DspParams params;
    params.volume = m_volume;
    params.balance = m_balance;
    if (m_equalizer) {
        params.eqEnabled = m_equalizer->isEnabled();
        params.preampDb = m_equalizer->preamp();
        for (int i = 0; i < DspParams::BandCount; ++i)
            params.bandsDb[i] = m_equalizer->band(i);
    }
    m_dsp->setDspParams(params);
    m_media->setDspParams(params);
    m_remote->setDspParams(params);
}

bool PlaybackEngine::effectsAvailable() const
{
    return m_active && m_active->supportsEffects();
}

int PlaybackEngine::visualizerSamples(float *out, int count) const
{
    return m_active ? m_active->visualizerSamples(out, count) : 0;
}

void PlaybackEngine::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

} // namespace newamp

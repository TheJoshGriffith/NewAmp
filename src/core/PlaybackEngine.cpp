#include "core/PlaybackEngine.h"

#include "audio/DspBackend.h"
#include "audio/MediaPlayerBackend.h"
#include "core/EqualizerSettings.h"
#include "core/MediaSource.h"

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
    m_dsp = dsp;
    m_media = new MediaPlayerBackend(this);
    connectBackend(m_dsp);
    connectBackend(m_media);
    // A remote track is only known to be undecodable once it has arrived.
    connect(dsp, &DspBackend::cannotDecode, this, [this](const QUrl &url, bool play) {
        if (m_active != m_dsp)
            return;
        useMediaPlayer(url);
        if (play)
            m_active->play();
    });
    applyDsp();
}

PlaybackEngine::~PlaybackEngine() = default;

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
    MediaSource *source = m_sources.value(track.sourceId);
    if (!source) {
        Q_EMIT errorOccurred(tr("No source registered for \"%1\"").arg(track.sourceId));
        return;
    }
    const QUrl url = source->streamUrl(track);
    m_fallback = source->fallbackUrl(track);
    m_firstError.clear();

    // Opening some files runs an event loop until the first audio arrives;
    // a play request from in there waits until this one is set up.
    if (m_loading) {
        m_pending = track;
        return;
    }
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

void PlaybackEngine::start(const QUrl &url)
{
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
    m_balance = qBound(-1.0, balance, 1.0);
    applyDsp();
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

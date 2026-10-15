#include "core/PlaybackEngine.h"

#include "audio/DspBackend.h"
#include "audio/MediaPlayerBackend.h"
#include "core/EqualizerSettings.h"
#include "core/MediaSource.h"

namespace newamp {

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent)
    , m_dsp(new DspBackend(this))
    , m_media(new MediaPlayerBackend(this))
{
    connectBackend(m_dsp);
    connectBackend(m_media);
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
            Q_EMIT errorOccurred(message);
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

    if (m_active)
        m_active->stop();
    // Prefer our own pipeline; fall back to Qt Multimedia for the rest.
    m_active = m_dsp;
    if (!m_dsp->load(url)) {
        m_active = m_media;
        Q_EMIT formatChanged(0, 0);
        m_media->load(url);
    }
    applyDsp();
    m_active->play();
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
    m_volume = qBound(0.0f, volume, 1.0f);
    applyDsp();
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

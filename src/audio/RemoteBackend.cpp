#include "audio/RemoteBackend.h"

namespace newamp {

class RemoteBackend::Relay final : public RemotePlayer::Events
{
public:
    explicit Relay(RemoteBackend &backend)
        : m_backend(backend)
    {
    }

    void stateChanged(RemotePlayer::State state) override
    {
        switch (state) {
        case RemotePlayer::State::Playing: Q_EMIT m_backend.stateChanged(State::Playing); break;
        case RemotePlayer::State::Paused: Q_EMIT m_backend.stateChanged(State::Paused); break;
        case RemotePlayer::State::Stopped: Q_EMIT m_backend.stateChanged(State::Stopped); break;
        }
    }
    void positionChanged(qint64 positionMs) override { Q_EMIT m_backend.positionChanged(positionMs); }
    void durationChanged(qint64 durationMs) override { Q_EMIT m_backend.durationChanged(durationMs); }
    void metaDataChanged(const QString &title, const QString &artist, const QString &album) override
    {
        Q_EMIT m_backend.metaDataChanged(title, artist, album);
    }
    void endOfTrack() override { Q_EMIT m_backend.endOfTrack(); }
    void errorOccurred(const QString &message) override { Q_EMIT m_backend.errorOccurred(message); }

private:
    RemoteBackend &m_backend;
};

RemoteBackend::RemoteBackend(QObject *parent)
    : AudioBackend(parent)
    , m_relay(std::make_unique<Relay>(*this))
{
}

RemoteBackend::~RemoteBackend()
{
    setPlayer(nullptr);
}

void RemoteBackend::setPlayer(RemotePlayer *player)
{
    if (player == m_player)
        return;
    if (m_player)
        m_player->setEvents(nullptr);
    m_player = player;
    m_volume = -1.0f;
    if (m_player)
        m_player->setEvents(m_relay.get());
}

bool RemoteBackend::load(const QUrl &url)
{
    if (!m_player)
        return false;
    m_player->load(url);
    return true;
}

void RemoteBackend::play()
{
    if (m_player)
        m_player->play();
}

void RemoteBackend::pause()
{
    if (m_player)
        m_player->pause();
}

void RemoteBackend::stop()
{
    if (m_player)
        m_player->stop();
}

void RemoteBackend::seek(qint64 positionMs)
{
    if (m_player)
        m_player->seek(positionMs);
}

qint64 RemoteBackend::position() const
{
    return m_player ? m_player->position() : 0;
}

qint64 RemoteBackend::duration() const
{
    return m_player ? m_player->duration() : -1;
}

void RemoteBackend::setDspParams(const DspParams &params)
{
    const float volume = float(params.volume);
    if (!m_player || qFuzzyCompare(volume + 1.0f, m_volume + 1.0f))
        return;
    m_volume = volume;
    m_player->setVolume(volume);
}

} // namespace newamp

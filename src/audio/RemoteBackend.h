#pragma once

#include "audio/AudioBackend.h"
#include "core/RemotePlayer.h"

#include <memory>

namespace newamp {

// Plays through a source's RemotePlayer, such as Spotify Connect, and turns
// its events into backend signals. Only volume applies.
class RemoteBackend final : public AudioBackend
{
    Q_OBJECT

public:
    explicit RemoteBackend(QObject *parent = nullptr);
    ~RemoteBackend() override;

    // The player for the next load(); not owned. Detaches the previous one.
    void setPlayer(RemotePlayer *player);

    bool load(const QUrl &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    qint64 position() const override;
    qint64 duration() const override;
    void setDspParams(const DspParams &params) override;

private:
    class Relay; // the player's events, re-emitted as this backend's signals

    std::unique_ptr<Relay> m_relay;
    RemotePlayer *m_player = nullptr;
    float m_volume = -1.0f; // last volume sent, so the player isn't flooded
};

} // namespace newamp

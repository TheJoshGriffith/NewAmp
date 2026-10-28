#include "platform/MediaControls.h"

#include "core/PlayerController.h"
#include "core/Playlist.h"

namespace newamp {

MediaControls::MediaControls(PlayerController &controller, const std::function<void()> &raise, QObject *parent)
    : MediaControls(controller, MediaControlsBackend::create(controller, raise), parent)
{
}

MediaControls::MediaControls(PlayerController &controller, std::unique_ptr<MediaControlsBackend> backend,
                             QObject *parent)
    : QObject(parent)
    , m_controller(controller)
    , m_backend(std::move(backend))
{
    if (!m_backend)
        return;
    PlaybackEngine &engine = m_controller.engine();
    Playlist &playlist = m_controller.playlist();
    connect(&engine, &PlaybackEngine::stateChanged, this, &MediaControls::push);
    connect(&engine, &PlaybackEngine::durationChanged, this, &MediaControls::push);
    connect(&engine, &PlaybackEngine::positionChanged, this, &MediaControls::positionChanged);
    connect(&playlist, &Playlist::currentIndexChanged, this, &MediaControls::push);
    connect(&playlist, &Playlist::tracksChanged, this, &MediaControls::push);
    connect(&playlist, &Playlist::modesChanged, this, &MediaControls::push);
    push();
}

MediaControls::~MediaControls() = default;

NowPlaying MediaControls::nowPlaying() const
{
    NowPlaying now;
    PlaybackEngine &engine = m_controller.engine();
    const Playlist &playlist = m_controller.playlist();
    if (const Track *track = playlist.currentTrack()) {
        now.hasTrack = true;
        now.title = track->title.isEmpty() ? track->location.fileName() : track->title;
        now.artist = track->artist;
        now.album = track->album;
        now.durationMs = track->durationMs;
    }
    now.state = engine.state();
    if (now.state != PlaybackEngine::State::Stopped) {
        now.positionMs = engine.position();
        if (engine.duration() > 0)
            now.durationMs = engine.duration();
    }
    now.canGoNext = playlist.count() > 1 || (playlist.repeat() && !playlist.isEmpty());
    now.canGoPrevious = now.canGoNext;
    now.canSeek = now.state != PlaybackEngine::State::Stopped && now.durationMs > 0;
    return now;
}

void MediaControls::push()
{
    const NowPlaying now = nowPlaying();
    m_lastPositionMs = now.positionMs;
    m_sinceLastPosition.start();
    m_backend->update(now);
}

void MediaControls::positionChanged(qint64 positionMs)
{
    // Positions arrive several times a second while playing; the system
    // works the rest out from the last one, so only jumps are passed on.
    const bool playing = m_controller.engine().state() == PlaybackEngine::State::Playing;
    const qint64 expected = m_lastPositionMs + (playing ? m_sinceLastPosition.elapsed() : 0);
    m_lastPositionMs = positionMs;
    m_sinceLastPosition.start();
    if (qAbs(positionMs - expected) > SeekToleranceMs)
        m_backend->seeked(nowPlaying());
}

} // namespace newamp

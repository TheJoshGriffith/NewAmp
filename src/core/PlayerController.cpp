#include "core/PlayerController.h"

#include "core/PlaybackEngine.h"
#include "core/Playlist.h"

namespace newamp {

PlayerController::PlayerController(Playlist &playlist, PlaybackEngine &engine, QObject *parent)
    : QObject(parent)
    , m_playlist(playlist)
    , m_engine(engine)
{
    connect(&m_engine, &PlaybackEngine::endOfTrack, this, [this] {
        if (m_playlist.next())
            playCurrent();
    });

    // Fill in tags as the backend reads them, so every view shows real names.
    connect(&m_engine, &PlaybackEngine::metaDataChanged, this,
            [this](const QString &title, const QString &artist, const QString &album) {
                const Track *current = m_playlist.currentTrack();
                if (!current)
                    return;
                Track updated = *current;
                if (!title.isEmpty())
                    updated.title = title;
                if (!artist.isEmpty())
                    updated.artist = artist;
                if (!album.isEmpty())
                    updated.album = album;
                m_playlist.updateTrack(m_playlist.currentIndex(), updated);
            });
}

void PlayerController::play()
{
    switch (m_engine.state()) {
    case PlaybackEngine::State::Paused:
        m_engine.resume();
        break;
    case PlaybackEngine::State::Playing:
        // Classic behaviour: play while playing restarts the track.
        m_engine.seek(0);
        break;
    case PlaybackEngine::State::Stopped:
        if (m_playlist.currentIndex() < 0 && !m_playlist.isEmpty())
            m_playlist.setCurrentIndex(0);
        playCurrent();
        break;
    }
}

void PlayerController::pause()
{
    if (m_engine.state() == PlaybackEngine::State::Playing)
        m_engine.pause();
    else if (m_engine.state() == PlaybackEngine::State::Paused)
        m_engine.resume();
}

void PlayerController::stop()
{
    m_engine.stop();
}

void PlayerController::next()
{
    if (m_playlist.next() && m_engine.state() != PlaybackEngine::State::Stopped)
        playCurrent();
}

void PlayerController::previous()
{
    if (m_playlist.previous() && m_engine.state() != PlaybackEngine::State::Stopped)
        playCurrent();
}

void PlayerController::playIndex(int index)
{
    m_playlist.setCurrentIndex(index);
    playCurrent();
}

void PlayerController::openTracks(const QList<Track> &tracks)
{
    const int first = m_playlist.append(tracks);
    if (first >= 0)
        playIndex(first);
}

void PlayerController::playCurrent()
{
    if (const Track *track = m_playlist.currentTrack())
        m_engine.play(*track);
}

} // namespace newamp

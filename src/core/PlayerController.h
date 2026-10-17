#pragma once

#include <QList>
#include <QObject>

namespace newamp {

class PlaybackEngine;
class Playlist;
struct Track;

// Transport logic shared by every window: what "play", "next" and friends
// mean given the playlist and the engine. Windows call this, never the
// engine directly, so behaviour stays identical everywhere.
class PlayerController : public QObject
{
    Q_OBJECT

public:
    PlayerController(Playlist &playlist, PlaybackEngine &engine, QObject *parent = nullptr);

    Playlist &playlist() { return m_playlist; }
    PlaybackEngine &engine() { return m_engine; }

    void play();
    void pause();
    void stop();
    void next();
    void previous();
    void playIndex(int index);
    // Pause when playing, otherwise play: the space bar.
    void playPause();
    // Relative nudges for keys and the mouse wheel; clamped to the track
    // and to 0..1.
    void seekBy(qint64 deltaMs);
    void changeVolume(float delta);

    // Add tracks to the playlist and start playing the first one.
    void openTracks(const QList<Track> &tracks);

private:
    void playCurrent();

    Playlist &m_playlist;
    PlaybackEngine &m_engine;
};

} // namespace newamp

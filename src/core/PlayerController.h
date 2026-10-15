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

    // Add tracks to the playlist and start playing the first one.
    void openTracks(const QList<Track> &tracks);

private:
    void playCurrent();

    Playlist &m_playlist;
    PlaybackEngine &m_engine;
};

} // namespace newamp

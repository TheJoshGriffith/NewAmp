#pragma once

#include <QList>
#include <QObject>

class QNetworkAccessManager;
class QUrl;

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
    // Add a web stream, a playlist of them (.pls, .m3u, as stations publish
    // them), a local path, or another source's location (spotify:track:...),
    // and play it when play is set.
    void openUrl(const QUrl &url, bool play = true);

private:
    void playCurrent();
    // A track failed to play (or its source isn't available): go on to the next.
    void skipFailed();
    // Tells the engine what comes after the current track.
    void queueNext();

    Playlist &m_playlist;
    PlaybackEngine &m_engine;
    int m_failures = 0; // tracks that failed in a row
    int m_queuedIndex = -1;
    QNetworkAccessManager *m_network = nullptr;
};

} // namespace newamp

#pragma once

#include "core/PlaybackEngine.h"

#include <QElapsedTimer>
#include <QObject>
#include <QString>

#include <functional>
#include <memory>

namespace newamp {

class PlayerController;

// What the operating system's media controls show about the current track.
struct NowPlaying
{
    QString title;
    QString artist;
    QString album;
    qint64 durationMs = -1;
    qint64 positionMs = 0;
    PlaybackEngine::State state = PlaybackEngine::State::Stopped;
    bool hasTrack = false;
    bool canGoNext = false;
    bool canGoPrevious = false;
    bool canSeek = false;
};

// One platform's media controls. Each platform supplies create():
// MediaControls_mac.mm (Now Playing and the media keys), MediaControls_mpris.cpp
// (MPRIS on Linux), MediaControls_win.cpp (the media keys). Backends send
// commands straight to the controller.
class MediaControlsBackend
{
public:
    // Null where the platform has nothing to offer. `raise` brings the
    // player to the front, for controls that offer that.
    static std::unique_ptr<MediaControlsBackend> create(PlayerController &controller,
                                                        const std::function<void()> &raise);

    virtual ~MediaControlsBackend() = default;
    // The track, its state or what the controls can do changed.
    virtual void update(const NowPlaying &nowPlaying) = 0;
    // The position jumped (a seek) rather than moving on as it plays.
    virtual void seeked(const NowPlaying &nowPlaying) { update(nowPlaying); }
};

// Keeps the system's media controls (media keys, Now Playing, MPRIS) in step
// with the player, and lets them drive it.
class MediaControls : public QObject
{
    Q_OBJECT

public:
    MediaControls(PlayerController &controller, const std::function<void()> &raise, QObject *parent = nullptr);
    // For tests: a backend of the caller's choosing.
    MediaControls(PlayerController &controller, std::unique_ptr<MediaControlsBackend> backend,
                  QObject *parent = nullptr);
    ~MediaControls() override;

    bool isAvailable() const { return m_backend != nullptr; }
    NowPlaying nowPlaying() const;

    // How far the position may stray from where playing would have put it
    // before it counts as a seek.
    static constexpr qint64 SeekToleranceMs = 1500;

private:
    void push();
    void positionChanged(qint64 positionMs);

    PlayerController &m_controller;
    std::unique_ptr<MediaControlsBackend> m_backend;
    qint64 m_lastPositionMs = 0;
    QElapsedTimer m_sinceLastPosition;
};

} // namespace newamp

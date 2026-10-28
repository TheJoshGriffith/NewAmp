#include "platform/MediaControls.h"

#include "core/PlayerController.h"

#include <QCoreApplication>
#include <QPointer>

#import <Foundation/Foundation.h>
#import <MediaPlayer/MediaPlayer.h>

#include <vector>

// The media keys, Control Centre's Now Playing and AirPods controls all go
// through the remote command centre to whichever app last said what's
// playing, which is why the Now Playing info is kept up to date.

namespace newamp {

namespace {

class MacMediaControls final : public MediaControlsBackend
{
public:
    explicit MacMediaControls(PlayerController &controller)
        : m_controller(&controller)
    {
        MPRemoteCommandCenter *center = [MPRemoteCommandCenter sharedCommandCenter];
        add(center.togglePlayPauseCommand, [](PlayerController &c) { c.playPause(); });
        add(center.playCommand, [](PlayerController &c) {
            if (c.engine().state() != PlaybackEngine::State::Playing)
                c.play();
        });
        add(center.pauseCommand, [](PlayerController &c) {
            if (c.engine().state() == PlaybackEngine::State::Playing)
                c.engine().pause();
        });
        add(center.stopCommand, [](PlayerController &c) { c.stop(); });
        add(center.nextTrackCommand, [](PlayerController &c) { c.next(); });
        add(center.previousTrackCommand, [](PlayerController &c) { c.previous(); });

        MPRemoteCommand *position = center.changePlaybackPositionCommand;
        QPointer<PlayerController> target = m_controller;
        id token = [position addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *event) {
            const qint64 ms = qint64(((MPChangePlaybackPositionCommandEvent *)event).positionTime * 1000);
            QMetaObject::invokeMethod(qApp, [target, ms] {
                if (target)
                    target->engine().seek(ms);
            });
            return MPRemoteCommandHandlerStatusSuccess;
        }];
        m_targets.push_back({position, token});
    }

    ~MacMediaControls() override
    {
        for (const Target &target : m_targets)
            [target.command removeTarget:target.token];
        MPNowPlayingInfoCenter *info = [MPNowPlayingInfoCenter defaultCenter];
        info.nowPlayingInfo = nil;
        info.playbackState = MPNowPlayingPlaybackStateStopped;
    }

    void update(const NowPlaying &now) override
    {
        MPRemoteCommandCenter *center = [MPRemoteCommandCenter sharedCommandCenter];
        center.nextTrackCommand.enabled = now.canGoNext;
        center.previousTrackCommand.enabled = now.canGoPrevious;
        center.changePlaybackPositionCommand.enabled = now.canSeek;

        MPNowPlayingInfoCenter *info = [MPNowPlayingInfoCenter defaultCenter];
        if (!now.hasTrack) {
            info.nowPlayingInfo = nil;
            info.playbackState = MPNowPlayingPlaybackStateStopped;
            return;
        }
        NSMutableDictionary *track = [NSMutableDictionary dictionary];
        track[MPMediaItemPropertyTitle] = now.title.toNSString();
        if (!now.artist.isEmpty())
            track[MPMediaItemPropertyArtist] = now.artist.toNSString();
        if (!now.album.isEmpty())
            track[MPMediaItemPropertyAlbumTitle] = now.album.toNSString();
        if (now.durationMs > 0)
            track[MPMediaItemPropertyPlaybackDuration] = @(now.durationMs / 1000.0);
        // The system advances the elapsed time itself from the rate.
        const bool playing = now.state == PlaybackEngine::State::Playing;
        track[MPNowPlayingInfoPropertyElapsedPlaybackTime] = @(now.positionMs / 1000.0);
        track[MPNowPlayingInfoPropertyPlaybackRate] = @(playing ? 1.0 : 0.0);
        track[MPNowPlayingInfoPropertyMediaType] = @(MPNowPlayingInfoMediaTypeAudio);
        info.nowPlayingInfo = track;

        switch (now.state) {
        case PlaybackEngine::State::Playing: info.playbackState = MPNowPlayingPlaybackStatePlaying; break;
        case PlaybackEngine::State::Paused: info.playbackState = MPNowPlayingPlaybackStatePaused; break;
        case PlaybackEngine::State::Stopped: info.playbackState = MPNowPlayingPlaybackStateStopped; break;
        }
    }

private:
    struct Target
    {
        MPRemoteCommand *command;
        id token;
    };

    void add(MPRemoteCommand *command, void (*action)(PlayerController &))
    {
        QPointer<PlayerController> target = m_controller;
        id token = [command addTargetWithHandler:^MPRemoteCommandHandlerStatus(MPRemoteCommandEvent *) {
            // Handlers may run off the main thread; the player lives on it.
            QMetaObject::invokeMethod(qApp, [target, action] {
                if (target)
                    action(*target);
            });
            return MPRemoteCommandHandlerStatusSuccess;
        }];
        command.enabled = YES;
        m_targets.push_back({command, token});
    }

    QPointer<PlayerController> m_controller;
    std::vector<Target> m_targets;
};

} // namespace

std::unique_ptr<MediaControlsBackend> MediaControlsBackend::create(PlayerController &controller,
                                                                   const std::function<void()> &)
{
    return std::make_unique<MacMediaControls>(controller);
}

} // namespace newamp

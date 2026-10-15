#pragma once

#include "core/Track.h"

#include <QList>
#include <QSettings>

#include <memory>

class QWidget;

namespace newamp {

// Receives playback events from the host. Plugins implement the calls they
// care about and register with PluginHost::addPlaybackObserver().
class PlaybackObserver
{
public:
    enum class State { Stopped, Playing, Paused };

    virtual ~PlaybackObserver() = default;

    // A track began playing from the start (also after stop and play again).
    virtual void trackStarted(const Track &track) { Q_UNUSED(track) }
    virtual void playbackStateChanged(State state) { Q_UNUSED(state) }
    // durationMs is -1 while unknown.
    virtual void positionChanged(qint64 positionMs, qint64 durationMs)
    {
        Q_UNUSED(positionMs)
        Q_UNUSED(durationMs)
    }
    // The track played through to its end.
    virtual void trackFinished(const Track &track) { Q_UNUSED(track) }
};

// What the app offers to plugins. Header-only and free of QObject on
// purpose: a plugin needs no symbols from the executable to use it.
//
// The host is created before plugins are initialised and destroyed before
// they are unloaded, so plugins must not call it from their destructors.
class PluginHost
{
public:
    virtual ~PluginHost() = default;

    // Append tracks to the playlist.
    virtual void addTracks(const QList<Track> &tracks) = 0;
    // Append tracks and start playing the first of them.
    virtual void playTracks(const QList<Track> &tracks) = 0;

    // Parent for the plugin's dialogs; may be null early in startup.
    virtual QWidget *dialogParent() const = 0;

    // The app's settings, already inside a group private to this plugin.
    virtual std::unique_ptr<QSettings> settings(const QString &pluginId) const = 0;

    // The observer is not owned and must stay valid while the host exists.
    virtual void addPlaybackObserver(PlaybackObserver *observer) = 0;
};

} // namespace newamp

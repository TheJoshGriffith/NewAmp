#pragma once

#include <QString>
#include <QUrl>

namespace newamp {

// Playback that happens outside NewAmp's audio pipeline, for sources whose
// audio the app can't decode itself (Spotify's protected streams play in
// Spotify's own player). The engine drives it like any other backend; it
// gets no equalizer and no visualizer.
//
// Header-only and free of QObject, like PluginHost, so plugins need no
// symbols from the executable.
class RemotePlayer
{
public:
    enum class State { Stopped, Playing, Paused };

    // What the player tells the engine. Calls may come at any time on the
    // main thread, including from inside the player's own methods.
    class Events
    {
    public:
        virtual ~Events() = default;
        virtual void stateChanged(State state) = 0;
        virtual void positionChanged(qint64 positionMs) = 0;
        virtual void durationChanged(qint64 durationMs) = 0;
        virtual void metaDataChanged(const QString &title, const QString &artist, const QString &album) = 0;
        virtual void endOfTrack() = 0;
        virtual void errorOccurred(const QString &message) = 0;
    };

    virtual ~RemotePlayer() = default;

    // Where events go; null detaches. Set before load().
    virtual void setEvents(Events *events) = 0;

    // url is the source's streamUrl() for the track; play() starts it.
    virtual void load(const QUrl &url) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 positionMs) = 0;
    // 0 .. 1. Remote players may apply it late or round it.
    virtual void setVolume(float volume) = 0;

    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0; // -1 while unknown
};

} // namespace newamp

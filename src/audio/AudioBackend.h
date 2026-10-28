#pragma once

#include "audio/DspChain.h"
#include "audio/PlaybackOptions.h"

#include <QObject>
#include <QUrl>

namespace newamp {

// One way of turning a URL into sound. PlaybackEngine picks a backend per
// track and forwards its signals; the UI never talks to a backend directly.
class AudioBackend : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Playing, Paused };

    using QObject::QObject;

    // Prepares the URL for playback. Returns false if this backend can't play
    // it, so the engine can try another.
    virtual bool load(const QUrl &url) = 0;
    virtual void play() = 0;
    virtual void pause() = 0;
    virtual void stop() = 0;
    virtual void seek(qint64 positionMs) = 0;
    virtual qint64 position() const = 0;
    virtual qint64 duration() const = 0;

    // Volume and effects. Backends without DSP apply what they can.
    virtual void setDspParams(const DspParams &params) = 0;
    virtual bool supportsEffects() const { return false; }
    // Crossfade, gapless, ReplayGain and the output device. Backends apply
    // what they can.
    virtual void setPlaybackOptions(const PlaybackOptions &) {}

    // The track to move on to when this one ends, so a backend that can
    // prepare it ahead joins the two without a gap (or fades between them)
    // and emits advancedToNext() instead of endOfTrack(). Empty clears it;
    // load() clears it too.
    virtual void setNext(const QUrl &) {}

    // Copies the most recent mono samples for the visualizer; returns how many.
    virtual int visualizerSamples(float *, int) const { return 0; }

Q_SIGNALS:
    void stateChanged(newamp::AudioBackend::State state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void metaDataChanged(const QString &title, const QString &artist, const QString &album);
    void bitrateChanged(int kbps);
    void formatChanged(int sampleRate, int channels);
    void endOfTrack();
    // The next track took over without stopping. Signals about the new track
    // (duration, metadata, format) follow.
    void advancedToNext();
    void errorOccurred(const QString &message);
};

} // namespace newamp

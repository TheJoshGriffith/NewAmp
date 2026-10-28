#pragma once

#include "audio/PcmSource.h"

#include <memory>
#include <vector>

namespace newamp {

// Joins tracks for DspBackend: plays the current source at its own gain and,
// once a next source is queued, moves on to it with no gap, or fades it in
// over the end of the current one. All sources share one format.
// Not thread-safe: DspBackend calls it under its mutex, render() from the
// audio thread. Sources it is done with are kept until takeRetired(), so
// none is ever destroyed on the audio thread.
class TrackMixer
{
public:
    TrackMixer();

    void configure(int channels);
    int channels() const { return m_channels; }

    // Replaces everything: drops any next track and fade.
    void setCurrent(std::unique_ptr<PcmSource> source, float gain);
    PcmSource *current() const { return m_current.source.get(); }
    void setCurrentGain(float gain) { m_current.gain = gain; }

    void setNext(std::unique_ptr<PcmSource> source, float gain);
    bool hasNext() const { return bool(m_next.source); }
    void setNextGain(float gain) { m_next.gain = gain; }
    void clearNext();

    // Length of the fade into the next track, in frames; 0 joins them gaplessly.
    void setCrossfadeFrames(qint64 frames) { m_crossfadeFrames = qMax<qint64>(0, frames); }
    // Ends a fade at once, e.g. on a seek.
    void stopFade();

    // Fills up to `frames` frames. Fewer than asked means every track has ended.
    qint64 render(float *out, qint64 frames);

    // The frame, counted over render() calls since resetClock(), at which the
    // next track took over; -1 if it hasn't. Stays set until clearTransition().
    qint64 transitionFrame() const { return m_transitionFrame; }
    void clearTransition() { m_transitionFrame = -1; }
    void resetClock();

    std::vector<std::unique_ptr<PcmSource>> takeRetired();

private:
    struct Slot
    {
        std::unique_ptr<PcmSource> source;
        float gain = 1.0f;
    };

    void retire(Slot &slot);
    void advance(qint64 at);
    void mixFade(float *out, qint64 frames);

    int m_channels = 2;
    Slot m_current;
    Slot m_next;
    Slot m_fading; // the previous track, fading out
    qint64 m_crossfadeFrames = 0;
    qint64 m_fadeLength = 0;
    qint64 m_fadePosition = 0; // frames of the fade already played
    qint64 m_fadeStart = 0;    // where in the current render() call the fade began
    qint64 m_rendered = 0;
    qint64 m_transitionFrame = -1;
    std::vector<float> m_fadeScratch;
    std::vector<std::unique_ptr<PcmSource>> m_retired;
};

} // namespace newamp

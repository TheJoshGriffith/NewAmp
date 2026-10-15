#pragma once

#include <QtGlobal>

namespace newamp {

// Interleaved float PCM that DspBackend plays. Format getters describe the
// output; the source* ones describe the file before resampling or downmixing.
// read() may be called from the audio thread; everything else, and every call
// to a source that also lives on a Qt event loop, is made from the thread
// that created it. DspBackend serialises the calls with its own mutex.
class PcmSource
{
public:
    virtual ~PcmSource() = default;

    virtual int channels() const = 0;
    virtual int sampleRate() const = 0;
    virtual int sourceSampleRate() const = 0;
    virtual int sourceChannels() const = 0;
    virtual qint64 lengthFrames() const = 0; // -1 if unknown
    virtual qint64 positionFrames() const = 0;

    // Returns frames read. A short read with finished() false means the data
    // isn't ready yet, not that the track has ended.
    virtual qint64 read(float *out, qint64 frames) = 0;
    // True once read() has returned everything there is.
    virtual bool finished() const = 0;
    // A source may not reach every frame yet; it then seeks as near as it can.
    virtual bool seek(qint64 frame) = 0;
    // Frees what can be rebuilt while playback is stopped; seek(0) rebuilds it.
    virtual void release() {}
};

} // namespace newamp

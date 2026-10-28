#pragma once

#include <QList>
#include <QString>

class QAction;

namespace newamp {

// A sound effect a plugin adds to NewAmp's own audio pipeline (DSP plugins,
// in Winamp's terms). Enabled effects run in the order they were added,
// after decoding and before the equalizer, balance and volume.
//
// Effects only hear tracks NewAmp decodes itself: local files and remote
// tracks it fetches. Streams the platform player handles, and DRM sources
// such as Spotify, bypass them, as they bypass the equalizer.
//
// Header-only and free of QObject, like PluginHost, so plugins need no
// symbols from the executable.
class DspEffect
{
public:
    virtual ~DspEffect() = default;

    // Stable and unique, e.g. "<plugin id>.<effect>"; saved in settings to
    // remember which effects are on.
    virtual QString id() const = 0;
    // Shown in the DSP menu.
    virtual QString name() const = 0;

    // The calls below run on the audio thread, one at a time.

    // Before the first process() after the effect is switched on, and
    // whenever the format changes.
    virtual void configure(int sampleRate, int channels) = 0;
    // Interleaved float frames, roughly -1..1, changed in place. The frame
    // count can't change, so effects that stretch time don't fit here.
    virtual void process(float *samples, qint64 frames) = 0;
    // Forget past audio: the track changed or the user seeked.
    virtual void reset() {}

    // Settings, shown under the effect's name. Main thread; values that
    // process() reads must be safe to change while it runs (atomics).
    // Owned by the plugin.
    virtual QList<QAction *> actions() { return {}; }
};

} // namespace newamp

#pragma once

#include <QByteArray>

namespace newamp {

// How tracks join and how loud they play. Plain values so backends can copy
// them; core/PlaybackSettings owns and saves them.
struct PlaybackOptions
{
    enum class ReplayGain { Off, Track, Album };

    // Start the next track the moment this one ends, with no gap.
    bool gapless = true;
    // Fade the next track in over the end of this one; 0 is off.
    int crossfadeMs = 0;

    ReplayGain replayGain = ReplayGain::Off;
    double replayGainPreampDb = 0.0; // added to the tagged gain
    bool preventClipping = true;     // lower the gain so the tagged peak stays below full scale

    // QAudioDevice::id() of the output; empty follows the system default.
    QByteArray outputDevice;

    bool transitions() const { return gapless || crossfadeMs > 0; }
};

} // namespace newamp

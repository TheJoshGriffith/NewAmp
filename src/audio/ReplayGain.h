#pragma once

#include "audio/PlaybackOptions.h"

#include <QString>

#include <optional>

namespace newamp {

// Loudness tags, as written by ReplayGain scanners (foobar2000, beets,
// loudgain, ...). Gains are in dB against the ReplayGain reference level;
// peaks are linear, 1.0 being full scale.
struct ReplayGainInfo
{
    std::optional<double> trackGain;
    std::optional<double> trackPeak;
    std::optional<double> albumGain;
    std::optional<double> albumPeak;

    bool isEmpty() const { return !trackGain && !albumGain; }

    // Takes one tag, e.g. ("REPLAYGAIN_TRACK_GAIN", "-6.52 dB"); the key is
    // matched without regard to case. Opus's R128_*_GAIN tags are read too.
    // Returns false for keys that aren't loudness tags.
    bool assign(const QString &key, const QString &value);
};

// The linear factor to play a track at under the given options: 1.0 when
// ReplayGain is off or the track has no tags.
float replayGainFactor(const ReplayGainInfo &info, const PlaybackOptions &options);

} // namespace newamp

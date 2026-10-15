#pragma once

#include "core/Track.h"

namespace newamp {

// Where tracks come from. Local files, Plex and other services each
// implement this so the engine and UI never need to know which is which.
class MediaSource
{
public:
    virtual ~MediaSource() = default;

    virtual QString id() const = 0;
    virtual QString displayName() const = 0;

    // Resolve a track to something QMediaPlayer can open. Remote sources can
    // attach auth tokens or pick a transcode here.
    virtual QUrl streamUrl(const Track &track) const = 0;
};

} // namespace newamp

#pragma once

#include "core/Track.h"

#include <functional>

namespace newamp {

// Somewhere lyrics can come from: a file beside the track, its tags, a
// media server, an online database. Header-only so plugins can implement
// it without linking the app.
class LyricsProvider
{
public:
    // The lyrics as LRC or plain text; empty when this provider has none.
    using Callback = std::function<void(const QString &text)>;

    virtual ~LyricsProvider() = default;

    // Shown in the lyrics window as where the lyrics came from.
    virtual QString name() const = 0;
    virtual bool canHandle(const Track &track) const = 0;
    // Calls done exactly once, now or later. The caller ignores answers for
    // a track it no longer wants, so there is no need to cancel.
    virtual void fetch(const Track &track, const Callback &done) = 0;
};

} // namespace newamp

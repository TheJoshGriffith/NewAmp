#pragma once

#include "core/MediaSource.h"

#include <QtPlugin>

namespace newamp {

// Interface for optional sources (Plex, Spotify, ...) shipped as Qt plugins.
// Local playback is built in; everything else is loaded only when the user
// enables it, so a default install carries none of their dependencies.
//
// A plugin's JSON metadata must contain "id", "name" and "description" so
// the app can list it without loading the library.
class SourcePlugin
{
public:
    virtual ~SourcePlugin() = default;

    // Owned by the plugin; valid for as long as the plugin stays loaded.
    virtual MediaSource *source() = 0;
};

} // namespace newamp

#define NewAmpSourcePlugin_iid "org.newamp.SourcePlugin/1"
Q_DECLARE_INTERFACE(newamp::SourcePlugin, NewAmpSourcePlugin_iid)

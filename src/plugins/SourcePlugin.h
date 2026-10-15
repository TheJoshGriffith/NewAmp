#pragma once

#include "core/MediaSource.h"

#include <QList>
#include <QtPlugin>

class QAction;

namespace newamp {

class PluginHost;

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

    // Called once, right after loading and before anything else is used.
    virtual void initialize(PluginHost &host) { Q_UNUSED(host) }

    // Owned by the plugin; valid for as long as the plugin stays loaded.
    virtual MediaSource *source() = 0;

    // Menu entries shown under the plugin's name in the main context menu.
    // Owned by the plugin.
    virtual QList<QAction *> actions() { return {}; }
};

} // namespace newamp

// Bump the version whenever this interface or PluginHost changes, so stale
// plugins are skipped instead of crashing.
#define NewAmpSourcePlugin_iid "org.newamp.SourcePlugin/3"
Q_DECLARE_INTERFACE(newamp::SourcePlugin, NewAmpSourcePlugin_iid)

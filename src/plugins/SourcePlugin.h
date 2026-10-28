#pragma once

#include "core/MediaSource.h"

#include <QList>
#include <QtPlugin>

class QAction;

namespace newamp {

class PluginHost;

// Interface for every optional NewAmp plugin, shipped as a Qt plugin. Local
// playback is built in; everything else is loaded only when the user
// enables it, so a default install carries none of their dependencies.
//
// One interface covers every kind of plugin; a plugin is whatever it offers:
// - a source of tracks (Plex, Spotify, CD): source(), and usually a library
//   through PluginHost::addLibrary();
// - DSP effects: PluginHost::addDspEffect();
// - visualizations: PluginHost::addVisualization();
// - general purpose: anything else done with the host, such as a
//   PlaybackObserver, with its own actions() in the menu.
// A plugin may offer several of these.
//
// A plugin's JSON metadata must contain "id", "name" and "description" so
// the app can list it without loading the library. An optional "category"
// ("source", "dsp", "visualization" or "general") groups it in the Plugins
// menu; it defaults to "source".
class SourcePlugin
{
public:
    virtual ~SourcePlugin() = default;

    // Called once, right after loading and before anything else is used.
    virtual void initialize(PluginHost &host) { Q_UNUSED(host) }

    // The plugin's tracks, or null when it has none. Owned by the plugin;
    // valid for as long as the plugin stays loaded.
    virtual MediaSource *source() { return nullptr; }

    // Menu entries shown under the plugin's name in the main context menu.
    // Owned by the plugin.
    virtual QList<QAction *> actions() { return {}; }
};

} // namespace newamp

// Bump the version whenever this interface, PluginHost, LibraryProvider,
// DspEffect or Visualization changes, so stale plugins are skipped instead
// of crashing.
#define NewAmpSourcePlugin_iid "org.newamp.SourcePlugin/7"
Q_DECLARE_INTERFACE(newamp::SourcePlugin, NewAmpSourcePlugin_iid)

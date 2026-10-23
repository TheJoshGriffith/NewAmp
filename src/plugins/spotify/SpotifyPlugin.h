#pragma once

#include "plugins/SourcePlugin.h"

#include <QObject>

#include <memory>

class QSettings;

namespace newamp::spotify {

class SpotifyClient;
class SpotifyLibrary;

// Spotify as a NewAmp source: sign in, browse and search the library, and
// play through Spotify Connect.
class SpotifyPlugin final : public QObject, public SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "spotify.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    SpotifyPlugin();
    ~SpotifyPlugin() override;

    void initialize(PluginHost &host) override;
    MediaSource *source() override;
    QList<QAction *> actions() override;

private:
    void showLibrary();
    void askClientId();
    void chooseDevice();

    PluginHost *m_host = nullptr;
    std::unique_ptr<QSettings> m_settings;
    SpotifyClient *m_client = nullptr;   // child of this
    SpotifyLibrary *m_library = nullptr; // child of this
    QAction *m_browse = nullptr;
    QAction *m_account = nullptr;
    QAction *m_clientId = nullptr;
    QAction *m_device = nullptr;
};

} // namespace newamp::spotify

#pragma once

#include "plugins/SourcePlugin.h"

#include <QObject>
#include <QPointer>

#include <memory>

class QSettings;

namespace newamp::plex {

class PlexBrowser;
class PlexClient;
class PlexReporter;

// Plex Media Server as a NewAmp source: sign in, browse, play and report.
class PlexPlugin final : public QObject, public SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "plex.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    PlexPlugin();
    ~PlexPlugin() override;

    void initialize(PluginHost &host) override;
    MediaSource *source() override;
    QList<QAction *> actions() override;

private:
    void showBrowser();

    PluginHost *m_host = nullptr;
    std::unique_ptr<QSettings> m_settings;
    PlexClient *m_client = nullptr; // child of this
    std::unique_ptr<PlexReporter> m_reporter;
    QPointer<PlexBrowser> m_browser;
    QAction *m_browse = nullptr;
    QAction *m_account = nullptr;
};

} // namespace newamp::plex

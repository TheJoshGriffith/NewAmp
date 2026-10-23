#pragma once

#include "plugins/SourcePlugin.h"

#include <QObject>
#include <QPointer>

#include <memory>

class QSettings;

namespace newamp::jellyfin {

class JellyfinClient;
class JellyfinLibrary;
class JellyfinReporter;
class JellyfinSignInDialog;

// A Jellyfin server as a NewAmp source: sign in, browse, play and report.
class JellyfinPlugin final : public QObject, public SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "jellyfin.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    JellyfinPlugin();
    ~JellyfinPlugin() override;

    void initialize(PluginHost &host) override;
    MediaSource *source() override;
    QList<QAction *> actions() override;

private:
    void showLibrary();
    void showSignIn();

    PluginHost *m_host = nullptr;
    std::unique_ptr<QSettings> m_settings;
    JellyfinClient *m_client = nullptr; // child of this
    std::unique_ptr<JellyfinReporter> m_reporter;
    JellyfinLibrary *m_library = nullptr; // child of this
    QPointer<JellyfinSignInDialog> m_signIn;
    QAction *m_browse = nullptr;
    QAction *m_account = nullptr;
};

} // namespace newamp::jellyfin

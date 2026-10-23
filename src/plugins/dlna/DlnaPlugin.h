#pragma once

#include "plugins/SourcePlugin.h"

#include <QObject>

#include <memory>

class QSettings;

namespace newamp::dlna {

class DlnaClient;
class DlnaLibrary;

// UPnP / DLNA media servers on the local network as a NewAmp source:
// discover, browse, search and play.
class DlnaPlugin final : public QObject, public SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "dlna.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    DlnaPlugin();
    ~DlnaPlugin() override;

    void initialize(PluginHost &host) override;
    MediaSource *source() override;
    QList<QAction *> actions() override;

private:
    void addServer();

    PluginHost *m_host = nullptr;
    std::unique_ptr<QSettings> m_settings;
    DlnaClient *m_client = nullptr;   // child of this
    DlnaLibrary *m_library = nullptr; // child of this
    QAction *m_browse = nullptr;
    QAction *m_rescan = nullptr;
};

} // namespace newamp::dlna

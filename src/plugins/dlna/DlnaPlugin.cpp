#include "DlnaPlugin.h"

#include "DlnaClient.h"
#include "DlnaLibrary.h"
#include "plugins/PluginHost.h"

#include <QAction>
#include <QInputDialog>
#include <QSettings>

namespace newamp::dlna {

DlnaPlugin::DlnaPlugin() = default;

DlnaPlugin::~DlnaPlugin()
{
    // The library uses the client, so it goes first.
    delete m_library;
    delete m_client;
}

void DlnaPlugin::initialize(PluginHost &host)
{
    m_host = &host;
    m_settings = host.settings(QString::fromLatin1(SourceId));
    if (!m_settings)
        m_settings = std::make_unique<QSettings>();
    m_client = new DlnaClient(*m_settings, this);

    m_browse = new QAction(tr("Browse media servers..."), this);
    connect(m_browse, &QAction::triggered, this, [this] { m_host->showLibrary(m_library); });
    m_rescan = new QAction(tr("Look for media servers again"), this);
    connect(m_rescan, &QAction::triggered, m_client, &DlnaClient::discover);
    auto *add = new QAction(tr("Add media server by address..."), this);
    connect(add, &QAction::triggered, this, &DlnaPlugin::addServer);

    m_library = new DlnaLibrary(*m_client, host, {m_rescan, add}, this);
    host.addLibrary(m_library);

    // Restored DLNA tracks play from the address they were listed at, and
    // follow their server once it has answered from a new one.
    m_client->discover();
}

MediaSource *DlnaPlugin::source()
{
    return m_client; // null until initialize()
}

QList<QAction *> DlnaPlugin::actions()
{
    if (!m_client)
        return {};
    return {m_browse, m_rescan};
}

void DlnaPlugin::addServer()
{
    bool ok = false;
    const QString text = QInputDialog::getText(
        m_host->dialogParent(), tr("Add Media Server"),
        tr("The server's device description address, such as\n"
           "http://192.168.1.20:8200/rootDesc.xml (MiniDLNA)\n"
           "http://192.168.1.20:32469/DeviceDescription.xml (Plex)"),
        QLineEdit::Normal, QString(), &ok);
    if (!ok || text.trimmed().isEmpty())
        return;
    QUrl url = QUrl::fromUserInput(text.trimmed());
    if (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https"))
        url.setScheme(QStringLiteral("http"));
    m_client->addServer(url);
    m_host->showLibrary(m_library);
}

} // namespace newamp::dlna

#include "PlexPlugin.h"

#include "PlexBrowser.h"
#include "PlexClient.h"
#include "PlexReporter.h"
#include "plugins/PluginHost.h"

#include <QAction>
#include <QSettings>

namespace newamp::plex {

PlexPlugin::PlexPlugin() = default;

PlexPlugin::~PlexPlugin()
{
    // The browser and reporter use the client, so they go first.
    delete m_browser;
    m_reporter.reset();
    delete m_client;
}

void PlexPlugin::initialize(PluginHost &host)
{
    m_host = &host;
    m_settings = host.settings(QString::fromLatin1(SourceId));
    if (!m_settings)
        m_settings = std::make_unique<QSettings>();
    m_client = new PlexClient(*m_settings, this);
    PlexClient *client = m_client;
    m_reporter = std::make_unique<PlexReporter>(
        [client](const QString &server, const QString &path) { client->report(server, path); });
    host.addPlaybackObserver(m_reporter.get());

    m_browse = new QAction(tr("Browse Plex..."), this);
    connect(m_browse, &QAction::triggered, this, &PlexPlugin::showBrowser);
    m_account = new QAction(this);
    connect(m_account, &QAction::triggered, this, [this] {
        if (m_client->isSignedIn())
            m_client->signOut();
        else
            m_client->signIn();
    });
    auto updateAccount = [this] {
        m_account->setText(m_client->isSignedIn() ? tr("Sign out of Plex") : tr("Sign in to Plex..."));
    };
    connect(m_client, &PlexClient::signedInChanged, this, updateAccount);
    updateAccount();

    // Servers are needed to play restored Plex tracks, so look them up now.
    m_client->refreshServers();
}

MediaSource *PlexPlugin::source()
{
    return m_client; // null until initialize()
}

QList<QAction *> PlexPlugin::actions()
{
    if (!m_client)
        return {};
    return {m_browse, m_account};
}

void PlexPlugin::showBrowser()
{
    if (!m_browser) {
        m_browser = new PlexBrowser(*m_client, *m_host, m_host->dialogParent());
        m_browser->setAttribute(Qt::WA_DeleteOnClose);
        if (!m_client->isSignedIn())
            m_client->signIn();
    }
    m_browser->show();
    m_browser->raise();
    m_browser->activateWindow();
}

} // namespace newamp::plex

#include "JellyfinPlugin.h"

#include "JellyfinClient.h"
#include "JellyfinLibrary.h"
#include "JellyfinReporter.h"
#include "JellyfinSignInDialog.h"
#include "plugins/PluginHost.h"

#include <QAction>
#include <QSettings>

namespace newamp::jellyfin {

JellyfinPlugin::JellyfinPlugin() = default;

JellyfinPlugin::~JellyfinPlugin()
{
    // The dialog, library and reporter use the client, so they go first.
    delete m_signIn;
    delete m_library;
    m_reporter.reset();
    delete m_client;
}

void JellyfinPlugin::initialize(PluginHost &host)
{
    m_host = &host;
    m_settings = host.settings(QString::fromLatin1(SourceId));
    if (!m_settings)
        m_settings = std::make_unique<QSettings>();
    m_client = new JellyfinClient(*m_settings, this);
    JellyfinClient *client = m_client;
    m_reporter = std::make_unique<JellyfinReporter>(
        [client](const QString &path, const QByteArray &json) { client->post(path, json); });
    host.addPlaybackObserver(m_reporter.get());

    m_browse = new QAction(tr("Browse Jellyfin..."), this);
    connect(m_browse, &QAction::triggered, this, &JellyfinPlugin::showLibrary);
    m_account = new QAction(this);
    connect(m_account, &QAction::triggered, this, [this] {
        if (m_client->isSignedIn())
            m_client->signOut();
        else
            showSignIn();
    });
    auto updateAccount = [this] {
        m_account->setText(m_client->isSignedIn() ? tr("Sign out of Jellyfin (%1)").arg(m_client->userName())
                                                  : tr("Sign in to Jellyfin..."));
    };
    connect(m_client, &JellyfinClient::signedInChanged, this, updateAccount);
    updateAccount();

    m_library = new JellyfinLibrary(*m_client, host, {m_account}, this);
    host.addLibrary(m_library);
}

MediaSource *JellyfinPlugin::source()
{
    return m_client; // null until initialize()
}

QList<QAction *> JellyfinPlugin::actions()
{
    if (!m_client)
        return {};
    return {m_browse, m_account};
}

void JellyfinPlugin::showLibrary()
{
    if (!m_client->isSignedIn())
        showSignIn();
    m_host->showLibrary(m_library);
}

void JellyfinPlugin::showSignIn()
{
    if (!m_signIn) {
        m_signIn = new JellyfinSignInDialog(*m_client, m_host->dialogParent());
        m_signIn->setAttribute(Qt::WA_DeleteOnClose);
    }
    m_signIn->show();
    m_signIn->raise();
    m_signIn->activateWindow();
}

} // namespace newamp::jellyfin

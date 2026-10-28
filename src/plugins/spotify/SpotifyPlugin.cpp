#include "SpotifyPlugin.h"

#include "SpotifyClient.h"
#include "SpotifyConnect.h"
#include "SpotifyLibrary.h"
#include "SpotifyWebPlayer.h"
#include "plugins/PluginHost.h"

#include <QAction>
#include <QInputDialog>
#include <QSettings>

namespace newamp::spotify {

SpotifyPlugin::SpotifyPlugin() = default;

SpotifyPlugin::~SpotifyPlugin()
{
    // The library and the player use the client, so they go first.
    delete m_webPlayer;
    delete m_library;
    delete m_client;
}

void SpotifyPlugin::initialize(PluginHost &host)
{
    m_host = &host;
    m_settings = host.settings(QString::fromLatin1(SourceId));
    if (!m_settings)
        m_settings = std::make_unique<QSettings>();
    m_client = new SpotifyClient(*m_settings, this);

    m_browse = new QAction(tr("Browse Spotify..."), this);
    connect(m_browse, &QAction::triggered, this, &SpotifyPlugin::showLibrary);
    m_account = new QAction(this);
    connect(m_account, &QAction::triggered, this, [this] {
        if (m_client->isSignedIn())
            m_client->signOut();
        else if (m_client->clientId().isEmpty())
            askClientId();
        else
            m_client->signIn();
    });
    auto updateAccount = [this] {
        m_account->setText(m_client->isSignedIn() ? tr("Sign out of Spotify") : tr("Sign in to Spotify..."));
    };
    connect(m_client, &SpotifyClient::signedInChanged, this, updateAccount);
    updateAccount();
    startWebPlayer();
    m_clientId = new QAction(tr("Spotify client ID..."), this);
    connect(m_clientId, &QAction::triggered, this, &SpotifyPlugin::askClientId);
    m_device = new QAction(tr("Play on..."), this);
    connect(m_device, &QAction::triggered, this, &SpotifyPlugin::chooseDevice);

    m_library = new SpotifyLibrary(*m_client, host, {m_account, m_device, m_clientId}, this);
    host.addLibrary(m_library);
}

MediaSource *SpotifyPlugin::source()
{
    return m_client; // null until initialize()
}

QList<QAction *> SpotifyPlugin::actions()
{
    if (!m_client)
        return {};
    return {m_browse, m_account, m_device, m_clientId};
}

void SpotifyPlugin::startWebPlayer()
{
    if (!SpotifyWebPlayer::isSupported())
        return;
    m_webPlayer = new SpotifyWebPlayer(*m_client, this);
    SpotifyConnect *connectPlayer = m_client->connectPlayer();
    connect(m_webPlayer, &SpotifyWebPlayer::ready, connectPlayer, &SpotifyConnect::setAppDevice);
    connect(m_webPlayer, &SpotifyWebPlayer::notReady, connectPlayer, [connectPlayer] { connectPlayer->setAppDevice({}); });
    connect(m_webPlayer, &SpotifyWebPlayer::problem, m_client, &SpotifyClient::status);
    connect(connectPlayer, &SpotifyConnect::appPlayerStale, m_webPlayer, &SpotifyWebPlayer::start);
    connect(m_client, &SpotifyClient::signedInChanged, m_webPlayer, [this](bool signedIn) {
        if (signedIn)
            m_webPlayer->start();
        else
            m_webPlayer->stop();
    });
    if (m_client->isSignedIn())
        m_webPlayer->start();
}

void SpotifyPlugin::showLibrary()
{
    if (!m_client->isSignedIn())
        m_account->trigger();
    m_host->showLibrary(m_library);
}

void SpotifyPlugin::askClientId()
{
    bool ok = false;
    const QString id = QInputDialog::getText(
        m_host->dialogParent(), tr("Spotify client ID"),
        tr("NewAmp talks to Spotify through an app you register for yourself:\n\n"
           "1. Open developer.spotify.com/dashboard and create an app.\n"
           "2. Add the redirect URI %1\n"
           "3. Tick \"Web API\", save, and paste the app's client ID here.\n\n"
           "Playing music needs Spotify Premium.")
            .arg(redirectUri()),
        QLineEdit::Normal, m_client->clientId(), &ok);
    if (!ok)
        return;
    m_client->setClientId(id);
    if (!id.trimmed().isEmpty() && !m_client->isSignedIn())
        m_client->signIn();
}

void SpotifyPlugin::chooseDevice()
{
    if (!m_client->isSignedIn()) {
        m_account->trigger();
        return;
    }
    SpotifyConnect *player = m_client->connectPlayer();
    player->listDevices([this, player](const QList<Device> &devices) {
        // The first entry lets NewAmp choose (see pickDevice()).
        QStringList names{SpotifyWebPlayer::isSupported()
                              ? tr("Automatic: NewAmp's own player, else Spotify on this computer")
                              : tr("Automatic: Spotify on this computer, else the active device")};
        int current = 0;
        for (const Device &device : devices) {
            names.append(device.restricted ? tr("%1 (can't be controlled)").arg(device.name) : device.name);
            if (device.id == player->deviceId())
                current = int(names.size()) - 1;
        }
        bool ok = false;
        const QString picked = QInputDialog::getItem(
            m_host->dialogParent(), tr("Play Spotify on"),
            devices.isEmpty() ? tr("No Spotify devices are open. Start Spotify on this computer or your phone.")
                              : tr("Spotify plays on:"),
            names, current, false, &ok);
        if (!ok)
            return;
        const qsizetype index = names.indexOf(picked);
        player->setDeviceId(index > 0 ? devices.at(index - 1).id : QString());
    });
}

} // namespace newamp::spotify

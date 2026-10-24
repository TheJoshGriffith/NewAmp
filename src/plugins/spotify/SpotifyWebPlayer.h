#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace newamp::spotify {

class SpotifyClient;
class WebView;

// NewAmp's own Spotify player: Spotify's Web Playback SDK running in a hidden
// system web view (WKWebView on macOS). It shows up as a Spotify Connect
// device named "NewAmp", so SpotifyConnect plays on it like on any other
// device and no Spotify app is needed. Elsewhere it is unsupported for now.
class SpotifyWebPlayer final : public QObject
{
    Q_OBJECT

public:
    explicit SpotifyWebPlayer(SpotifyClient &client, QObject *parent = nullptr);
    ~SpotifyWebPlayer() override;

    // Whether this platform has a web view that can run the player.
    static bool isSupported();

    // Loads the player, which then asks the client for tokens. Restarting
    // a running player reloads it.
    void start();
    void stop();
    bool isRunning() const;

    // A message the page posted: a JSON object with a "type". Public for
    // the tests; the web view is the only other caller.
    void handleMessage(const QByteArray &json);

Q_SIGNALS:
    // The player registered with Spotify as this Connect device.
    void ready(const QString &deviceId);
    // The device went away (network, sign-out, stop()).
    void notReady();
    // Why the player can't play, for the status line.
    void problem(const QString &message);

private:
    void sendToken();
    void say(const QString &problem);

    SpotifyClient &m_client;
    std::unique_ptr<WebView> m_view; // null while stopped
    QString m_deviceId;              // set while ready
};

} // namespace newamp::spotify

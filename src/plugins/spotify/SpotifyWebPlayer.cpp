#include "SpotifyWebPlayer.h"

#include "SpotifyClient.h"
#include "SpotifyWebView.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTimer>

namespace newamp::spotify {

namespace {

// How long the player gets to come back online by itself.
constexpr int ReconnectMs = 10000;

// The player page. It loads the SDK, hands every event to NewAmp and asks
// NewAmp for tokens. It also reports which DRM systems the web view has,
// since Spotify's audio needs one.
const char PlayerPage[] = R"html(<!doctype html>
<html><head><meta charset="utf-8"><title>NewAmp</title></head><body><script>
function post(message) { window.webkit.messageHandlers.newamp.postMessage(JSON.stringify(message)); }
window.onerror = function (message, source, line) { post({type: 'log', message: message + ' (' + source + ':' + line + ')'}); };

var waiting = [];
window.newampToken = function (token) { var callbacks = waiting; waiting = []; callbacks.forEach(function (cb) { cb(token); }); };

(function checkDrm() {
  if (!navigator.requestMediaKeySystemAccess) { post({type: 'drm', systems: [], legacy: !!window.WebKitMediaKeys}); return; }
  var configs = [
    {initDataTypes: ['cenc'], audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]},
    {initDataTypes: ['sinf', 'skd'], audioCapabilities: [{contentType: 'audio/mp4'}]}
  ];
  var names = ['com.widevine.alpha', 'com.apple.fps', 'com.apple.fps.1_0'];
  Promise.all(names.map(function (name) {
    return navigator.requestMediaKeySystemAccess(name, configs).then(function () { return name; }, function () { return null; });
  })).then(function (found) { post({type: 'drm', systems: found.filter(Boolean), legacy: !!window.WebKitMediaKeys}); });
})();

window.onSpotifyWebPlaybackSDKReady = function () {
  var player = new Spotify.Player({
    name: 'NewAmp',
    volume: 1.0,
    getOAuthToken: function (cb) { waiting.push(cb); post({type: 'token'}); }
  });
  ['ready', 'not_ready'].forEach(function (name) {
    player.addListener(name, function (e) { post({type: name, device_id: e.device_id}); });
  });
  ['initialization_error', 'authentication_error', 'account_error', 'playback_error'].forEach(function (name) {
    player.addListener(name, function (e) { post({type: name, message: e.message}); });
  });
  player.addListener('autoplay_failed', function () { post({type: 'autoplay_failed'}); });
  player.connect().then(function (ok) { if (!ok) post({type: 'connect_failed'}); });
  window.newampPlayer = player;
};

var sdk = document.createElement('script');
sdk.src = 'https://sdk.scdn.co/spotify-player.js';
sdk.onerror = function () { post({type: 'sdk_failed'}); };
document.head.appendChild(sdk);
</script></body></html>
)html";

} // namespace

SpotifyWebPlayer::SpotifyWebPlayer(SpotifyClient &client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
}

SpotifyWebPlayer::~SpotifyWebPlayer() = default;

bool SpotifyWebPlayer::isSupported()
{
    return WebView::isSupported();
}

void SpotifyWebPlayer::start()
{
    stop();
    m_view = WebView::create([this](const QByteArray &message) { handleMessage(message); });
    if (!m_view)
        return;
    // The SDK and DRM need a secure origin; nothing is fetched from it.
    m_view->load(QString::fromUtf8(PlayerPage), QUrl(QStringLiteral("https://localhost/")));
}

void SpotifyWebPlayer::stop()
{
    if (!m_view)
        return;
    m_view->run(QStringLiteral("window.newampPlayer && window.newampPlayer.disconnect();"));
    m_view.reset();
    if (!m_deviceId.isEmpty()) {
        m_deviceId.clear();
        Q_EMIT notReady();
    }
}

bool SpotifyWebPlayer::isRunning() const
{
    return m_view != nullptr;
}

void SpotifyWebPlayer::handleMessage(const QByteArray &json)
{
    const QJsonObject message = QJsonDocument::fromJson(json).object();
    const QString type = message.value(QStringLiteral("type")).toString();
    const QString text = message.value(QStringLiteral("message")).toString();
    if (SpotifyClient::debugEnabled())
        qInfo("Spotify player: %s", json.constData());

    if (type == QLatin1String("token")) {
        sendToken();
    } else if (type == QLatin1String("ready")) {
        m_deviceId = message.value(QStringLiteral("device_id")).toString();
        Q_EMIT ready(m_deviceId);
    } else if (type == QLatin1String("not_ready")) {
        m_deviceId.clear();
        Q_EMIT notReady();
        // The SDK went offline (the Mac slept, the network dropped). It
        // doesn't always come back by itself, so reload it if it hasn't.
        QTimer::singleShot(ReconnectMs, this, [this] {
            if (m_view && m_deviceId.isEmpty())
                start();
        });
    } else if (type == QLatin1String("drm")) {
        if (message.value(QStringLiteral("systems")).toArray().isEmpty())
            say(tr("This computer's web view can't play Spotify's protected audio, so NewAmp can't be its own "
                   "Spotify player. Choose a Spotify device under \"Play on...\" instead."));
    } else if (type == QLatin1String("sdk_failed")) {
        say(tr("Couldn't load Spotify's player from sdk.scdn.co. Check the network and sign in again."));
    } else if (type == QLatin1String("initialization_error") || type == QLatin1String("connect_failed")) {
        say(tr("Spotify's player can't start in NewAmp: %1").arg(text.isEmpty() ? tr("it didn't connect") : text));
    } else if (type == QLatin1String("authentication_error")) {
        say(tr("Spotify refused NewAmp's player (%1). Sign out of Spotify and sign in again so NewAmp gets "
               "the permissions its player needs.")
                .arg(text));
    } else if (type == QLatin1String("account_error")) {
        say(tr("Spotify refused NewAmp's player for this account (%1). It needs Premium, and so does the "
               "account that owns your Spotify developer app.")
                .arg(text));
    } else if (type == QLatin1String("playback_error")) {
        say(tr("Spotify's player couldn't play this: %1").arg(text));
    } else if (type == QLatin1String("autoplay_failed")) {
        say(tr("The web view blocked Spotify's player from starting audio."));
    }
}

void SpotifyWebPlayer::sendToken()
{
    m_client.accessToken(this, [this](const QString &token) {
        if (!m_view)
            return;
        if (token.isEmpty()) {
            say(tr("Sign in to Spotify to use NewAmp's player."));
            return;
        }
        // A JSON array is the simplest safe way to quote the token for JS.
        const QByteArray quoted = QJsonDocument(QJsonArray{token}).toJson(QJsonDocument::Compact);
        m_view->run(QStringLiteral("window.newampToken(%1[0]);").arg(QString::fromUtf8(quoted)));
    });
}

void SpotifyWebPlayer::say(const QString &problem)
{
    qWarning("Spotify player: %s", qPrintable(problem));
    Q_EMIT this->problem(problem);
}

} // namespace newamp::spotify

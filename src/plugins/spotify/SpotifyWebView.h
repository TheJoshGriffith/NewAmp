#pragma once

#include <QByteArray>
#include <QString>
#include <QUrl>

#include <functional>
#include <memory>

namespace newamp::spotify {

// The hidden system web view SpotifyWebPlayer runs Spotify's player in.
// Each platform supplies create(); SpotifyWebView_mac.mm uses WKWebView.
class WebView
{
public:
    // Gets the strings the page posts to window.webkit.messageHandlers.newamp.
    using MessageHandler = std::function<void(const QByteArray &message)>;

    // Null where there is no suitable web view.
    static std::unique_ptr<WebView> create(const MessageHandler &onMessage);
    static bool isSupported();

    virtual ~WebView() = default;
    // baseUrl is the page's origin; the player needs a secure one.
    virtual void load(const QString &html, const QUrl &baseUrl) = 0;
    virtual void run(const QString &script) = 0;
};

} // namespace newamp::spotify

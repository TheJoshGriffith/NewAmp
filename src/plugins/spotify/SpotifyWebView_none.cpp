#include "SpotifyWebView.h"

namespace newamp::spotify {

// No web view here that can play Spotify's protected audio yet.
std::unique_ptr<WebView> WebView::create(const MessageHandler &)
{
    return nullptr;
}

bool WebView::isSupported()
{
    return false;
}

} // namespace newamp::spotify

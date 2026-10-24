#include "SpotifyWebView.h"

#import <AppKit/AppKit.h>
#import <WebKit/WebKit.h>

// Gets the page's messages; held by the web view's content controller.
@interface NewAmpSpotifyMessages : NSObject <WKScriptMessageHandler>
- (instancetype)initWithHandler:(newamp::spotify::WebView::MessageHandler)handler;
@end

@implementation NewAmpSpotifyMessages {
    newamp::spotify::WebView::MessageHandler _handler;
}

- (instancetype)initWithHandler:(newamp::spotify::WebView::MessageHandler)handler
{
    if ((self = [super init]))
        _handler = std::move(handler);
    return self;
}

- (void)forget
{
    _handler = nullptr;
}

- (void)userContentController:(WKUserContentController *)controller didReceiveScriptMessage:(WKScriptMessage *)message
{
    if (_handler && [message.body isKindOfClass:[NSString class]])
        _handler(QString::fromNSString(message.body).toUtf8());
}

@end

namespace newamp::spotify {

namespace {

const auto HandlerName = @"newamp";

class MacWebView final : public WebView
{
public:
    explicit MacWebView(const MessageHandler &onMessage)
    {
        m_messages = [[NewAmpSpotifyMessages alloc] initWithHandler:onMessage];

        WKWebViewConfiguration *config = [[WKWebViewConfiguration alloc] init];
        config.mediaTypesRequiringUserActionForPlayback = WKAudiovisualMediaTypeNone;
        // Spotify's SDK checks for a browser it supports.
        config.applicationNameForUserAgent = @"Version/17.0 Safari/605.1.15";
        [config.userContentController addScriptMessageHandler:m_messages name:HandlerName];

        m_view = [[WKWebView alloc] initWithFrame:NSMakeRect(0, 0, 320, 240) configuration:config];
        if (@available(macOS 13.3, *))
            m_view.inspectable = YES; // Safari's Develop menu can debug the player

        // WebKit treats a view outside any window as a background page, so it
        // gets a window of its own: borderless, transparent and off screen.
        m_window = [[NSWindow alloc] initWithContentRect:NSMakeRect(-10000, -10000, 320, 240)
                                               styleMask:NSWindowStyleMaskBorderless
                                                 backing:NSBackingStoreBuffered
                                                   defer:NO];
        m_window.releasedWhenClosed = NO;
        m_window.ignoresMouseEvents = YES;
        m_window.alphaValue = 0;
        m_window.excludedFromWindowsMenu = YES;
        m_window.collectionBehavior = NSWindowCollectionBehaviorTransient | NSWindowCollectionBehaviorIgnoresCycle;
        m_window.contentView = m_view;
        [m_window orderBack:nil];
    }

    ~MacWebView() override
    {
        [m_messages forget];
        [m_view.configuration.userContentController removeScriptMessageHandlerForName:HandlerName];
        [m_view stopLoading];
        [m_window orderOut:nil];
        m_window.contentView = nil;
        [m_window close];
    }

    void load(const QString &html, const QUrl &baseUrl) override
    {
        [m_view loadHTMLString:html.toNSString() baseURL:baseUrl.toNSURL()];
    }

    void run(const QString &script) override
    {
        [m_view evaluateJavaScript:script.toNSString() completionHandler:nil];
    }

private:
    NewAmpSpotifyMessages *m_messages;
    WKWebView *m_view;
    NSWindow *m_window;
};

} // namespace

std::unique_ptr<WebView> WebView::create(const MessageHandler &onMessage)
{
    return std::make_unique<MacWebView>(onMessage);
}

bool WebView::isSupported()
{
    return true;
}

} // namespace newamp::spotify

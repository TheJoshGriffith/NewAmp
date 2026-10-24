#include "platform/MediaControls.h"

#include "core/PlayerController.h"

#include <QAbstractNativeEventFilter>
#include <QCoreApplication>
#include <QPointer>

#include <qt_windows.h>

#include <iterator>

// The keyboard's media keys, taken as global hot keys so they work whichever
// app has focus. A key another app (another player) already holds stays
// with it.

namespace newamp {

namespace {

struct MediaKey
{
    UINT virtualKey;
    void (*action)(PlayerController &);
};

const MediaKey MediaKeys[] = {
    {VK_MEDIA_PLAY_PAUSE, [](PlayerController &c) { c.playPause(); }},
    {VK_MEDIA_STOP, [](PlayerController &c) { c.stop(); }},
    {VK_MEDIA_NEXT_TRACK, [](PlayerController &c) { c.next(); }},
    {VK_MEDIA_PREV_TRACK, [](PlayerController &c) { c.previous(); }},
};

// Hot key ids are per thread and only need to be distinct from each other.
constexpr int FirstHotKeyId = 0x4e41; // "NA"

class WinMediaControls final : public MediaControlsBackend, public QAbstractNativeEventFilter
{
public:
    explicit WinMediaControls(PlayerController &controller)
        : m_controller(&controller)
    {
        for (int i = 0; i < int(std::size(MediaKeys)); ++i) {
            // A null window sends WM_HOTKEY to this thread's message queue.
            if (RegisterHotKey(nullptr, FirstHotKeyId + i, MOD_NOREPEAT, MediaKeys[i].virtualKey))
                m_registered << FirstHotKeyId + i;
            else
                qWarning("NewAmp: media key %u is held by another app", MediaKeys[i].virtualKey);
        }
        QCoreApplication::instance()->installNativeEventFilter(this);
    }

    ~WinMediaControls() override
    {
        if (QCoreApplication *app = QCoreApplication::instance())
            app->removeNativeEventFilter(this);
        for (int id : std::as_const(m_registered))
            UnregisterHotKey(nullptr, id);
    }

    bool isUseful() const { return !m_registered.isEmpty(); }

    void update(const NowPlaying &) override {}

    bool nativeEventFilter(const QByteArray &, void *message, qintptr *) override
    {
        const MSG *msg = static_cast<const MSG *>(message);
        if (msg->message != WM_HOTKEY || msg->hwnd != nullptr || !m_registered.contains(int(msg->wParam)))
            return false;
        if (m_controller)
            MediaKeys[msg->wParam - FirstHotKeyId].action(*m_controller);
        return true;
    }

private:
    QPointer<PlayerController> m_controller;
    QList<int> m_registered;
};

} // namespace

std::unique_ptr<MediaControlsBackend> MediaControlsBackend::create(PlayerController &controller,
                                                                   const std::function<void()> &)
{
    auto backend = std::make_unique<WinMediaControls>(controller);
    if (!backend->isUseful())
        return nullptr;
    return backend;
}

} // namespace newamp

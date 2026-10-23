#pragma once

#include "JellyfinApi.h"
#include "plugins/PluginHost.h"

#include <QElapsedTimer>

#include <functional>

namespace newamp::jellyfin {

// Tells the server what is playing, so Jellyfin shows NewAmp under active
// devices and counts a track as played when it stops near the end.
class JellyfinReporter final : public PlaybackObserver
{
public:
    // POSTs a JSON body to a server path; JellyfinClient::post in the plugin,
    // a recorder in tests.
    using Send = std::function<void(const QString &path, const QByteArray &json)>;

    explicit JellyfinReporter(Send send) : m_send(std::move(send)) {}

    void trackStarted(const Track &track) override;
    void playbackStateChanged(State state) override;
    void positionChanged(qint64 positionMs, qint64 durationMs) override;
    void trackFinished(const Track &track) override;

private:
    void progress(const QString &event);
    void stopped();

    Send m_send;
    QString m_itemId; // empty when the current track isn't from Jellyfin
    bool m_paused = false;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    QElapsedTimer m_sinceReport;
};

} // namespace newamp::jellyfin

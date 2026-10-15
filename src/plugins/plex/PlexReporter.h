#pragma once

#include "PlexApi.h"
#include "plugins/PluginHost.h"

#include <QElapsedTimer>

#include <functional>

namespace newamp::plex {

// Tells the server what is playing (timeline) and marks tracks as played
// (scrobble), so Plex keeps "now playing", play counts and history in sync.
class PlexReporter final : public PlaybackObserver
{
public:
    // Sends a request path (with query) to the given server; PlexClient::report
    // in the plugin, a recorder in tests.
    using Send = std::function<void(const QString &machineIdentifier, const QString &path)>;

    explicit PlexReporter(Send send) : m_send(std::move(send)) {}

    void trackStarted(const Track &track) override;
    void playbackStateChanged(State state) override;
    void positionChanged(qint64 positionMs, qint64 durationMs) override;
    void trackFinished(const Track &track) override;

private:
    void sendTimeline(const QString &state);
    void scrobble();

    Send m_send;
    TrackRef m_current; // invalid when the current track isn't from Plex
    QString m_lastState;
    qint64 m_positionMs = 0;
    qint64 m_durationMs = 0;
    bool m_scrobbled = false;
    QElapsedTimer m_sinceReport;
};

} // namespace newamp::plex

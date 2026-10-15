#include "PlexReporter.h"

namespace newamp::plex {

namespace {
constexpr qint64 ReportIntervalMs = 10000;
constexpr double PlayedFraction = 0.9; // Plex counts a track as played here

QString stateName(PlaybackObserver::State state)
{
    switch (state) {
    case PlaybackObserver::State::Playing: return QStringLiteral("playing");
    case PlaybackObserver::State::Paused: return QStringLiteral("paused");
    case PlaybackObserver::State::Stopped: break;
    }
    return QStringLiteral("stopped");
}
} // namespace

void PlexReporter::trackStarted(const Track &track)
{
    // Close out the previous Plex track if nobody else did.
    if (m_current.isValid() && m_lastState != QLatin1String("stopped"))
        sendTimeline(QStringLiteral("stopped"));

    m_current = track.sourceId == QLatin1String(SourceId) ? parseTrackLocation(track.location) : TrackRef();
    if (m_current.ratingKey.isEmpty())
        m_current = {}; // nothing to report against
    m_positionMs = 0;
    m_durationMs = qMax<qint64>(0, track.durationMs);
    m_scrobbled = false;
    m_lastState.clear();
    if (m_current.isValid())
        sendTimeline(QStringLiteral("playing"));
}

void PlexReporter::playbackStateChanged(State state)
{
    const QString name = stateName(state);
    if (m_current.isValid() && name != m_lastState)
        sendTimeline(name);
}

void PlexReporter::positionChanged(qint64 positionMs, qint64 durationMs)
{
    if (!m_current.isValid())
        return;
    m_positionMs = positionMs;
    if (durationMs > 0)
        m_durationMs = durationMs;
    if (!m_scrobbled && m_durationMs > 0 && m_positionMs >= m_durationMs * PlayedFraction)
        scrobble();
    if (m_lastState == QLatin1String("playing") && m_sinceReport.hasExpired(ReportIntervalMs))
        sendTimeline(m_lastState);
}

void PlexReporter::trackFinished(const Track &track)
{
    Q_UNUSED(track)
    if (!m_current.isValid())
        return;
    if (!m_scrobbled)
        scrobble();
    if (m_durationMs > 0)
        m_positionMs = m_durationMs;
    if (m_lastState != QLatin1String("stopped"))
        sendTimeline(QStringLiteral("stopped"));
    m_current = {};
}

void PlexReporter::sendTimeline(const QString &state)
{
    m_lastState = state;
    m_sinceReport.start();
    m_send(m_current.machineIdentifier, timelinePath(m_current.ratingKey, state, m_positionMs, m_durationMs));
}

void PlexReporter::scrobble()
{
    m_scrobbled = true;
    m_send(m_current.machineIdentifier, scrobblePath(m_current.ratingKey));
}

} // namespace newamp::plex

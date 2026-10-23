#include "JellyfinReporter.h"

namespace newamp::jellyfin {

namespace {
constexpr qint64 ReportIntervalMs = 10000;
}

void JellyfinReporter::trackStarted(const Track &track)
{
    // Close out the previous Jellyfin track if nobody else did.
    if (!m_itemId.isEmpty())
        stopped();

    if (track.sourceId == QLatin1String(SourceId))
        m_itemId = parseTrackLocation(track.location).itemId;
    m_paused = false;
    m_positionMs = 0;
    m_durationMs = qMax<qint64>(0, track.durationMs);
    if (m_itemId.isEmpty())
        return;
    m_sinceReport.start();
    m_send(QStringLiteral("/Sessions/Playing"), playingBody(m_itemId, 0, false));
}

void JellyfinReporter::playbackStateChanged(State state)
{
    if (m_itemId.isEmpty())
        return;
    switch (state) {
    case State::Paused:
        if (!m_paused) {
            m_paused = true;
            progress(QStringLiteral("pause"));
        }
        break;
    case State::Playing:
        if (m_paused) {
            m_paused = false;
            progress(QStringLiteral("unpause"));
        }
        break;
    case State::Stopped: stopped(); break;
    }
}

void JellyfinReporter::positionChanged(qint64 positionMs, qint64 durationMs)
{
    if (m_itemId.isEmpty())
        return;
    m_positionMs = positionMs;
    if (durationMs > 0)
        m_durationMs = durationMs;
    if (!m_paused && m_sinceReport.hasExpired(ReportIntervalMs))
        progress(QStringLiteral("timeupdate"));
}

void JellyfinReporter::trackFinished(const Track &track)
{
    Q_UNUSED(track)
    if (m_itemId.isEmpty())
        return;
    // Stopping at the very end is what marks the track played.
    if (m_durationMs > 0)
        m_positionMs = m_durationMs;
    stopped();
}

void JellyfinReporter::progress(const QString &event)
{
    m_sinceReport.start();
    m_send(QStringLiteral("/Sessions/Playing/Progress"), progressBody(m_itemId, m_positionMs, m_paused, event));
}

void JellyfinReporter::stopped()
{
    m_send(QStringLiteral("/Sessions/Playing/Stopped"), stoppedBody(m_itemId, m_positionMs));
    m_itemId.clear();
}

} // namespace newamp::jellyfin

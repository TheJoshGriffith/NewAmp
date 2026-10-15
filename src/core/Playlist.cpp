#include "core/Playlist.h"

namespace newamp {

const Track *Playlist::currentTrack() const
{
    return (m_current >= 0 && m_current < m_tracks.size()) ? &m_tracks.at(m_current) : nullptr;
}

void Playlist::setCurrentIndex(int index)
{
    if (index < -1 || index >= m_tracks.size() || index == m_current)
        return;
    m_current = index;
    Q_EMIT currentIndexChanged(m_current);
}

int Playlist::append(const QList<Track> &tracks)
{
    if (tracks.isEmpty())
        return -1;
    const int first = m_tracks.size();
    m_tracks += tracks;
    Q_EMIT tracksChanged();
    if (m_current < 0)
        setCurrentIndex(first);
    return first;
}

void Playlist::clear()
{
    m_tracks.clear();
    Q_EMIT tracksChanged();
    setCurrentIndex(-1);
}

void Playlist::updateTrack(int index, const Track &track)
{
    if (index < 0 || index >= m_tracks.size())
        return;
    m_tracks[index] = track;
    Q_EMIT tracksChanged();
}

bool Playlist::next()
{
    if (m_current + 1 >= m_tracks.size())
        return false;
    setCurrentIndex(m_current + 1);
    return true;
}

bool Playlist::previous()
{
    if (m_current <= 0)
        return false;
    setCurrentIndex(m_current - 1);
    return true;
}

} // namespace newamp

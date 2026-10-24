#include "core/Playlist.h"

#include <QRandomGenerator>

#include <algorithm>

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
    m_history.clear();
    m_shufflePick = -1;
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

void Playlist::remove(QList<int> indices)
{
    std::sort(indices.begin(), indices.end());
    indices.erase(std::unique(indices.begin(), indices.end()), indices.end());
    if (indices.isEmpty())
        return;

    int current = m_current;
    for (auto it = indices.crbegin(); it != indices.crend(); ++it) {
        const int index = *it;
        if (index < 0 || index >= m_tracks.size())
            continue;
        m_tracks.removeAt(index);
        if (index < current)
            --current;
        else if (index == current)
            current = -2; // removed; resolved below
    }
    m_history.clear();
    m_shufflePick = -1;
    Q_EMIT tracksChanged();

    if (current == -2)
        current = m_tracks.isEmpty() ? -1 : qMin(indices.first(), int(m_tracks.size()) - 1);
    // Indices shifted, so always announce the new cursor.
    m_current = current;
    Q_EMIT currentIndexChanged(m_current);
}

void Playlist::sortBy(const std::function<bool(const Track &, const Track &)> &lessThan)
{
    const Track *current = currentTrack();
    const QUrl currentLocation = current ? current->location : QUrl();
    std::stable_sort(m_tracks.begin(), m_tracks.end(), lessThan);
    m_history.clear();
    m_shufflePick = -1;
    Q_EMIT tracksChanged();
    if (current) {
        for (int i = 0; i < m_tracks.size(); ++i) {
            if (m_tracks.at(i).location == currentLocation) {
                m_current = i;
                break;
            }
        }
        Q_EMIT currentIndexChanged(m_current);
    }
}

void Playlist::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    m_history.clear();
    m_shufflePick = -1;
    Q_EMIT modesChanged();
}

void Playlist::setRepeat(bool repeat)
{
    if (repeat == m_repeat)
        return;
    m_repeat = repeat;
    Q_EMIT modesChanged();
}

int Playlist::upcoming() const
{
    if (m_tracks.isEmpty())
        return -1;
    if (m_shuffle) {
        if (m_tracks.size() == 1)
            return m_repeat ? 0 : -1;
        if (m_shufflePick < 0 || m_shufflePick >= m_tracks.size() || m_shufflePick == m_current) {
            int pick = m_current;
            while (pick == m_current)
                pick = int(QRandomGenerator::global()->bounded(m_tracks.size()));
            m_shufflePick = pick;
        }
        return m_shufflePick;
    }
    if (m_current + 1 >= m_tracks.size())
        return m_repeat ? 0 : -1;
    return m_current + 1;
}

bool Playlist::next()
{
    const int pick = upcoming();
    if (pick < 0)
        return false;
    if (m_shuffle) {
        if (m_tracks.size() == 1)
            return true;
        if (m_current >= 0)
            m_history << m_current;
        m_shufflePick = -1;
    }
    setCurrentIndex(pick);
    return true;
}

bool Playlist::previous()
{
    if (m_shuffle && !m_history.isEmpty()) {
        setCurrentIndex(m_history.takeLast());
        return true;
    }
    if (m_current <= 0) {
        if (!m_repeat || m_tracks.isEmpty())
            return false;
        setCurrentIndex(m_tracks.size() - 1);
        return true;
    }
    setCurrentIndex(m_current - 1);
    return true;
}

} // namespace newamp

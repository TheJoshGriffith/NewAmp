#include "core/Playlist.h"

#include <QFileInfo>
#include <QRandomGenerator>
#include <QSet>

#include <algorithm>
#include <numeric>

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
    if (!m_queue.isEmpty()) {
        m_queue.clear();
        Q_EMIT queueChanged();
    }
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
    const bool queued = !m_queue.isEmpty();
    for (auto it = indices.crbegin(); it != indices.crend(); ++it) {
        const int index = *it;
        if (index < 0 || index >= m_tracks.size())
            continue;
        m_tracks.removeAt(index);
        if (index < current)
            --current;
        else if (index == current)
            current = -2; // removed; resolved below
        m_queue.removeAll(index);
        for (int &q : m_queue)
            if (q > index)
                --q;
    }
    m_history.clear();
    Q_EMIT tracksChanged();
    if (queued)
        Q_EMIT queueChanged();

    if (current == -2)
        current = m_tracks.isEmpty() ? -1 : qMin(indices.first(), int(m_tracks.size()) - 1);
    // Indices shifted, so always announce the new cursor.
    m_current = current;
    Q_EMIT currentIndexChanged(m_current);
}

void Playlist::crop(const QList<int> &keep)
{
    const QSet<int> kept(keep.cbegin(), keep.cend());
    QList<int> drop;
    for (int i = 0; i < m_tracks.size(); ++i)
        if (!kept.contains(i))
            drop << i;
    remove(drop);
}

int Playlist::removeDuplicates()
{
    QSet<QPair<QString, QUrl>> seen;
    QList<int> drop;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &track = m_tracks.at(i);
        const QPair<QString, QUrl> key(track.sourceId, track.location);
        if (seen.contains(key))
            drop << i;
        else
            seen.insert(key);
    }
    remove(drop);
    return int(drop.size());
}

int Playlist::removeDeadFiles()
{
    // Only local files can be checked cheaply; other sources report failures
    // when they're played instead.
    QList<int> drop;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const QUrl &location = m_tracks.at(i).location;
        if (location.isLocalFile() && !QFileInfo::exists(location.toLocalFile()))
            drop << i;
    }
    remove(drop);
    return int(drop.size());
}

void Playlist::reorder(const QList<int> &order)
{
    QList<int> newIndex(order.size());
    QList<Track> tracks;
    tracks.reserve(order.size());
    for (int i = 0; i < order.size(); ++i) {
        tracks << m_tracks.at(order.at(i));
        newIndex[order.at(i)] = i;
    }
    m_tracks = tracks;
    for (int &q : m_queue)
        q = newIndex.at(q);
    m_history.clear();
    Q_EMIT tracksChanged();
    if (!m_queue.isEmpty())
        Q_EMIT queueChanged();
    if (m_current >= 0) {
        m_current = newIndex.at(m_current);
        Q_EMIT currentIndexChanged(m_current);
    }
}

static QList<int> identityOrder(int n)
{
    QList<int> order(n);
    std::iota(order.begin(), order.end(), 0);
    return order;
}

void Playlist::sortBy(const std::function<bool(const Track &, const Track &)> &lessThan)
{
    QList<int> order = identityOrder(int(m_tracks.size()));
    std::stable_sort(order.begin(), order.end(),
                     [&](int a, int b) { return lessThan(m_tracks.at(a), m_tracks.at(b)); });
    reorder(order);
}

void Playlist::reverse()
{
    QList<int> order = identityOrder(int(m_tracks.size()));
    std::reverse(order.begin(), order.end());
    reorder(order);
}

void Playlist::randomize()
{
    QList<int> order = identityOrder(int(m_tracks.size()));
    std::shuffle(order.begin(), order.end(), *QRandomGenerator::global());
    reorder(order);
}

QList<int> Playlist::search(const QString &query) const
{
    const QStringList words = query.split(QLatin1Char(' '), Qt::SkipEmptyParts);
    QList<int> rows;
    for (int i = 0; i < m_tracks.size(); ++i) {
        const Track &t = m_tracks.at(i);
        const QString haystack = t.title + QLatin1Char(' ') + t.artist + QLatin1Char(' ') + t.album +
                                 QLatin1Char(' ') + t.location.fileName();
        const bool match = std::all_of(words.cbegin(), words.cend(), [&](const QString &word) {
            return haystack.contains(word, Qt::CaseInsensitive);
        });
        if (match)
            rows << i;
    }
    return rows;
}

void Playlist::toggleQueued(const QList<int> &indices)
{
    QList<int> valid;
    for (int index : indices)
        if (index >= 0 && index < m_tracks.size() && !valid.contains(index))
            valid << index;
    if (valid.isEmpty())
        return;
    const bool allQueued =
        std::all_of(valid.cbegin(), valid.cend(), [this](int index) { return m_queue.contains(index); });
    for (int index : valid) {
        if (allQueued)
            m_queue.removeAll(index);
        else if (!m_queue.contains(index))
            m_queue << index;
    }
    Q_EMIT queueChanged();
}

void Playlist::clearQueue()
{
    if (m_queue.isEmpty())
        return;
    m_queue.clear();
    Q_EMIT queueChanged();
}

void Playlist::setShuffle(bool shuffle)
{
    if (shuffle == m_shuffle)
        return;
    m_shuffle = shuffle;
    m_history.clear();
    Q_EMIT modesChanged();
}

void Playlist::setRepeat(bool repeat)
{
    if (repeat == m_repeat)
        return;
    m_repeat = repeat;
    Q_EMIT modesChanged();
}

bool Playlist::next()
{
    if (m_tracks.isEmpty())
        return false;
    if (!m_queue.isEmpty()) {
        const int queued = m_queue.takeFirst();
        Q_EMIT queueChanged();
        if (m_shuffle && m_current >= 0)
            m_history << m_current;
        setCurrentIndex(queued);
        return true;
    }
    if (m_shuffle) {
        if (m_tracks.size() == 1)
            return m_repeat;
        int pick = m_current;
        while (pick == m_current)
            pick = int(QRandomGenerator::global()->bounded(m_tracks.size()));
        if (m_current >= 0)
            m_history << m_current;
        setCurrentIndex(pick);
        return true;
    }
    if (m_current + 1 >= m_tracks.size()) {
        if (!m_repeat)
            return false;
        setCurrentIndex(0);
        return true;
    }
    setCurrentIndex(m_current + 1);
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

#pragma once

#include "core/Track.h"

#include <QList>
#include <QObject>

#include <functional>

namespace newamp {

// The ordered list of tracks and the cursor into it. Pure data: no playback.
class Playlist : public QObject
{
    Q_OBJECT

public:
    using QObject::QObject;

    int count() const { return m_tracks.size(); }
    bool isEmpty() const { return m_tracks.isEmpty(); }
    const Track &at(int index) const { return m_tracks.at(index); }
    const QList<Track> &tracks() const { return m_tracks; }

    int currentIndex() const { return m_current; }
    const Track *currentTrack() const;
    void setCurrentIndex(int index);

    // Returns the index of the first appended track, or -1 if none.
    int append(const QList<Track> &tracks);
    void clear();
    void updateTrack(int index, const Track &track);
    // Removes the given rows; the current track survives if it isn't removed.
    void remove(QList<int> indices);
    // Removes every row except the given ones.
    void crop(const QList<int> &keep);
    // Remove repeats of the same location, keeping the first; and local files
    // that no longer exist. Return how many tracks went.
    int removeDuplicates();
    int removeDeadFiles();

    // Reordering keeps the current track and the queue pointing at the same
    // tracks.
    void sortBy(const std::function<bool(const Track &, const Track &)> &lessThan);
    void reverse();
    void randomize();

    // Rows whose title, artist, album or file name contain every word of
    // `query`, ignoring case, in playlist order. For "jump to file".
    QList<int> search(const QString &query) const;

    // Queued tracks play next, in queue order, ahead of shuffle and repeat.
    // Toggling queues the rows that aren't queued, or dequeues them all when
    // every one already is.
    void toggleQueued(const QList<int> &indices);
    void clearQueue();
    const QList<int> &queue() const { return m_queue; }
    // 1-based place in the queue, or 0 when not queued.
    int queuePosition(int index) const { return int(m_queue.indexOf(index)) + 1; }

    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    bool repeat() const { return m_repeat; }
    void setRepeat(bool repeat);

    // Move the cursor, honouring shuffle and repeat. Return false when there
    // is nowhere to go (the end of the list without repeat).
    bool next();
    bool previous();

Q_SIGNALS:
    void tracksChanged();
    void currentIndexChanged(int index);
    void modesChanged();
    void queueChanged();

private:
    // Rearranges the tracks so that row i holds what was at order[i].
    void reorder(const QList<int> &order);

    QList<Track> m_tracks;
    int m_current = -1;
    bool m_shuffle = false;
    bool m_repeat = false;
    QList<int> m_history; // tracks played in shuffle mode, for previous()
    QList<int> m_queue;
};

} // namespace newamp

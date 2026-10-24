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
    void sortBy(const std::function<bool(const Track &, const Track &)> &lessThan);

    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool shuffle);
    bool repeat() const { return m_repeat; }
    void setRepeat(bool repeat);

    // Move the cursor, honouring shuffle and repeat. Return false when there
    // is nowhere to go (the end of the list without repeat).
    bool next();
    bool previous();
    // Where next() will go, without moving: -1 at the end. In shuffle mode
    // the pick is made here and next() keeps to it.
    int upcoming() const;

Q_SIGNALS:
    void tracksChanged();
    void currentIndexChanged(int index);
    void modesChanged();

private:
    QList<Track> m_tracks;
    int m_current = -1;
    bool m_shuffle = false;
    bool m_repeat = false;
    QList<int> m_history; // tracks played in shuffle mode, for previous()
    mutable int m_shufflePick = -1; // the next shuffled track, once chosen
};

} // namespace newamp

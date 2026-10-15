#pragma once

#include "core/Track.h"

#include <QList>
#include <QObject>

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

    // Move the cursor. Return false at either end (no wrap-around yet).
    bool next();
    bool previous();

Q_SIGNALS:
    void tracksChanged();
    void currentIndexChanged(int index);

private:
    QList<Track> m_tracks;
    int m_current = -1;
};

} // namespace newamp

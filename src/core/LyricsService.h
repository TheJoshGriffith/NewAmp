#pragma once

#include "core/Lyrics.h"
#include "core/Track.h"

#include <QList>
#include <QObject>

namespace newamp {

class LyricsProvider;

// Finds lyrics for a track by asking each provider in turn until one has
// them. Only the latest request counts: answers for earlier tracks are
// dropped.
class LyricsService : public QObject
{
    Q_OBJECT

public:
    enum class State { Idle, Searching, Found, NotFound };

    using QObject::QObject;

    // Asked in the order added. Not owned; must outlive the service.
    void addProvider(LyricsProvider *provider) { m_providers.append(provider); }

    // Looks up lyrics for the track; lyricsChanged() follows, at once when
    // a provider answers synchronously.
    void request(const Track &track);
    // Forget the current track (nothing playing).
    void clear();
    // Use these lyrics for the current track, such as a file the user picked.
    void setLyrics(const QString &text, const QString &source);

    State state() const { return m_state; }
    const Lyrics &lyrics() const { return m_lyrics; }
    const Track &track() const { return m_track; }
    bool hasTrack() const { return m_hasTrack; }

Q_SIGNALS:
    void lyricsChanged();

private:
    void tryProvider(int index, quint64 generation);

    QList<LyricsProvider *> m_providers;
    Track m_track;
    bool m_hasTrack = false;
    Lyrics m_lyrics;
    State m_state = State::Idle;
    quint64 m_generation = 0;
};

} // namespace newamp

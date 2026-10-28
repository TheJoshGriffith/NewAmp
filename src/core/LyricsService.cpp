#include "core/LyricsService.h"

#include "core/LyricsProvider.h"

#include <QPointer>

namespace newamp {

void LyricsService::request(const Track &track)
{
    m_track = track;
    m_hasTrack = true;
    m_lyrics = {};
    m_state = State::Searching;
    const quint64 generation = ++m_generation;
    Q_EMIT lyricsChanged();
    tryProvider(0, generation);
}

void LyricsService::clear()
{
    ++m_generation;
    m_track = {};
    m_hasTrack = false;
    m_lyrics = {};
    m_state = State::Idle;
    Q_EMIT lyricsChanged();
}

void LyricsService::setLyrics(const QString &text, const QString &source)
{
    ++m_generation; // a search still running must not replace these
    m_lyrics = Lyrics::parse(text);
    m_lyrics.source = source;
    m_state = m_lyrics.isEmpty() ? State::NotFound : State::Found;
    Q_EMIT lyricsChanged();
}

void LyricsService::tryProvider(int index, quint64 generation)
{
    while (index < m_providers.size() && !m_providers.at(index)->canHandle(m_track))
        ++index;
    if (index >= m_providers.size()) {
        m_state = State::NotFound;
        Q_EMIT lyricsChanged();
        return;
    }
    LyricsProvider *provider = m_providers.at(index);
    QPointer<LyricsService> self(this);
    provider->fetch(m_track, [self, provider, index, generation](const QString &text) {
        if (!self || generation != self->m_generation)
            return;
        Lyrics lyrics = Lyrics::parse(text);
        if (lyrics.isEmpty()) {
            self->tryProvider(index + 1, generation);
            return;
        }
        lyrics.source = provider->name();
        self->m_lyrics = lyrics;
        self->m_state = State::Found;
        Q_EMIT self->lyricsChanged();
    });
}

} // namespace newamp

#include "plugins/AppPluginHost.h"

#include "core/PlaybackEngine.h"
#include "core/PlayerController.h"
#include "core/Playlist.h"

namespace newamp {

namespace {
bool sameTrack(const Track &a, const Track &b)
{
    return a.sourceId == b.sourceId && a.location == b.location;
}
} // namespace

template<typename Fn>
void AppPluginHost::notify(Fn fn)
{
    for (PlaybackObserver *observer : std::as_const(m_observers))
        fn(observer);
}

AppPluginHost::AppPluginHost(Playlist &playlist, PlaybackEngine &engine, QObject *parent)
    : QObject(parent)
    , m_playlist(playlist)
    , m_engine(engine)
{
    connect(&m_engine, &PlaybackEngine::stateChanged, this, [this](PlaybackEngine::State state) {
        PlaybackObserver::State mapped = PlaybackObserver::State::Stopped;
        switch (state) {
        case PlaybackEngine::State::Playing:
            // Resuming from pause keeps the same track; anything else is a start.
            startCurrentTrack(m_restart);
            mapped = PlaybackObserver::State::Playing;
            break;
        case PlaybackEngine::State::Paused:
            mapped = PlaybackObserver::State::Paused;
            break;
        case PlaybackEngine::State::Stopped:
            m_restart = true;
            break;
        }
        notify([mapped](PlaybackObserver *o) { o->playbackStateChanged(mapped); });
    });

    connect(&m_engine, &PlaybackEngine::positionChanged, this, [this](qint64 positionMs) {
        // A backend may switch tracks without passing through Stopped; the
        // first position report after the cursor moved catches that.
        if (m_indexChanged && m_engine.state() == PlaybackEngine::State::Playing)
            startCurrentTrack(false);
        qint64 durationMs = m_engine.duration();
        if (durationMs <= 0)
            durationMs = m_started ? m_started->durationMs : -1;
        notify([=](PlaybackObserver *o) { o->positionChanged(positionMs, durationMs); });
    });

    connect(&m_engine, &PlaybackEngine::endOfTrack, this, [this] {
        m_restart = true;
        if (!m_started)
            return;
        const Track finished = *m_started;
        m_started.reset();
        notify([&finished](PlaybackObserver *o) { o->trackFinished(finished); });
    });

    connect(&m_playlist, &Playlist::currentIndexChanged, this, [this] { m_indexChanged = true; });
}

void AppPluginHost::addTracks(const QList<Track> &tracks)
{
    m_playlist.append(tracks);
}

void AppPluginHost::playTracks(const QList<Track> &tracks)
{
    if (m_controller)
        m_controller->openTracks(tracks);
    else
        addTracks(tracks);
}

std::unique_ptr<QSettings> AppPluginHost::settings(const QString &pluginId) const
{
    auto settings = std::make_unique<QSettings>();
    settings->beginGroup(QStringLiteral("plugins/") + pluginId);
    return settings;
}

void AppPluginHost::addPlaybackObserver(PlaybackObserver *observer)
{
    if (observer && !m_observers.contains(observer))
        m_observers << observer;
}

void AppPluginHost::startCurrentTrack(bool force)
{
    m_indexChanged = false;
    const Track *current = m_playlist.currentTrack();
    if (!current || (!force && m_started && sameTrack(*m_started, *current)))
        return;
    m_restart = false;
    m_started = *current;
    const Track started = *current;
    notify([&started](PlaybackObserver *o) { o->trackStarted(started); });
}

} // namespace newamp

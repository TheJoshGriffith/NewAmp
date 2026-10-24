#include "core/PlayerController.h"

#include "core/LocalFileSource.h"
#include "core/PlaybackEngine.h"
#include "core/Playlist.h"
#include "core/PlaylistFile.h"

#include <QNetworkAccessManager>
#include <QNetworkReply>

namespace newamp {

PlayerController::PlayerController(Playlist &playlist, PlaybackEngine &engine, QObject *parent)
    : QObject(parent)
    , m_playlist(playlist)
    , m_engine(engine)
{
    connect(&m_engine, &PlaybackEngine::endOfTrack, this, [this] {
        if (m_playlist.next())
            playCurrent();
    });

    // Fill in tags as the backend reads them, so every view shows real names.
    connect(&m_engine, &PlaybackEngine::metaDataChanged, this,
            [this](const QString &title, const QString &artist, const QString &album) {
                const Track *current = m_playlist.currentTrack();
                if (!current)
                    return;
                Track updated = *current;
                if (!title.isEmpty())
                    updated.title = title;
                if (!artist.isEmpty())
                    updated.artist = artist;
                if (!album.isEmpty())
                    updated.album = album;
                m_playlist.updateTrack(m_playlist.currentIndex(), updated);
            });

    // A track that reports its length has loaded, so any earlier failure is gone.
    connect(&m_engine, &PlaybackEngine::durationChanged, this, [this](qint64 durationMs) {
        if (durationMs > 0)
            m_failures = 0;
        const Track *current = m_playlist.currentTrack();
        if (!current || durationMs <= 0 || (current->durationMs == durationMs && current->error.isEmpty()))
            return;
        Track updated = *current;
        updated.durationMs = durationMs;
        updated.error.clear();
        m_playlist.updateTrack(m_playlist.currentIndex(), updated);
    });

    // Keep the failure on the track, so the playlist can mark it and copy it.
    connect(&m_engine, &PlaybackEngine::errorOccurred, this, [this](const QString &message) {
        const Track *current = m_playlist.currentTrack();
        if (!current)
            return;
        if (current->error != message) {
            Track updated = *current;
            updated.error = message;
            m_playlist.updateTrack(m_playlist.currentIndex(), updated);
        }
        skipFailed();
    });
}

void PlayerController::skipFailed()
{
    // Stop once every track has failed in a row, rather than loop forever.
    if (++m_failures >= m_playlist.count()) {
        m_failures = 0;
        return;
    }
    // Queued, so a run of unavailable tracks doesn't recurse; and dropped if
    // the user has picked another track meanwhile.
    const int failed = m_playlist.currentIndex();
    QMetaObject::invokeMethod(
        this,
        [this, failed] {
            if (m_playlist.currentIndex() != failed || m_engine.state() == PlaybackEngine::State::Playing)
                return;
            if (m_playlist.next())
                playCurrent();
            else
                m_failures = 0;
        },
        Qt::QueuedConnection);
}

void PlayerController::play()
{
    m_failures = 0;
    switch (m_engine.state()) {
    case PlaybackEngine::State::Paused:
        m_engine.resume();
        break;
    case PlaybackEngine::State::Playing:
        // Classic behaviour: play while playing restarts the track.
        m_engine.seek(0);
        break;
    case PlaybackEngine::State::Stopped:
        if (m_playlist.currentIndex() < 0 && !m_playlist.isEmpty())
            m_playlist.setCurrentIndex(0);
        playCurrent();
        break;
    }
}

void PlayerController::playPause()
{
    if (m_engine.state() == PlaybackEngine::State::Playing)
        m_engine.pause();
    else
        play();
}

void PlayerController::seekBy(qint64 deltaMs)
{
    const qint64 duration = m_engine.duration();
    if (m_engine.state() == PlaybackEngine::State::Stopped || duration <= 0)
        return;
    m_engine.seek(qBound<qint64>(0, m_engine.position() + deltaMs, duration));
}

void PlayerController::changeVolume(float delta)
{
    m_engine.setVolume(m_engine.volume() + delta);
}

void PlayerController::pause()
{
    if (m_engine.state() == PlaybackEngine::State::Playing)
        m_engine.pause();
    else if (m_engine.state() == PlaybackEngine::State::Paused)
        m_engine.resume();
}

void PlayerController::stop()
{
    m_engine.stop();
}

void PlayerController::next()
{
    m_failures = 0;
    if (m_playlist.next() && m_engine.state() != PlaybackEngine::State::Stopped)
        playCurrent();
}

void PlayerController::previous()
{
    m_failures = 0;
    if (m_playlist.previous() && m_engine.state() != PlaybackEngine::State::Stopped)
        playCurrent();
}

void PlayerController::playIndex(int index)
{
    m_failures = 0;
    m_playlist.setCurrentIndex(index);
    playCurrent();
}

void PlayerController::openTracks(const QList<Track> &tracks)
{
    const int first = m_playlist.append(tracks);
    if (first >= 0)
        playIndex(first);
}

void PlayerController::openUrl(const QUrl &url, bool play)
{
    const auto add = [this, play](const QList<Track> &tracks) {
        if (play)
            openTracks(tracks);
        else
            m_playlist.append(tracks);
    };
    if (url.isLocalFile()) {
        add(LocalFileSource::tracksForPaths({url.toLocalFile()}));
        return;
    }
    const Track itself = PlaylistFile::trackForEntry(url.toString(), QUrl());
    const PlaylistFile::Format format = PlaylistFile::formatForPath(url.path());
    if (format == PlaylistFile::Format::Unknown || !PlaylistFile::isPortable(itself)) {
        add({itself});
        return;
    }

    // A playlist on the web: fetch it and open what it lists. If it turns
    // out to be an HLS stream, or isn't a list after all, play the address.
    constexpr qint64 MaxPlaylistBytes = 1 << 20;
    if (!m_network)
        m_network = new QNetworkAccessManager(this);
    QNetworkRequest request(url);
    request.setTransferTimeout(15000);
    QNetworkReply *reply = m_network->get(request);
    connect(reply, &QNetworkReply::readyRead, reply, [reply] {
        if (reply->bytesAvailable() > MaxPlaylistBytes)
            reply->abort(); // a stream named like a playlist
    });
    connect(reply, &QNetworkReply::finished, this, [reply, format, itself, add] {
        reply->deleteLater();
        const QByteArray data = reply->readAll();
        QList<Track> tracks;
        if (reply->error() == QNetworkReply::NoError && !PlaylistFile::isHlsStream(data))
            tracks = PlaylistFile::parse(data, format, reply->url());
        add(tracks.isEmpty() ? QList<Track>{itself} : tracks);
    });
}

void PlayerController::playCurrent()
{
    if (const Track *track = m_playlist.currentTrack())
        m_engine.play(*track);
}

} // namespace newamp

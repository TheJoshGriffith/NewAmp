#include "core/PlaybackEngine.h"

#include "core/MediaSource.h"

#include <QAudioOutput>
#include <QMediaMetaData>
#include <QMediaPlayer>

namespace newamp {

PlaybackEngine::PlaybackEngine(QObject *parent)
    : QObject(parent)
    , m_player(new QMediaPlayer(this))
    , m_output(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_output);
    m_output->setVolume(0.8f);

    connect(m_player, &QMediaPlayer::positionChanged, this, &PlaybackEngine::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &PlaybackEngine::durationChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            Q_EMIT endOfTrack();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        switch (s) {
        case QMediaPlayer::PlayingState: setState(State::Playing); break;
        case QMediaPlayer::PausedState: setState(State::Paused); break;
        case QMediaPlayer::StoppedState: setState(State::Stopped); break;
        }
    });
    connect(m_player, &QMediaPlayer::metaDataChanged, this, [this] {
        const QMediaMetaData md = m_player->metaData();
        QString artist = md.stringValue(QMediaMetaData::ContributingArtist);
        if (artist.isEmpty())
            artist = md.stringValue(QMediaMetaData::AlbumArtist);
        Q_EMIT metaDataChanged(md.stringValue(QMediaMetaData::Title), artist,
                               md.stringValue(QMediaMetaData::AlbumTitle));
        Q_EMIT bitrateChanged(md.value(QMediaMetaData::AudioBitRate).toInt() / 1000);
    });
    connect(m_player, &QMediaPlayer::errorOccurred, this,
            [this](QMediaPlayer::Error, const QString &message) { Q_EMIT errorOccurred(message); });
}

PlaybackEngine::~PlaybackEngine() = default;

void PlaybackEngine::registerSource(MediaSource *source)
{
    m_sources.insert(source->id(), source);
}

void PlaybackEngine::play(const Track &track)
{
    MediaSource *source = m_sources.value(track.sourceId);
    if (!source) {
        Q_EMIT errorOccurred(tr("No source registered for \"%1\"").arg(track.sourceId));
        return;
    }
    m_player->setSource(source->streamUrl(track));
    m_player->play();
}

void PlaybackEngine::resume()
{
    if (!m_player->source().isEmpty())
        m_player->play();
}

void PlaybackEngine::pause()
{
    m_player->pause();
}

void PlaybackEngine::stop()
{
    m_player->stop();
}

void PlaybackEngine::seek(qint64 positionMs)
{
    m_player->setPosition(positionMs);
}

qint64 PlaybackEngine::position() const
{
    return m_player->position();
}

qint64 PlaybackEngine::duration() const
{
    return m_player->duration();
}

float PlaybackEngine::volume() const
{
    return m_output->volume();
}

void PlaybackEngine::setVolume(float volume)
{
    m_output->setVolume(qBound(0.0f, volume, 1.0f));
}

void PlaybackEngine::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    Q_EMIT stateChanged(state);
}

} // namespace newamp

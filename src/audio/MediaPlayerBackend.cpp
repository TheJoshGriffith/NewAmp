#include "audio/MediaPlayerBackend.h"

#include <QAudioOutput>
#include <QMediaMetaData>
#include <QMediaPlayer>

namespace newamp {

MediaPlayerBackend::MediaPlayerBackend(QObject *parent)
    : AudioBackend(parent)
    , m_player(new QMediaPlayer(this))
    , m_output(new QAudioOutput(this))
{
    m_player->setAudioOutput(m_output);

    connect(m_player, &QMediaPlayer::positionChanged, this, &AudioBackend::positionChanged);
    connect(m_player, &QMediaPlayer::durationChanged, this, &AudioBackend::durationChanged);
    connect(m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia)
            Q_EMIT endOfTrack();
    });
    connect(m_player, &QMediaPlayer::playbackStateChanged, this, [this](QMediaPlayer::PlaybackState s) {
        switch (s) {
        case QMediaPlayer::PlayingState: Q_EMIT stateChanged(State::Playing); break;
        case QMediaPlayer::PausedState: Q_EMIT stateChanged(State::Paused); break;
        case QMediaPlayer::StoppedState: Q_EMIT stateChanged(State::Stopped); break;
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

bool MediaPlayerBackend::load(const QUrl &url)
{
    m_player->setSource(url);
    return true;
}

void MediaPlayerBackend::play()
{
    if (!m_player->source().isEmpty())
        m_player->play();
}

void MediaPlayerBackend::pause()
{
    m_player->pause();
}

void MediaPlayerBackend::stop()
{
    m_player->stop();
}

void MediaPlayerBackend::seek(qint64 positionMs)
{
    m_player->setPosition(positionMs);
}

qint64 MediaPlayerBackend::position() const
{
    return m_player->position();
}

qint64 MediaPlayerBackend::duration() const
{
    return m_player->duration();
}

void MediaPlayerBackend::setDspParams(const DspParams &params)
{
    m_output->setVolume(float(params.volume));
}

} // namespace newamp

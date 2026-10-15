#pragma once

#include "core/Track.h"

#include <QHash>
#include <QObject>

class QAudioOutput;
class QMediaPlayer;

namespace newamp {

class MediaSource;

// Thin wrapper over QMediaPlayer that resolves tracks through their source.
class PlaybackEngine : public QObject
{
    Q_OBJECT

public:
    enum class State { Stopped, Playing, Paused };
    Q_ENUM(State)

    explicit PlaybackEngine(QObject *parent = nullptr);
    ~PlaybackEngine() override;

    // The engine does not take ownership of sources.
    void registerSource(MediaSource *source);

    void play(const Track &track);
    void resume();
    void pause();
    void stop();
    void seek(qint64 positionMs);

    State state() const { return m_state; }
    qint64 position() const;
    qint64 duration() const;

    float volume() const;
    void setVolume(float volume);

Q_SIGNALS:
    void stateChanged(newamp::PlaybackEngine::State state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void metaDataChanged(const QString &title, const QString &artist, const QString &album);
    void bitrateChanged(int kbps); // 0 when unknown
    void endOfTrack();
    void errorOccurred(const QString &message);

private:
    void setState(State state);

    QMediaPlayer *m_player;
    QAudioOutput *m_output;
    QHash<QString, MediaSource *> m_sources;
    State m_state = State::Stopped;
};

} // namespace newamp

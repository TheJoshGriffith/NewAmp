#pragma once

#include "audio/DspRack.h"
#include "core/Track.h"

#include <QHash>
#include <QObject>

#include <optional>

namespace newamp {

class AudioBackend;
class DspBackend;
class EqualizerSettings;
class IcyReader;
class MediaSource;
class PlaybackSettings;
class RemoteBackend;
class RemotePlayer;

// Plays tracks: resolves them through their source, then hands them to the
// best backend. Local files go through NewAmp's own DSP pipeline (equalizer,
// balance, visualizer); streams, and files nothing can decode for it, through
// QMediaPlayer; tracks of sources with a RemotePlayer (Spotify), through that.
// A track whose source isn't available fails at once, with errorOccurred().
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
    // The track to play when this one ends. Where the backend can, it is
    // prepared ahead and joined without a gap (or crossfaded), and
    // trackAdvanced() is emitted instead of endOfTrack().
    void queueNext(const Track &track);
    void clearNext();
    void resume();
    void pause();
    void stop();
    void seek(qint64 positionMs);

    State state() const { return m_state; }
    qint64 position() const;
    qint64 duration() const;

    float volume() const { return m_volume; }
    void setVolume(float volume);
    double balance() const { return m_balance; }
    void setBalance(double balance); // -1 .. 1

    // The engine follows the settings' changes; it does not take ownership.
    void setEqualizer(EqualizerSettings *equalizer);
    void setPlaybackSettings(PlaybackSettings *settings);
    PlaybackSettings *playbackSettings() const { return m_playback; }

    // Plugin DSP effects, which play wherever the equalizer does.
    DspRack &dspRack() { return m_rack; }

    // Whether the current track can use the equalizer and balance.
    bool effectsAvailable() const;
    // Most recent mono samples for the visualizer; returns how many.
    int visualizerSamples(float *out, int count) const;

Q_SIGNALS:
    void stateChanged(newamp::PlaybackEngine::State state);
    void positionChanged(qint64 positionMs);
    void durationChanged(qint64 durationMs);
    void volumeChanged(float volume);
    void balanceChanged(double balance);
    void metaDataChanged(const QString &title, const QString &artist, const QString &album);
    void bitrateChanged(int kbps);                    // 0 when unknown
    void formatChanged(int sampleRate, int channels); // 0 when unknown
    void endOfTrack();
    // The queued track took over without stopping.
    void trackAdvanced();
    void errorOccurred(const QString &message);

private:
    void connectBackend(AudioBackend *backend);
    void start(const QUrl &url);
    void useMediaPlayer(const QUrl &url);
    void failed(const QString &message);
    void applyDsp();
    void applyPlayback();
    void setState(State state);

    DspRack m_rack;
    DspBackend *m_dsp;
    AudioBackend *m_media;
    RemoteBackend *m_remote;
    IcyReader *m_icy;
    RemotePlayer *m_remotePlayer = nullptr; // the current track's, if its source has one
    AudioBackend *m_active = nullptr;
    QHash<QString, MediaSource *> m_sources;
    EqualizerSettings *m_equalizer = nullptr;
    PlaybackSettings *m_playback = nullptr;
    std::optional<Track> m_queued; // handed to the active backend as its next track
    QUrl m_queuedFallback;
    State m_state = State::Stopped;
    bool m_loading = false;
    bool m_radio = false;            // the current track is a web stream the user opened
    std::optional<Track> m_pending; // asked for while m_loading
    QUrl m_fallback;                 // the current track's second chance, until used
    QString m_firstError;            // why the first attempt failed, once m_fallback is used
    float m_volume = 0.8f;
    double m_balance = 0.0;
};

} // namespace newamp

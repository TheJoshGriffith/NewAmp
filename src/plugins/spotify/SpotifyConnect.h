#pragma once

#include "SpotifyApi.h"
#include "core/RemotePlayer.h"

#include <QElapsedTimer>
#include <QObject>

#include <functional>

class QSettings;
class QTimer;

namespace newamp::spotify {

class SpotifyClient;

// Plays Spotify tracks, or whole playlists, on a Spotify Connect device (the Spotify app on this
// computer, a phone, a speaker) by remote control through the Web API, and
// follows that device's playback to report position and the end of each
// track. Needs Premium.
//
// Commands go out one at a time, in order, so a quick stop-then-play can't
// arrive at Spotify the other way round.
class SpotifyConnect final : public QObject, public RemotePlayer
{
    Q_OBJECT

public:
    SpotifyConnect(SpotifyClient &client, QSettings &settings, QObject *parent = nullptr);
    ~SpotifyConnect() override;

    void setEvents(Events *events) override { m_events = events; }
    void load(const QUrl &url) override;
    void play() override;
    void pause() override;
    void stop() override;
    void seek(qint64 positionMs) override;
    void setVolume(float volume) override;
    qint64 position() const override;
    qint64 duration() const override { return m_durationMs; }

    // The device to play on; empty means whichever is active, else the first.
    QString deviceId() const { return m_deviceId; }
    void setDeviceId(const QString &deviceId);
    // Asks Spotify which devices are open; done gets them (empty on failure).
    void listDevices(const std::function<void(const QList<Device> &devices)> &done);

private:
    struct Command
    {
        QByteArray verb;
        QString path; // without device_id, which is added when sent
        QByteArray body;
        bool startsTrack = false; // its failure is the track's failure
        bool namesDevice = true;  // add device_id to the query
        int pauseAfterMs = 0;     // before the next command goes out
    };

    Command startCommand() const;

    void enqueue(const Command &command);
    void sendNext();
    void sendWithDevice(const Command &command, bool mayFindDevice);
    void findDevice(const Command &command);
    void commandDone(const Command &command, int status, const QByteArray &body, bool mayFindDevice);
    void poll();
    void followPlayback(const Playback &playback);
    void setState(State state);
    void fail(const QString &message);
    void startPolling();
    void stopPolling();
    void ended();

    SpotifyClient &m_client;
    QSettings &m_settings;
    Events *m_events = nullptr;
    QTimer *m_poll;     // asks Spotify what is playing
    QTimer *m_tick;     // moves the position along between polls
    QTimer *m_volumeTimer; // volume changes are sent once the slider rests
    QList<Command> m_queue;
    bool m_sending = false;
    QString m_deviceId; // the user's choice, saved
    QString m_target;   // the device commands go to now
    QString m_uri;          // the loaded track, or whole playlist
    QString m_item;         // the song the device last reported for it
    bool m_started = false; // m_uri has been sent to the device
    bool m_stopPending = false;
    bool m_ended = false;   // endOfTrack sent for m_uri
    State m_state = State::Stopped;
    qint64 m_positionMs = 0; // at m_positionClock
    QElapsedTimer m_positionClock;
    qint64 m_durationMs = -1;
    int m_volumePercent = -1;
    int m_strayPolls = 0; // polls in a row where the device played something else
    bool m_woken = false; // playback was transferred to the device to start m_uri
    QElapsedTimer m_sinceCommand; // the device's state lags our commands
};

} // namespace newamp::spotify

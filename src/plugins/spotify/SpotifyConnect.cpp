#include "SpotifyConnect.h"

#include "SpotifyClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QSysInfo>
#include <QTimer>

#include <cmath>

namespace newamp::spotify {

namespace {
constexpr int PlayingPollMs = 1000;
constexpr int PausedPollMs = 3000;
constexpr int TickMs = 250;
constexpr int VolumeDelayMs = 300;
// How long the device's reported state may disagree with what we asked for.
constexpr qint64 CommandLagMs = 2500;
// Within this of the end, a track that stops or changes has finished.
constexpr qint64 EndSlackMs = 5000;
// Polls showing another track before NewAmp lets go of the device.
constexpr int StrayPollLimit = 3;
// How long a device gets to take over playback before it is asked to play.
constexpr int WakeMs = 1000;
// After the last track, how long to wait for another before pausing the
// device, which would otherwise go on with Spotify's autoplay.
constexpr int AfterLastTrackMs = 1500;

QString withQuery(const QString &path, const QString &key, const QString &value)
{
    return path + (path.contains(QLatin1Char('?')) ? QLatin1Char('&') : QLatin1Char('?')) + key + QLatin1Char('=')
         + QString::fromLatin1(QUrl::toPercentEncoding(value));
}
} // namespace

SpotifyConnect::SpotifyConnect(SpotifyClient &client, QSettings &settings, QObject *parent)
    : QObject(parent)
    , m_client(client)
    , m_settings(settings)
    , m_poll(new QTimer(this))
    , m_tick(new QTimer(this))
    , m_volumeTimer(new QTimer(this))
{
    m_deviceId = m_settings.value(QStringLiteral("device")).toString();
    m_target = m_deviceId;
    connect(m_poll, &QTimer::timeout, this, &SpotifyConnect::poll);
    m_tick->setInterval(TickMs);
    connect(m_tick, &QTimer::timeout, this, [this] {
        if (m_events)
            m_events->positionChanged(position());
    });
    m_volumeTimer->setSingleShot(true);
    m_volumeTimer->setInterval(VolumeDelayMs);
    connect(m_volumeTimer, &QTimer::timeout, this, [this] {
        enqueue({"PUT", QStringLiteral("/me/player/volume?volume_percent=%1").arg(m_volumePercent), {}, false});
    });
    m_sinceCommand.start();
}

SpotifyConnect::~SpotifyConnect() = default;

void SpotifyConnect::setDeviceId(const QString &deviceId)
{
    m_deviceId = deviceId;
    m_target = deviceId;
    m_settings.setValue(QStringLiteral("device"), deviceId);
}

void SpotifyConnect::setAppDevice(const QString &deviceId)
{
    if (deviceId == m_appDevice)
        return;
    // Spotify knows the new device from now on; target it unless the user
    // picked another.
    if (m_deviceId.isEmpty() && (m_target.isEmpty() || m_target == m_appDevice))
        m_target = deviceId;
    m_appDevice = deviceId;
}

void SpotifyConnect::load(const QUrl &url)
{
    // A new track replaces whatever hadn't gone out for the old one,
    // including the pause a stop() just before it would have sent.
    m_queue.clear();
    m_stopPending = false;
    m_uri = playableUri(url);
    m_item.clear();
    m_started = false;
    m_ended = false;
    m_strayPolls = 0;
    m_woken = false;
    m_positionMs = 0;
    m_positionClock.start();
    m_durationMs = -1;
    if (m_uri.isEmpty())
        fail(tr("Not a Spotify track: %1").arg(url.toString()));
}

void SpotifyConnect::play()
{
    if (m_uri.isEmpty())
        return;
    if (!m_started) {
        m_started = true;
        enqueue(startCommand());
    } else {
        enqueue({"PUT", QStringLiteral("/me/player/play"), {}, false});
    }
    m_positionMs = position();
    m_positionClock.start();
    setState(State::Playing);
    startPolling();
}

SpotifyConnect::Command SpotifyConnect::startCommand() const
{
    const QJsonObject body = isPlaylistUri(m_uri) ? QJsonObject{{QStringLiteral("context_uri"), m_uri}}
                                                  : QJsonObject{{QStringLiteral("uris"), QJsonArray{m_uri}}};
    Command command{"PUT", QStringLiteral("/me/player/play"), QJsonDocument(body).toJson(QJsonDocument::Compact)};
    command.startsTrack = true;
    return command;
}

void SpotifyConnect::pause()
{
    if (m_state != State::Playing)
        return;
    m_positionMs = position();
    enqueue({"PUT", QStringLiteral("/me/player/pause"), {}, false});
    setState(State::Paused);
    startPolling();
}

void SpotifyConnect::stop()
{
    if (m_state == State::Stopped)
        return;
    const bool wasPlaying = m_state == State::Playing;
    setState(State::Stopped);
    stopPolling();
    m_positionMs = 0;
    m_started = false; // play() after stop starts the track again
    if (!wasPlaying)
        return;
    // The engine stops the old track before loading the next one; only
    // pause the device if nothing else is loaded straight after.
    m_stopPending = true;
    QTimer::singleShot(0, this, [this] {
        if (std::exchange(m_stopPending, false))
            enqueue({"PUT", QStringLiteral("/me/player/pause"), {}, false});
    });
}

void SpotifyConnect::seek(qint64 positionMs)
{
    if (!m_started)
        return;
    positionMs = qMax<qint64>(0, positionMs);
    enqueue({"PUT", QStringLiteral("/me/player/seek?position_ms=%1").arg(positionMs), {}, false});
    m_positionMs = positionMs;
    m_positionClock.start();
    if (m_events)
        m_events->positionChanged(positionMs);
}

void SpotifyConnect::setVolume(float volume)
{
    const int percent = int(std::lround(qBound(0.0f, volume, 1.0f) * 100));
    if (percent == m_volumePercent)
        return;
    m_volumePercent = percent;
    m_volumeTimer->start();
}

qint64 SpotifyConnect::position() const
{
    qint64 ms = m_positionMs;
    if (m_state == State::Playing && m_positionClock.isValid())
        ms += m_positionClock.elapsed();
    return m_durationMs > 0 ? qMin(ms, m_durationMs) : ms;
}

void SpotifyConnect::listDevices(const std::function<void(const QList<Device> &devices)> &done)
{
    m_client.request("GET", QStringLiteral("/me/player/devices"), {}, this,
                     [done](int status, const QByteArray &body) {
                         done(status == 200 ? parseDevices(body) : QList<Device>());
                     });
}

void SpotifyConnect::enqueue(const Command &command)
{
    m_queue.append(command);
    if (!m_sending)
        sendNext();
}

void SpotifyConnect::sendNext()
{
    if (m_queue.isEmpty()) {
        m_sending = false;
        return;
    }
    m_sending = true;
    sendWithDevice(m_queue.takeFirst(), true);
}

void SpotifyConnect::sendWithDevice(const Command &command, bool mayFindDevice)
{
    // Name a device rather than let Spotify use the one it thinks is
    // active, which can be one that stopped answering long ago.
    if (m_target.isEmpty() && mayFindDevice) {
        findDevice(command);
        return;
    }
    m_sinceCommand.start();
    const QString path = m_target.isEmpty() || !command.namesDevice
                           ? command.path
                           : withQuery(command.path, QStringLiteral("device_id"), m_target);
    m_client.request(command.verb, path, command.body, this,
                     [this, command, mayFindDevice](int status, const QByteArray &body) {
                         commandDone(command, status, body, mayFindDevice);
                     });
}

void SpotifyConnect::commandDone(const Command &command, int status, const QByteArray &body, bool mayFindDevice)
{
    if (status >= 200 && status < 300) {
        if (command.pauseAfterMs > 0)
            QTimer::singleShot(command.pauseAfterMs, this, &SpotifyConnect::sendNext);
        else
            sendNext();
        return;
    }
    // The device we used went away: pick one that is open and try once more.
    if (status == 404 && mayFindDevice) {
        m_target.clear();
        findDevice(command);
        return;
    }
    const QString message = SpotifyClient::errorText(status, body);
    if (command.startsTrack)
        fail(message);
    else
        qWarning("NewAmp: Spotify %s %s: %s", command.verb.constData(), qPrintable(command.path),
                 qPrintable(message));
    sendNext();
}

void SpotifyConnect::findDevice(const Command &command)
{
    listDevices([this, command](const QList<Device> &devices) {
        const QString preferred = m_deviceId.isEmpty() ? m_appDevice : m_deviceId;
        const Device *pick = pickDevice(devices, preferred, QSysInfo::machineHostName());
        if (!pick) {
            commandDone(command, 404, R"({"error":{"status":404,"reason":"NO_ACTIVE_DEVICE"}})", false);
            return;
        }
        m_target = pick->id;
        sendWithDevice(command, false);
    });
}

void SpotifyConnect::startPolling()
{
    m_poll->start(m_state == State::Playing ? PlayingPollMs : PausedPollMs);
    if (m_state == State::Playing)
        m_tick->start();
    else
        m_tick->stop();
}

void SpotifyConnect::stopPolling()
{
    m_poll->stop();
    m_tick->stop();
}

void SpotifyConnect::poll()
{
    m_client.request("GET", QStringLiteral("/me/player"), {}, this, [this](int status, const QByteArray &body) {
        if (status == 200 || status == 204)
            followPlayback(parsePlayback(status == 200 ? body : QByteArray()));
    });
}

void SpotifyConnect::followPlayback(const Playback &playback)
{
    if (m_state == State::Stopped || m_uri.isEmpty() || m_ended)
        return;
    // Our commands are still on their way, so the device's state is stale.
    const bool settling = m_sending || m_sinceCommand.elapsed() < CommandLagMs;
    const bool nearEnd = m_durationMs > 0 && position() >= m_durationMs - EndSlackMs;

    if (SpotifyClient::debugEnabled())
        qInfo("Spotify player: valid %d playing %d at %lld on %s, item %s, context %s; want %s", playback.valid,
              playback.playing, playback.progressMs, qPrintable(playback.deviceId),
              qPrintable(playback.track.location.toString()), qPrintable(playback.contextUri), qPrintable(m_uri));
    // A whole playlist is followed by its context, a track by itself.
    const bool ours = playback.valid
                   && (isPlaylistUri(m_uri) ? playback.contextUri == m_uri
                                            : trackUri(playback.track.location) == m_uri);
    if (!ours) {
        if (settling)
            return;
        if (nearEnd && m_state == State::Playing) {
            ended(); // the device moved on to autoplay, or stopped
        } else if (++m_strayPolls >= StrayPollLimit && !m_woken && !m_target.isEmpty()) {
            // A Spotify app that has been idle can answer "204" to play and
            // then do nothing. Moving playback onto it wakes it; then ask
            // again.
            m_woken = true;
            m_strayPolls = 0;
            const QJsonObject wake{{QStringLiteral("device_ids"), QJsonArray{m_target}},
                                   {QStringLiteral("play"), false}};
            Command transfer{"PUT", QStringLiteral("/me/player"), QJsonDocument(wake).toJson(QJsonDocument::Compact)};
            transfer.namesDevice = false;
            transfer.pauseAfterMs = WakeMs;
            enqueue(transfer);
            enqueue(startCommand());
        } else if (m_strayPolls >= StrayPollLimit) {
            // The device never started the track, or someone played
            // something else on it: let it go, and say which.
            const Track &other = playback.track;
            if (!playback.valid)
                fail(tr("Spotify isn't playing on any device. Open Spotify on this computer or your phone, "
                        "choose it under \"Play on...\", and play again."));
            else if (other.location.isEmpty())
                fail(tr("Spotify accepted the track but didn't start it, even after waking the device. "
                        "Try playing it in the Spotify app: it may not be available in your country."));
            else
                fail(tr("Spotify is playing something else: %1").arg(other.displayTitle()));
        }
        return;
    }
    m_strayPolls = 0;

    // A track's names come from the device. A whole playlist keeps its own,
    // which the playlist would otherwise lose to each song in turn.
    const Track &track = playback.track;
    const QString item = trackUri(track.location);
    if (item != m_item) {
        m_item = item;
        if (m_events && !isPlaylistUri(m_uri))
            m_events->metaDataChanged(track.title, track.artist, track.album);
    }
    if (track.durationMs != m_durationMs) {
        m_durationMs = track.durationMs;
        if (m_events)
            m_events->durationChanged(m_durationMs);
    }
    if (settling)
        return;

    // A track that ran out stays loaded at its start or end, not playing.
    if (!playback.playing && m_state == State::Playing && nearEnd
        && (playback.progressMs == 0 || playback.progressMs >= m_durationMs - EndSlackMs)) {
        ended();
        return;
    }
    m_positionMs = playback.progressMs;
    m_positionClock.start();
    if (m_events)
        m_events->positionChanged(m_positionMs);
    // Paused or resumed from the Spotify app.
    if (playback.playing && m_state == State::Paused) {
        setState(State::Playing);
        startPolling();
    } else if (!playback.playing && m_state == State::Playing) {
        setState(State::Paused);
        startPolling();
    }
}

void SpotifyConnect::ended()
{
    m_ended = true;
    const QString uri = m_uri;
    if (m_events)
        m_events->endOfTrack();
    // The engine loads the next track from in there, if there is one.
    if (m_uri != uri || !m_ended)
        return;
    setState(State::Stopped);
    stopPolling();
    m_started = false;
    QTimer::singleShot(AfterLastTrackMs, this, [this, uri] {
        if (m_uri == uri && m_state == State::Stopped)
            enqueue({"PUT", QStringLiteral("/me/player/pause"), {}, false});
    });
}

void SpotifyConnect::setState(State state)
{
    if (state == m_state)
        return;
    m_state = state;
    if (m_events)
        m_events->stateChanged(state);
}

void SpotifyConnect::fail(const QString &message)
{
    setState(State::Stopped);
    stopPolling();
    m_started = false;
    qWarning("NewAmp: Spotify: %s", qPrintable(message));
    // The track shows the error too, but the library's status line is
    // where Spotify problems are looked for.
    Q_EMIT m_client.status(message);
    if (m_events)
        m_events->errorOccurred(message);
}

} // namespace newamp::spotify

#include "SpotifyConnect.h"

#include "SpotifyClient.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
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

void SpotifyConnect::load(const QUrl &url)
{
    // A new track replaces whatever hadn't gone out for the old one,
    // including the pause a stop() just before it would have sent.
    m_queue.clear();
    m_stopPending = false;
    m_uri = trackUri(url);
    m_started = false;
    m_ended = false;
    m_strayPolls = 0;
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
        const QJsonObject body{{QStringLiteral("uris"), QJsonArray{m_uri}}};
        enqueue({"PUT", QStringLiteral("/me/player/play"), QJsonDocument(body).toJson(QJsonDocument::Compact), true});
    } else {
        enqueue({"PUT", QStringLiteral("/me/player/play"), {}, false});
    }
    m_positionMs = position();
    m_positionClock.start();
    setState(State::Playing);
    startPolling();
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
    m_sinceCommand.start();
    const QString path = m_target.isEmpty() ? command.path
                                            : withQuery(command.path, QStringLiteral("device_id"), m_target);
    m_client.request(command.verb, path, command.body, this,
                     [this, command, mayFindDevice](int status, const QByteArray &body) {
                         commandDone(command, status, body, mayFindDevice);
                     });
}

void SpotifyConnect::commandDone(const Command &command, int status, const QByteArray &body, bool mayFindDevice)
{
    if (status >= 200 && status < 300) {
        sendNext();
        return;
    }
    // No device is active, or the one we used went away: pick one that is
    // open (the user's choice if it is, else the active one, else the
    // first) and try once more.
    if (status == 404 && mayFindDevice) {
        listDevices([this, command](const QList<Device> &devices) {
            const Device *pick = nullptr;
            for (const Device &d : devices) {
                if (d.restricted)
                    continue;
                if (d.id == m_deviceId) {
                    pick = &d;
                    break;
                }
                if (!pick || (d.active && !pick->active))
                    pick = &d;
            }
            if (!pick) {
                commandDone(command, 404, R"({"error":{"status":404,"reason":"NO_ACTIVE_DEVICE"}})", false);
                return;
            }
            m_target = pick->id;
            sendWithDevice(command, false);
        });
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

    if (!playback.valid || trackUri(playback.track.location) != m_uri) {
        if (settling)
            return;
        if (nearEnd && m_state == State::Playing) {
            ended(); // the device moved on to autoplay, or stopped
        } else if (++m_strayPolls >= StrayPollLimit) {
            // Someone played something else on the device: let it go.
            setState(State::Stopped);
            stopPolling();
        }
        return;
    }
    m_strayPolls = 0;

    const Track &track = playback.track;
    if (track.durationMs != m_durationMs) {
        const bool first = m_durationMs < 0;
        m_durationMs = track.durationMs;
        if (m_events) {
            m_events->durationChanged(m_durationMs);
            if (first)
                m_events->metaDataChanged(track.title, track.artist, track.album);
        }
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
    if (m_events)
        m_events->errorOccurred(message);
}

} // namespace newamp::spotify

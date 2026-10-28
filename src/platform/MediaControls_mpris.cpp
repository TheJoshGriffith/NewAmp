#include "platform/MediaControls.h"

#include "core/PlayerController.h"
#include "core/Playlist.h"

#include <QCoreApplication>
#include <QDBusAbstractAdaptor>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QStringList>
#include <QVariantMap>

// MPRIS, the D-Bus interface Linux desktops use for media keys, sound menus
// and lock screens: https://specifications.freedesktop.org/mpris-spec/latest/

namespace newamp {

namespace {

const QString ObjectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString RootInterface = QStringLiteral("org.mpris.MediaPlayer2");
const QString PlayerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");

qlonglong toMicroseconds(qint64 ms)
{
    return qlonglong(ms) * 1000;
}

// org.mpris.MediaPlayer2: the application itself.
class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")
    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    MprisRoot(QObject *parent, std::function<void()> raise)
        : QDBusAbstractAdaptor(parent)
        , m_raise(std::move(raise))
    {
    }

    bool canQuit() const { return true; }
    bool canRaise() const { return bool(m_raise); }
    bool hasTrackList() const { return false; }
    QString identity() const { return QStringLiteral("NewAmp"); }
    QString desktopEntry() const { return QStringLiteral("newamp"); }
    QStringList supportedUriSchemes() const { return {QStringLiteral("file")}; }
    QStringList supportedMimeTypes() const
    {
        return {QStringLiteral("audio/mpeg"), QStringLiteral("audio/flac"), QStringLiteral("audio/ogg"),
                QStringLiteral("audio/x-wav"), QStringLiteral("audio/mp4")};
    }

public Q_SLOTS:
    void Raise()
    {
        if (m_raise)
            m_raise();
    }
    void Quit() { QCoreApplication::quit(); }

private:
    std::function<void()> m_raise;
};

// org.mpris.MediaPlayer2.Player: transport, the current track and position.
class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")
    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(QString LoopStatus READ loopStatus WRITE setLoopStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(bool Shuffle READ shuffle WRITE setShuffle)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoPrevious)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPause)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    MprisPlayer(QObject *parent, PlayerController &controller)
        : QDBusAbstractAdaptor(parent)
        , m_controller(controller)
    {
        connect(&controller.engine(), &PlaybackEngine::volumeChanged, this,
                [this] { propertiesChanged({{QStringLiteral("Volume"), volume()}}); });
    }

    QString playbackStatus() const
    {
        switch (m_now.state) {
        case PlaybackEngine::State::Playing: return QStringLiteral("Playing");
        case PlaybackEngine::State::Paused: return QStringLiteral("Paused");
        case PlaybackEngine::State::Stopped: break;
        }
        return QStringLiteral("Stopped");
    }
    // NewAmp's repeat repeats the playlist.
    QString loopStatus() const
    {
        return m_controller.playlist().repeat() ? QStringLiteral("Playlist") : QStringLiteral("None");
    }
    void setLoopStatus(const QString &status) { m_controller.playlist().setRepeat(status != QLatin1String("None")); }
    double rate() const { return 1.0; }
    void setRate(double) {}
    bool shuffle() const { return m_controller.playlist().shuffle(); }
    void setShuffle(bool on) { m_controller.playlist().setShuffle(on); }
    double volume() const { return m_controller.engine().volume(); }
    void setVolume(double volume) { m_controller.engine().setVolume(float(qBound(0.0, volume, 1.0))); }
    qlonglong position() const
    {
        const PlaybackEngine &engine = m_controller.engine();
        return engine.state() == PlaybackEngine::State::Stopped ? 0 : toMicroseconds(engine.position());
    }
    bool canGoNext() const { return m_now.canGoNext; }
    bool canGoPrevious() const { return m_now.canGoPrevious; }
    bool canPlay() const { return m_now.hasTrack || !m_controller.playlist().isEmpty(); }
    bool canPause() const { return m_now.hasTrack; }
    bool canSeek() const { return m_now.canSeek; }
    bool canControl() const { return true; }

    QVariantMap metadata() const
    {
        QVariantMap map;
        if (!m_now.hasTrack)
            return map;
        map.insert(QStringLiteral("mpris:trackid"), QVariant::fromValue(trackId()));
        if (m_now.durationMs > 0)
            map.insert(QStringLiteral("mpris:length"), toMicroseconds(m_now.durationMs));
        map.insert(QStringLiteral("xesam:title"), m_now.title);
        if (!m_now.artist.isEmpty())
            map.insert(QStringLiteral("xesam:artist"), QStringList{m_now.artist});
        if (!m_now.album.isEmpty())
            map.insert(QStringLiteral("xesam:album"), m_now.album);
        if (const Track *track = m_controller.playlist().currentTrack(); track && track->location.isLocalFile())
            map.insert(QStringLiteral("xesam:url"), track->location.toString());
        return map;
    }

    void update(const NowPlaying &now)
    {
        const QVariantMap before = changedProperties();
        m_now = now;
        const QVariantMap after = changedProperties();
        QVariantMap changed;
        for (auto it = after.begin(); it != after.end(); ++it)
            if (before.value(it.key()) != it.value())
                changed.insert(it.key(), it.value());
        if (!changed.isEmpty())
            propertiesChanged(changed);
    }

    void seeked(const NowPlaying &now)
    {
        update(now);
        Q_EMIT Seeked(toMicroseconds(now.positionMs));
    }

public Q_SLOTS:
    void Next() { m_controller.next(); }
    void Previous() { m_controller.previous(); }
    void Pause()
    {
        if (m_controller.engine().state() == PlaybackEngine::State::Playing)
            m_controller.engine().pause();
    }
    void PlayPause() { m_controller.playPause(); }
    void Stop() { m_controller.stop(); }
    void Play()
    {
        if (m_controller.engine().state() != PlaybackEngine::State::Playing)
            m_controller.play();
    }
    void Seek(qlonglong offsetUs) { m_controller.seekBy(offsetUs / 1000); }
    void SetPosition(const QDBusObjectPath &track, qlonglong positionUs)
    {
        // Ignored unless it's still the same track, as the spec asks.
        const qint64 ms = positionUs / 1000;
        if (track == trackId() && ms >= 0 && (m_now.durationMs <= 0 || ms <= m_now.durationMs))
            m_controller.engine().seek(ms);
    }
    void OpenUri(const QString &) {}

Q_SIGNALS:
    void Seeked(qlonglong positionUs);

private:
    QDBusObjectPath trackId() const
    {
        // Changes with the playlist position, so a new track is a new id.
        const int index = qMax(0, m_controller.playlist().currentIndex());
        return QDBusObjectPath(QStringLiteral("/org/newamp/track/%1").arg(index));
    }

    // The properties that change on their own, and need announcing.
    QVariantMap changedProperties() const
    {
        return {
            {QStringLiteral("PlaybackStatus"), playbackStatus()},
            {QStringLiteral("LoopStatus"), loopStatus()},
            {QStringLiteral("Shuffle"), shuffle()},
            {QStringLiteral("Metadata"), metadata()},
            {QStringLiteral("CanGoNext"), canGoNext()},
            {QStringLiteral("CanGoPrevious"), canGoPrevious()},
            {QStringLiteral("CanPlay"), canPlay()},
            {QStringLiteral("CanPause"), canPause()},
            {QStringLiteral("CanSeek"), canSeek()},
        };
    }

    void propertiesChanged(const QVariantMap &changed)
    {
        QDBusMessage signal = QDBusMessage::createSignal(ObjectPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                         QStringLiteral("PropertiesChanged"));
        signal << PlayerInterface << changed << QStringList();
        QDBusConnection::sessionBus().send(signal);
    }

    PlayerController &m_controller;
    NowPlaying m_now;
};

class MprisBackend final : public MediaControlsBackend
{
public:
    MprisBackend(PlayerController &controller, const std::function<void()> &raise)
        : m_object(new QObject)
    {
        new MprisRoot(m_object.get(), raise);
        m_player = new MprisPlayer(m_object.get(), controller);
    }

    ~MprisBackend() override
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        bus.unregisterObject(ObjectPath);
        bus.unregisterService(m_service);
    }

    bool start()
    {
        QDBusConnection bus = QDBusConnection::sessionBus();
        if (!bus.isConnected() || !bus.registerObject(ObjectPath, m_object.get()))
            return false;
        // A second copy, when multiple instances are allowed, names itself apart.
        m_service = QStringLiteral("org.mpris.MediaPlayer2.newamp");
        if (!bus.registerService(m_service)) {
            m_service += QStringLiteral(".instance%1").arg(QCoreApplication::applicationPid());
            if (!bus.registerService(m_service))
                return false;
        }
        return true;
    }

    void update(const NowPlaying &now) override { m_player->update(now); }
    void seeked(const NowPlaying &now) override { m_player->seeked(now); }

private:
    std::unique_ptr<QObject> m_object;
    MprisPlayer *m_player;
    QString m_service;
};

} // namespace

std::unique_ptr<MediaControlsBackend> MediaControlsBackend::create(PlayerController &controller,
                                                                   const std::function<void()> &raise)
{
    auto backend = std::make_unique<MprisBackend>(controller, raise);
    if (!backend->start())
        return nullptr;
    return backend;
}

} // namespace newamp

#include "MediaControls_mpris.moc"

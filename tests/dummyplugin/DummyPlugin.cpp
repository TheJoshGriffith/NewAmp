#include "plugins/PluginHost.h"
#include "plugins/SourcePlugin.h"

#include <QAction>
#include <QObject>

namespace {

class DummySource final : public newamp::MediaSource
{
public:
    QString id() const override { return QStringLiteral("dummy"); }
    QString displayName() const override { return QStringLiteral("Dummy"); }
    QUrl streamUrl(const newamp::Track &track) const override { return track.location; }
};

} // namespace

class DummyPlugin : public QObject, public newamp::SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "dummy.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    void initialize(newamp::PluginHost &host) override
    {
        // One action that adds a track, so tests can see the host round trip.
        m_add = new QAction(QStringLiteral("Add dummy track"), this);
        connect(m_add, &QAction::triggered, this, [&host] {
            newamp::Track track;
            track.sourceId = QStringLiteral("dummy");
            track.title = QStringLiteral("Dummy");
            host.addTracks({track});
        });
    }

    newamp::MediaSource *source() override { return &m_source; }
    QList<QAction *> actions() override { return m_add ? QList<QAction *>{m_add} : QList<QAction *>{}; }

private:
    DummySource m_source;
    QAction *m_add = nullptr;
};

#include "DummyPlugin.moc"

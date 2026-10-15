#include "plugins/SourcePlugin.h"

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
    newamp::MediaSource *source() override { return &m_source; }

private:
    DummySource m_source;
};

#include "DummyPlugin.moc"

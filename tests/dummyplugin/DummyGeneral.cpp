#include "plugins/DspEffect.h"
#include "plugins/PluginHost.h"
#include "plugins/SourcePlugin.h"
#include "plugins/Visualization.h"

#include <QObject>

namespace {

class Silence final : public newamp::DspEffect
{
public:
    QString id() const override { return QStringLiteral("dummygeneral.silence"); }
    QString name() const override { return QStringLiteral("Silence"); }
    void configure(int, int channels) override { m_channels = channels; }
    void process(float *samples, qint64 frames) override { std::fill(samples, samples + frames * m_channels, 0.0f); }

private:
    int m_channels = 2;
};

class Black final : public newamp::Visualization
{
public:
    QString name() const override { return QStringLiteral("Black"); }
    void render(QImage &image, const newamp::VisualizationInput &) override { image.fill(Qt::black); }
};

} // namespace

// Offers no source: only an effect and a visualization.
class DummyGeneral : public QObject, public newamp::SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "general.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    void initialize(newamp::PluginHost &host) override
    {
        host.addDspEffect(&m_silence);
        host.addVisualization(&m_black);
    }

private:
    Silence m_silence;
    Black m_black;
};

#include "DummyGeneral.moc"

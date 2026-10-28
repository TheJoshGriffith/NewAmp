#include "SoundEffects.h"

#include "plugins/PluginHost.h"
#include "plugins/SourcePlugin.h"

namespace newamp::effects {

// A DSP plugin: offers its effects to the host and nothing else. They stay
// off until the user turns them on in the DSP effects menu.
class EffectsPlugin : public QObject, public SourcePlugin
{
    Q_OBJECT
    Q_PLUGIN_METADATA(IID NewAmpSourcePlugin_iid FILE "effects.json")
    Q_INTERFACES(newamp::SourcePlugin)

public:
    void initialize(PluginHost &host) override
    {
        host.addDspEffect(&m_vocals);
        host.addDspEffect(&m_widener);
        host.addDspEffect(&m_crossfeed);
    }

private:
    VocalRemover m_vocals;
    StereoWidener m_widener{this};
    Crossfeed m_crossfeed;
};

} // namespace newamp::effects

#include "EffectsPlugin.moc"

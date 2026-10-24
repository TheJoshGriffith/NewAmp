#pragma once

#include "ui/SkinnedWindow.h"

#include <array>

namespace newamp {

class EqualizerSettings;
class SkinSlider;
class SkinToggleButton;
class EqGraph;

// The classic ten-band equalizer window.
class EqualizerWindow : public SkinnedWindow
{
    Q_OBJECT

public:
    EqualizerWindow(const Skin *skin, EqualizerSettings &settings, QWidget *parent = nullptr);

    bool canShade() const override { return true; }

protected:
    void paintBackground(QPainter &painter, const Skin &skin) override;
    void paintShadeBackground(QPainter &painter, const Skin &skin) override;
    void shadeChanged() override;

private:
    void syncFromSettings();
    void syncFromEngine();
    void showPresets();

    EqualizerSettings &m_settings;
    SkinToggleButton *m_on;
    SkinToggleButton *m_auto;
    SkinSlider *m_preamp;
    std::array<SkinSlider *, 10> m_bands{};
    EqGraph *m_graph;
    SkinSlider *m_shadeVolume;
    SkinSlider *m_shadeBalance;
    bool m_followsEngine = false;
};

} // namespace newamp

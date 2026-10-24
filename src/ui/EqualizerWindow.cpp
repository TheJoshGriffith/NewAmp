#include "ui/EqualizerWindow.h"

#include "core/EqualizerSettings.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"

#include <QCursor>
#include <QMenu>
#include <QPainter>

#include <cmath>

namespace newamp {

using namespace sprites;

namespace {

// Slider value runs 0 (top, +12 dB) to 1 (bottom, -12 dB).
double toDb(double value)
{
    return (0.5 - value) * 2 * EqualizerSettings::MaxGain;
}

double toValue(double db)
{
    return 0.5 - db / (2 * EqualizerSettings::MaxGain);
}

QRect sliderBackground(double value)
{
    return eqSliderBackground(qRound((1.0 - value) * (EqSliderSteps - 1)));
}

} // namespace

// The little response curve above the sliders.
class EqGraph : public SkinElement
{
public:
    EqGraph(SkinnedWindow *window, EqualizerSettings &settings)
        : SkinElement(window, QRect(EqGraphPos, EqGraphBackground.size()))
        , m_settings(settings)
    {
    }

    void refresh() { update(); }

    void paint(QPainter &painter, const Skin &skin) const override
    {
        skin.drawSprite(painter, rect().topLeft(), Sheet::EqMain, EqGraphBackground);

        const int h = rect().height();
        const int preampY = qRound((1 - (m_settings.preamp() / EqualizerSettings::MaxGain + 1) / 2) * (h - 1));
        skin.drawSprite(painter, rect().topLeft() + QPoint(0, preampY), Sheet::EqMain, EqPreampLine);

        // Piecewise-linear curve through the band values, one pixel per
        // column, coloured from the skin's line colour strip by height.
        const int w = rect().width();
        const int n = EqualizerSettings::BandCount;
        for (int x = 0; x < w; ++x) {
            const double t = double(x) / (w - 1) * (n - 1);
            const int i = qMin(int(t), n - 2);
            const double db = m_settings.band(i) + (m_settings.band(i + 1) - m_settings.band(i)) * (t - i);
            const int y = qBound(0, qRound((1 - (db / EqualizerSettings::MaxGain + 1) / 2) * (h - 1)), h - 1);
            skin.drawSprite(painter, rect().topLeft() + QPoint(x, y), Sheet::EqMain,
                            QRect(EqGraphLineColors.left(), EqGraphLineColors.top() + y, 1, 1));
        }
    }

private:
    EqualizerSettings &m_settings;
};

EqualizerWindow::EqualizerWindow(const Skin *skin, EqualizerSettings &settings, QWidget *parent)
    : SkinnedWindow(skin, EqWindowSize, parent, Qt::Tool)
    , m_settings(settings)
{
    setObjectName(QStringLiteral("equalizer"));
    setWindowTitle(tr("Equalizer"));
    setAttribute(Qt::WA_MacAlwaysShowToolWindow);

    setCursorNames(QStringLiteral("eqnormal"), QStringLiteral("eqtitle"));
    addElement<SkinButton>(EqCloseButtonPos, Sheet::EqMain, EqCloseButton, [this] { hide(); })
        ->setCursorName(QStringLiteral("eqclose"));
    m_on = addElement<SkinToggleButton>(EqOnButtonPos, Sheet::EqMain, EqOnButton,
                                        [this](bool on) { m_settings.setEnabled(on); });
    m_auto = addElement<SkinToggleButton>(EqAutoButtonPos, Sheet::EqMain, EqAutoButton,
                                          [this](bool on) { m_settings.setAuto(on); });
    addElement<SkinButton>(EqPresetsButtonPos, Sheet::EqMain, EqPresetsButton, [this] { showPresets(); });
    m_graph = addElement<EqGraph>(m_settings);

    auto makeSlider = [this](const QPoint &pos) {
        auto *slider = addElement<SkinSlider>(QRect(pos, EqSliderSize), Sheet::EqMain, sliderBackground,
                                              EqSliderThumb, Qt::Vertical, QPoint(1, 0));
        slider->setCursorName(QStringLiteral("eqslid"));
        return slider;
    };
    m_preamp = makeSlider(EqPreampPos);
    m_preamp->onMoved = [this](double v) { m_settings.setPreamp(toDb(v)); };
    m_preamp->onReleased = m_preamp->onMoved;
    for (int band = 0; band < EqualizerSettings::BandCount; ++band) {
        m_bands[band] = makeSlider(eqBandPos(band));
        m_bands[band]->onMoved = [this, band](double v) { m_settings.setBand(band, toDb(v)); };
        m_bands[band]->onReleased = m_bands[band]->onMoved;
    }

    connect(&m_settings, &EqualizerSettings::changed, this, &EqualizerWindow::syncFromSettings);
    syncFromSettings();
}

void EqualizerWindow::paintBackground(QPainter &painter, const Skin &skin)
{
    skin.drawSprite(painter, {0, 0}, Sheet::EqMain, EqBackground);
    skin.drawSprite(painter, {0, 0}, Sheet::EqMain, isActiveWindow() ? EqTitleBarActive : EqTitleBarInactive);
}

void EqualizerWindow::syncFromSettings()
{
    m_on->setChecked(m_settings.isEnabled());
    m_auto->setChecked(m_settings.isAuto());
    m_preamp->setValue(toValue(m_settings.preamp()));
    for (int band = 0; band < EqualizerSettings::BandCount; ++band)
        m_bands[band]->setValue(toValue(m_settings.band(band)));
    m_graph->refresh();
}

void EqualizerWindow::showPresets()
{
    QMenu menu(this);
    for (const EqualizerSettings::Preset &preset : EqualizerSettings::builtInPresets())
        menu.addAction(preset.name, this, [this, preset] { m_settings.applyPreset(preset); });
    menu.exec(QCursor::pos());
}

} // namespace newamp

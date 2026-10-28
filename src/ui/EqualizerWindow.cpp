#include "ui/EqualizerWindow.h"

#include "core/EqfFile.h"
#include "core/EqualizerSettings.h"
#include "skin/Skin.h"
#include "ui/SkinElements.h"

#include <QCursor>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QInputDialog>
#include <QMenu>
#include <QMessageBox>
#include <QPainter>
#include <QSettings>

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

// Where preset files were last loaded from or saved to.
QString presetsDirectory()
{
    return QSettings().value(QStringLiteral("equalizer/lastDir")).toString();
}

void rememberDirectory(const QString &path)
{
    QSettings().setValue(QStringLiteral("equalizer/lastDir"), QFileInfo(path).absolutePath());
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

    addElement<SkinButton>(EqCloseButtonPos, Sheet::EqMain, EqCloseButton, [this] { hide(); });
    m_on = addElement<SkinToggleButton>(EqOnButtonPos, Sheet::EqMain, EqOnButton,
                                        [this](bool on) { m_settings.setEnabled(on); });
    m_auto = addElement<SkinToggleButton>(EqAutoButtonPos, Sheet::EqMain, EqAutoButton,
                                          [this](bool on) { m_settings.setAuto(on); });
    addElement<SkinButton>(EqPresetsButtonPos, Sheet::EqMain, EqPresetsButton, [this] { showPresets(); });
    m_graph = addElement<EqGraph>(m_settings);

    auto makeSlider = [this](const QPoint &pos) {
        return addElement<SkinSlider>(QRect(pos, EqSliderSize), Sheet::EqMain, sliderBackground, EqSliderThumb,
                                      Qt::Vertical, QPoint(1, 0));
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
    const bool hasTrack = !m_settings.currentTrack().isEmpty();
    const bool hasAuto = m_settings.hasAutoPreset(m_settings.currentTrack());

    QMenu *load = menu.addMenu(tr("Load"));
    for (const EqualizerSettings::Preset &preset : EqualizerSettings::builtInPresets())
        load->addAction(preset.name, this, [this, preset] { m_settings.applyPreset(preset); });
    if (!m_settings.userPresets().isEmpty()) {
        load->addSeparator();
        for (const EqualizerSettings::Preset &preset : m_settings.userPresets())
            load->addAction(preset.name, this, [this, preset] { m_settings.applyPreset(preset); });
    }
    load->addSeparator();
    load->addAction(tr("From file (.eqf, .q1)..."), this, &EqualizerWindow::importFile);

    QMenu *save = menu.addMenu(tr("Save"));
    save->addAction(tr("Preset..."), this, &EqualizerWindow::savePreset);
    save->addAction(tr("Auto-load preset for this track"), &m_settings, &EqualizerSettings::saveAutoPreset)
        ->setEnabled(hasTrack);
    save->addSeparator();
    save->addAction(tr("To file (.eqf)..."), this, [this] { exportFile(false); });
    save->addAction(tr("All my presets to a library (.q1)..."), this, [this] { exportFile(true); })
        ->setEnabled(!m_settings.userPresets().isEmpty());

    QMenu *remove = menu.addMenu(tr("Delete"));
    for (const EqualizerSettings::Preset &preset : m_settings.userPresets())
        remove->addAction(preset.name, this, [this, name = preset.name] { m_settings.deletePreset(name); });
    if (!m_settings.userPresets().isEmpty())
        remove->addSeparator();
    remove->addAction(tr("Auto-load preset for this track"), this, [this] { m_settings.deleteAutoPreset(); })
        ->setEnabled(hasAuto);

    menu.exec(QCursor::pos());
}

void EqualizerWindow::savePreset()
{
    bool ok = false;
    const QString name =
        QInputDialog::getText(this, tr("Save preset"), tr("Preset name:"), QLineEdit::Normal, {}, &ok).trimmed();
    if (ok && !name.isEmpty())
        m_settings.savePreset(m_settings.current(name));
}

void EqualizerWindow::importFile()
{
    const QString path = QFileDialog::getOpenFileName(this, tr("Load equalizer presets"), presetsDirectory(),
                                                      tr("Equalizer presets (*.eqf *.q1);;All files (*)"));
    if (path.isEmpty())
        return;
    rememberDirectory(path);
    QFile file(path);
    const QList<EqualizerSettings::Preset> presets =
        file.open(QIODevice::ReadOnly) ? eqf::read(file.readAll()) : QList<EqualizerSettings::Preset>{};
    if (presets.isEmpty()) {
        QMessageBox::warning(this, tr("Load equalizer presets"),
                             tr("%1 isn't a WinAmp equalizer preset file.").arg(QFileInfo(path).fileName()));
        return;
    }
    // Every preset in it joins the user's own; a single one is applied too.
    for (EqualizerSettings::Preset preset : presets) {
        if (preset.name.isEmpty())
            preset.name = QFileInfo(path).completeBaseName();
        m_settings.savePreset(preset);
    }
    if (presets.size() == 1)
        m_settings.applyPreset(presets.first());
}

void EqualizerWindow::exportFile(bool library)
{
    QList<EqualizerSettings::Preset> presets;
    QString suggested;
    if (library) {
        presets = m_settings.userPresets();
        suggested = QStringLiteral("NewAmp.q1");
    } else {
        bool ok = false;
        const QString name =
            QInputDialog::getText(this, tr("Save preset to file"), tr("Preset name:"), QLineEdit::Normal, {}, &ok)
                .trimmed();
        if (!ok || name.isEmpty())
            return;
        presets << m_settings.current(name);
        suggested = name + QStringLiteral(".eqf");
    }
    const QString dir = presetsDirectory();
    const QString path = QFileDialog::getSaveFileName(
        this, library ? tr("Save preset library") : tr("Save preset to file"),
        dir.isEmpty() ? suggested : dir + QLatin1Char('/') + suggested,
        library ? tr("Preset library (*.q1)") : tr("Equalizer preset (*.eqf)"));
    if (path.isEmpty())
        return;
    rememberDirectory(path);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(eqf::write(presets)) < 0)
        QMessageBox::warning(this, tr("Save equalizer presets"), tr("Couldn't write %1.").arg(path));
}

} // namespace newamp

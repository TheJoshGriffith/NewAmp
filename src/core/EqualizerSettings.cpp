#include "core/EqualizerSettings.h"

#include <QSettings>

namespace newamp {

namespace {
double clampGain(double db)
{
    return qBound(-EqualizerSettings::MaxGain, db, EqualizerSettings::MaxGain);
}
} // namespace

void EqualizerSettings::setEnabled(bool enabled)
{
    if (enabled == m_enabled)
        return;
    m_enabled = enabled;
    Q_EMIT changed();
}

void EqualizerSettings::setAuto(bool autoLoad)
{
    if (autoLoad == m_auto)
        return;
    m_auto = autoLoad;
    Q_EMIT changed();
}

void EqualizerSettings::setPreamp(double db)
{
    db = clampGain(db);
    if (qFuzzyCompare(db + 100, m_preamp + 100))
        return;
    m_preamp = db;
    Q_EMIT changed();
}

void EqualizerSettings::setBand(int index, double db)
{
    if (index < 0 || index >= BandCount)
        return;
    db = clampGain(db);
    if (qFuzzyCompare(db + 100, m_bands[index] + 100))
        return;
    m_bands[index] = db;
    Q_EMIT changed();
}

void EqualizerSettings::applyPreset(const Preset &preset)
{
    m_preamp = clampGain(preset.preamp);
    for (int i = 0; i < BandCount; ++i)
        m_bands[i] = clampGain(preset.bands[i]);
    Q_EMIT changed();
}

const QList<EqualizerSettings::Preset> &EqualizerSettings::builtInPresets()
{
    // NewAmp's own starting points; users will be able to save their own.
    static const QList<Preset> presets = {
        {QStringLiteral("Flat"), 0, {0, 0, 0, 0, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("Bass boost"), -3, {7, 6, 4, 2, 0, 0, 0, 0, 0, 0}},
        {QStringLiteral("Treble boost"), -3, {0, 0, 0, 0, 0, 1, 3, 5, 6, 7}},
        {QStringLiteral("Loudness"), -4, {6, 4, 1, 0, -1, 0, 1, 3, 5, 6}},
        {QStringLiteral("Vocal"), 0, {-3, -2, 0, 2, 4, 4, 3, 1, 0, -1}},
        {QStringLiteral("Rock"), -2, {5, 3, -2, -4, -1, 2, 4, 5, 5, 5}},
        {QStringLiteral("Electronic"), -2, {6, 5, 1, 0, -2, 1, 0, 2, 5, 6}},
        {QStringLiteral("Classical"), 0, {0, 0, 0, 0, 0, 0, -3, -4, -4, -5}},
    };
    return presets;
}

void EqualizerSettings::save(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("equalizer"));
    settings.setValue(QStringLiteral("enabled"), m_enabled);
    settings.setValue(QStringLiteral("auto"), m_auto);
    settings.setValue(QStringLiteral("preamp"), m_preamp);
    QVariantList bands;
    for (double b : m_bands)
        bands << b;
    settings.setValue(QStringLiteral("bands"), bands);
    settings.endGroup();
}

void EqualizerSettings::load(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("equalizer"));
    m_enabled = settings.value(QStringLiteral("enabled"), false).toBool();
    m_auto = settings.value(QStringLiteral("auto"), false).toBool();
    m_preamp = clampGain(settings.value(QStringLiteral("preamp"), 0.0).toDouble());
    const QVariantList bands = settings.value(QStringLiteral("bands")).toList();
    for (int i = 0; i < BandCount; ++i)
        m_bands[i] = i < bands.size() ? clampGain(bands.at(i).toDouble()) : 0.0;
    settings.endGroup();
    Q_EMIT changed();
}

} // namespace newamp

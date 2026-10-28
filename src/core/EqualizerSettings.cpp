#include "core/EqualizerSettings.h"

#include <QSettings>

namespace newamp {

namespace {
double clampGain(double db)
{
    return qBound(-EqualizerSettings::MaxGain, db, EqualizerSettings::MaxGain);
}

// A preset in settings: preamp, then the bands.
QVariantList toVariant(const EqualizerSettings::Preset &preset)
{
    QVariantList values{preset.preamp};
    for (double b : preset.bands)
        values << b;
    return values;
}

EqualizerSettings::Preset fromVariant(const QVariant &variant, const QString &name = {})
{
    const QVariantList values = variant.toList();
    EqualizerSettings::Preset preset{name, 0.0, {}};
    if (!values.isEmpty())
        preset.preamp = clampGain(values.first().toDouble());
    for (int i = 0; i < EqualizerSettings::BandCount; ++i)
        preset.bands[i] = i + 1 < values.size() ? clampGain(values.at(i + 1).toDouble()) : 0.0;
    return preset;
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
    if (m_auto) {
        loadAutoPreset(); // emits
    } else {
        if (m_autoLoaded)
            setValues(m_manual);
        m_autoLoaded = false;
        Q_EMIT changed();
    }
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

void EqualizerSettings::setValues(const Preset &preset)
{
    m_preamp = clampGain(preset.preamp);
    for (int i = 0; i < BandCount; ++i)
        m_bands[i] = clampGain(preset.bands[i]);
}

void EqualizerSettings::applyPreset(const Preset &preset)
{
    setValues(preset);
    Q_EMIT changed();
}

EqualizerSettings::Preset EqualizerSettings::current(const QString &name) const
{
    return {name, m_preamp, m_bands};
}

void EqualizerSettings::savePreset(const Preset &preset)
{
    for (Preset &existing : m_userPresets) {
        if (existing.name.compare(preset.name, Qt::CaseInsensitive) == 0) {
            existing = preset;
            return;
        }
    }
    m_userPresets << preset;
}

void EqualizerSettings::deletePreset(const QString &name)
{
    m_userPresets.removeIf([&name](const Preset &p) { return p.name.compare(name, Qt::CaseInsensitive) == 0; });
}

void EqualizerSettings::setCurrentTrack(const QString &key)
{
    if (key == m_track)
        return;
    m_track = key;
    if (m_auto)
        loadAutoPreset();
}

void EqualizerSettings::loadAutoPreset()
{
    const auto it = m_autoPresets.constFind(m_track);
    if (it != m_autoPresets.constEnd()) {
        if (!m_autoLoaded)
            m_manual = current();
        m_autoLoaded = true;
        setValues(*it);
    } else {
        if (m_autoLoaded)
            setValues(m_manual);
        m_autoLoaded = false;
        // What this track started with, in case the user saves a preset for it.
        m_manual = current();
    }
    Q_EMIT changed();
}

void EqualizerSettings::saveAutoPreset()
{
    if (m_track.isEmpty())
        return;
    m_autoPresets.insert(m_track, current(m_track));
    // With Auto on, leaving the track restores what it started with; with
    // Auto off, the preset waits for the next time Auto is on.
    m_autoLoaded = m_auto;
}

void EqualizerSettings::deleteAutoPreset()
{
    if (m_autoPresets.remove(m_track) && m_autoLoaded) {
        m_autoLoaded = false;
        setValues(m_manual);
        Q_EMIT changed();
    }
}

const QList<EqualizerSettings::Preset> &EqualizerSettings::builtInPresets()
{
    // NewAmp's own starting points; users save their own alongside.
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
    // A track's auto-load preset isn't what the user set.
    const Preset manual = m_autoLoaded ? m_manual : current();
    settings.setValue(QStringLiteral("preamp"), manual.preamp);
    QVariantList bands;
    for (double b : manual.bands)
        bands << b;
    settings.setValue(QStringLiteral("bands"), bands);

    QVariantMap presets;
    for (const Preset &preset : m_userPresets)
        presets.insert(preset.name, toVariant(preset));
    settings.setValue(QStringLiteral("presets"), presets);
    QVariantMap autoPresets;
    for (auto it = m_autoPresets.cbegin(); it != m_autoPresets.cend(); ++it)
        autoPresets.insert(it.key(), toVariant(*it));
    settings.setValue(QStringLiteral("autoPresets"), autoPresets);
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

    m_userPresets.clear();
    const QVariantMap presets = settings.value(QStringLiteral("presets")).toMap();
    for (auto it = presets.cbegin(); it != presets.cend(); ++it)
        m_userPresets << fromVariant(*it, it.key());
    m_autoPresets.clear();
    const QVariantMap autoPresets = settings.value(QStringLiteral("autoPresets")).toMap();
    for (auto it = autoPresets.cbegin(); it != autoPresets.cend(); ++it)
        m_autoPresets.insert(it.key(), fromVariant(*it, it.key()));
    m_autoLoaded = false;
    settings.endGroup();
    Q_EMIT changed();
}

} // namespace newamp

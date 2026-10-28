#pragma once

#include <QHash>
#include <QList>
#include <QObject>
#include <QString>

#include <array>

class QSettings;

namespace newamp {

// Equalizer state: on/off, preamp and ten bands, each in dB from -12 to +12.
// The window edits this; the audio pipeline reads it.
//
// It also keeps the user's own presets, and auto-load presets: settings
// saved for one track that come back whenever that track plays while Auto
// is on. Leaving such a track (or turning Auto off) restores what was set
// before it.
class EqualizerSettings : public QObject
{
    Q_OBJECT

public:
    static constexpr int BandCount = 10;
    static constexpr double MaxGain = 12.0;
    using Bands = std::array<double, BandCount>;

    struct Preset
    {
        QString name;
        double preamp;
        Bands bands;
    };

    using QObject::QObject;

    bool isEnabled() const { return m_enabled; }
    void setEnabled(bool enabled);
    bool isAuto() const { return m_auto; }
    void setAuto(bool autoLoad);

    double preamp() const { return m_preamp; }
    void setPreamp(double db);
    double band(int index) const { return m_bands.at(index); }
    void setBand(int index, double db);
    const Bands &bands() const { return m_bands; }

    void applyPreset(const Preset &preset);
    Preset current(const QString &name = {}) const;
    static const QList<Preset> &builtInPresets();

    // The user's saved presets, by name; saving under an existing name
    // replaces it.
    const QList<Preset> &userPresets() const { return m_userPresets; }
    void savePreset(const Preset &preset);
    void deletePreset(const QString &name);

    // The playing track, as a key that identifies it (its location). With
    // Auto on, a new track loads its auto-load preset.
    void setCurrentTrack(const QString &key);
    const QString &currentTrack() const { return m_track; }
    bool hasAutoPreset(const QString &key) const { return m_autoPresets.contains(key); }
    void saveAutoPreset(); // the current settings, for the current track
    void deleteAutoPreset();

    void save(QSettings &settings) const;
    void load(QSettings &settings);

Q_SIGNALS:
    void changed();

private:
    void loadAutoPreset();
    void setValues(const Preset &preset);

    bool m_enabled = false;
    bool m_auto = false;
    double m_preamp = 0.0;
    Bands m_bands{};

    QList<Preset> m_userPresets;
    QHash<QString, Preset> m_autoPresets;
    QString m_track;
    bool m_autoLoaded = false; // the values come from the track's auto preset
    Preset m_manual;           // what to restore once they no longer do
};

} // namespace newamp

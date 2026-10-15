#pragma once

#include <QList>
#include <QObject>
#include <QString>

#include <array>

class QSettings;

namespace newamp {

// Equalizer state: on/off, preamp and ten bands, each in dB from -12 to +12.
// The window edits this; the audio pipeline will read it.
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
    static const QList<Preset> &builtInPresets();

    void save(QSettings &settings) const;
    void load(QSettings &settings);

Q_SIGNALS:
    void changed();

private:
    bool m_enabled = false;
    bool m_auto = false;
    double m_preamp = 0.0;
    Bands m_bands{};
};

} // namespace newamp

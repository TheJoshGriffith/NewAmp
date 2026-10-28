#include "core/EqfFile.h"

#include <cmath>

namespace newamp::eqf {

namespace {

const QByteArray Header = QByteArrayLiteral("Winamp EQ library file v1.1\x1a!--");
constexpr int NameSize = 257;
constexpr int ValueCount = EqualizerSettings::BandCount + 1;
constexpr int EntrySize = NameSize + ValueCount;
constexpr double Centre = 31.5;

double toDb(quint8 value)
{
    const double db = (Centre - qMin<int>(value, 63)) / Centre * EqualizerSettings::MaxGain;
    // 0 dB falls between two steps; treat both as flat.
    return std::abs(db) < 0.2 ? 0.0 : std::round(db * 10) / 10;
}

char toValue(double db)
{
    return char(qBound(0, int(std::lround(Centre - db / EqualizerSettings::MaxGain * Centre)), 63));
}

} // namespace

QList<EqualizerSettings::Preset> read(const QByteArray &data)
{
    QList<EqualizerSettings::Preset> presets;
    if (!data.startsWith(Header))
        return presets;
    for (qsizetype at = Header.size(); at + EntrySize <= data.size(); at += EntrySize) {
        const QByteArray name = data.mid(at, NameSize);
        EqualizerSettings::Preset preset;
        preset.name = QString::fromLatin1(name.left(name.indexOf('\0'))).trimmed();
        const auto *values = reinterpret_cast<const quint8 *>(data.constData() + at + NameSize);
        for (int i = 0; i < EqualizerSettings::BandCount; ++i)
            preset.bands[i] = toDb(values[i]);
        preset.preamp = toDb(values[EqualizerSettings::BandCount]);
        presets << preset;
    }
    return presets;
}

QByteArray write(const QList<EqualizerSettings::Preset> &presets)
{
    QByteArray data = Header;
    for (const EqualizerSettings::Preset &preset : presets) {
        QByteArray name = preset.name.toLatin1().left(NameSize - 1);
        name.resize(NameSize, '\0');
        data += name;
        for (double band : preset.bands)
            data += toValue(band);
        data += toValue(preset.preamp);
    }
    return data;
}

} // namespace newamp::eqf

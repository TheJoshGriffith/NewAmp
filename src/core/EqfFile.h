#pragma once

#include "core/EqualizerSettings.h"

#include <QByteArray>
#include <QList>

namespace newamp::eqf {

// WinAmp's equalizer preset files. A .eqf holds one preset and a .q1
// library holds many, in the same format: a header, then for each preset a
// 257-byte name and eleven bytes (ten bands, then preamp), each 0 (+12 dB)
// to 63 (-12 dB).

// The presets in a file; empty if it isn't one.
QList<EqualizerSettings::Preset> read(const QByteArray &data);
QByteArray write(const QList<EqualizerSettings::Preset> &presets);

} // namespace newamp::eqf

#include "audio/ReplayGain.h"

#include <QRegularExpression>

#include <cmath>

namespace newamp {

namespace {

// "-6.52 dB", "+1.3dB", "0.988": the leading number.
std::optional<double> leadingNumber(const QString &value)
{
    static const QRegularExpression Number(QStringLiteral("^\\s*([-+]?(?:\\d+\\.?\\d*|\\.\\d+))"));
    const QRegularExpressionMatch match = Number.match(value);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double number = match.captured(1).toDouble(&ok);
    if (!ok || !std::isfinite(number))
        return std::nullopt;
    return number;
}

} // namespace

bool ReplayGainInfo::assign(const QString &key, const QString &value)
{
    const QString name = key.trimmed().toUpper();
    std::optional<double> *field = nullptr;
    bool r128 = false;
    if (name == QLatin1String("REPLAYGAIN_TRACK_GAIN"))
        field = &trackGain;
    else if (name == QLatin1String("REPLAYGAIN_TRACK_PEAK"))
        field = &trackPeak;
    else if (name == QLatin1String("REPLAYGAIN_ALBUM_GAIN"))
        field = &albumGain;
    else if (name == QLatin1String("REPLAYGAIN_ALBUM_PEAK"))
        field = &albumPeak;
    else if (name == QLatin1String("R128_TRACK_GAIN"))
        field = &trackGain, r128 = true;
    else if (name == QLatin1String("R128_ALBUM_GAIN"))
        field = &albumGain, r128 = true;
    else
        return false;

    std::optional<double> number = leadingNumber(value);
    if (!number)
        return true;
    // Opus (RFC 7845): a Q7.8 integer against -23 LUFS; ReplayGain's
    // reference is 5 dB louder.
    if (r128)
        number = *number / 256.0 + 5.0;
    // A ReplayGain tag takes precedence over an R128 one.
    if (!r128 || !*field)
        *field = number;
    return true;
}

float replayGainFactor(const ReplayGainInfo &info, const PlaybackOptions &options)
{
    using Mode = PlaybackOptions::ReplayGain;
    if (options.replayGain == Mode::Off)
        return 1.0f;
    // Album mode falls back to the track's gain, and track mode to the album's.
    const bool useAlbum = options.replayGain == Mode::Album ? bool(info.albumGain) : !info.trackGain;
    const std::optional<double> gain = useAlbum ? info.albumGain : info.trackGain;
    if (!gain)
        return 1.0f;
    const std::optional<double> peak = useAlbum ? (info.albumPeak ? info.albumPeak : info.trackPeak)
                                                : (info.trackPeak ? info.trackPeak : info.albumPeak);

    double factor = std::pow(10.0, (*gain + options.replayGainPreampDb) / 20.0);
    if (options.preventClipping && peak && *peak > 0.0 && factor * *peak > 1.0)
        factor = 1.0 / *peak;
    return float(factor);
}

} // namespace newamp

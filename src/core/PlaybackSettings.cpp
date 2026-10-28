#include "core/PlaybackSettings.h"

#include <QSettings>

namespace newamp {

template<typename T>
void PlaybackSettings::set(T &field, const T &value)
{
    if (field == value)
        return;
    field = value;
    Q_EMIT changed();
}

void PlaybackSettings::setGapless(bool gapless)
{
    set(m_options.gapless, gapless);
}

void PlaybackSettings::setCrossfadeMs(int ms)
{
    set(m_options.crossfadeMs, qBound(0, ms, MaxCrossfadeMs));
}

void PlaybackSettings::setReplayGain(PlaybackOptions::ReplayGain mode)
{
    set(m_options.replayGain, mode);
}

void PlaybackSettings::setReplayGainPreamp(double db)
{
    set(m_options.replayGainPreampDb, qBound(-MaxPreampDb, db, MaxPreampDb));
}

void PlaybackSettings::setPreventClipping(bool prevent)
{
    set(m_options.preventClipping, prevent);
}

void PlaybackSettings::setOutputDevice(const QByteArray &id)
{
    set(m_options.outputDevice, id);
}

void PlaybackSettings::save(QSettings &settings) const
{
    settings.beginGroup(QStringLiteral("playback"));
    settings.setValue(QStringLiteral("gapless"), m_options.gapless);
    settings.setValue(QStringLiteral("crossfadeMs"), m_options.crossfadeMs);
    settings.setValue(QStringLiteral("replayGain"), int(m_options.replayGain));
    settings.setValue(QStringLiteral("replayGainPreamp"), m_options.replayGainPreampDb);
    settings.setValue(QStringLiteral("preventClipping"), m_options.preventClipping);
    settings.setValue(QStringLiteral("outputDevice"), m_options.outputDevice);
    settings.endGroup();
}

void PlaybackSettings::load(QSettings &settings)
{
    settings.beginGroup(QStringLiteral("playback"));
    PlaybackOptions defaults;
    m_options.gapless = settings.value(QStringLiteral("gapless"), defaults.gapless).toBool();
    m_options.crossfadeMs = qBound(0, settings.value(QStringLiteral("crossfadeMs"), 0).toInt(), MaxCrossfadeMs);
    const int mode = settings.value(QStringLiteral("replayGain"), 0).toInt();
    m_options.replayGain = mode >= 0 && mode <= int(PlaybackOptions::ReplayGain::Album)
        ? PlaybackOptions::ReplayGain(mode)
        : PlaybackOptions::ReplayGain::Off;
    m_options.replayGainPreampDb =
        qBound(-MaxPreampDb, settings.value(QStringLiteral("replayGainPreamp"), 0.0).toDouble(), MaxPreampDb);
    m_options.preventClipping = settings.value(QStringLiteral("preventClipping"), defaults.preventClipping).toBool();
    m_options.outputDevice = settings.value(QStringLiteral("outputDevice")).toByteArray();
    settings.endGroup();
    Q_EMIT changed();
}

} // namespace newamp

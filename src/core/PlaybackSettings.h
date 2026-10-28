#pragma once

#include "audio/PlaybackOptions.h"

#include <QObject>

class QSettings;

namespace newamp {

// Crossfade, gapless, ReplayGain and the output device, as the user set
// them. The Playback menu edits this; the engine follows it.
class PlaybackSettings : public QObject
{
    Q_OBJECT

public:
    static constexpr int MaxCrossfadeMs = 10000;
    static constexpr double MaxPreampDb = 12.0;

    using QObject::QObject;

    const PlaybackOptions &options() const { return m_options; }

    void setGapless(bool gapless);
    void setCrossfadeMs(int ms);
    void setReplayGain(PlaybackOptions::ReplayGain mode);
    void setReplayGainPreamp(double db);
    void setPreventClipping(bool prevent);
    void setOutputDevice(const QByteArray &id);

    void save(QSettings &settings) const;
    void load(QSettings &settings);

Q_SIGNALS:
    void changed();

private:
    template<typename T>
    void set(T &field, const T &value);

    PlaybackOptions m_options;
};

} // namespace newamp

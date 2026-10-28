#pragma once

#include <QList>
#include <QStringList>
#include <QtGlobal>

#include <mutex>
#include <vector>

namespace newamp {

class DspEffect;

// The DSP effects plugins offer, and which of them are on. The UI changes it
// from the main thread while the audio thread runs process(), so one mutex
// guards it; process() holds it only for the length of one buffer.
class DspRack
{
public:
    // Not owned; the effect must outlive the rack's use of it.
    void add(DspEffect *effect, bool enabled = false);
    QList<DspEffect *> effects() const;

    bool isEnabled(const DspEffect *effect) const;
    void setEnabled(DspEffect *effect, bool enabled);
    QStringList enabledIds() const;

    // Audio thread: runs every enabled effect in order, configuring any
    // that are new or see a new format.
    void process(float *samples, qint64 frames, int sampleRate, int channels);
    void reset();

private:
    struct Slot
    {
        DspEffect *effect = nullptr;
        bool enabled = false;
        int sampleRate = 0; // what it was configured for; 0 when it needs configuring
        int channels = 0;
    };

    mutable std::mutex m_mutex;
    std::vector<Slot> m_slots;
};

} // namespace newamp

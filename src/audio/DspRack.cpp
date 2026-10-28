#include "audio/DspRack.h"

#include "plugins/DspEffect.h"

#include <algorithm>

namespace newamp {

void DspRack::add(DspEffect *effect, bool enabled)
{
    if (!effect)
        return;
    std::lock_guard lock(m_mutex);
    const bool known = std::any_of(m_slots.begin(), m_slots.end(), [effect](const Slot &s) { return s.effect == effect; });
    if (!known)
        m_slots.push_back(Slot{effect, enabled});
}

QList<DspEffect *> DspRack::effects() const
{
    std::lock_guard lock(m_mutex);
    QList<DspEffect *> effects;
    for (const Slot &slot : m_slots)
        effects << slot.effect;
    return effects;
}

bool DspRack::isEnabled(const DspEffect *effect) const
{
    std::lock_guard lock(m_mutex);
    for (const Slot &slot : m_slots)
        if (slot.effect == effect)
            return slot.enabled;
    return false;
}

void DspRack::setEnabled(DspEffect *effect, bool enabled)
{
    std::lock_guard lock(m_mutex);
    for (Slot &slot : m_slots) {
        if (slot.effect != effect || slot.enabled == enabled)
            continue;
        slot.enabled = enabled;
        slot.sampleRate = 0; // configure and start clean when it next runs
    }
}

QStringList DspRack::enabledIds() const
{
    std::lock_guard lock(m_mutex);
    QStringList ids;
    for (const Slot &slot : m_slots)
        if (slot.enabled)
            ids << slot.effect->id();
    return ids;
}

void DspRack::process(float *samples, qint64 frames, int sampleRate, int channels)
{
    std::lock_guard lock(m_mutex);
    for (Slot &slot : m_slots) {
        if (!slot.enabled)
            continue;
        if (slot.sampleRate != sampleRate || slot.channels != channels) {
            slot.effect->configure(sampleRate, channels);
            slot.effect->reset();
            slot.sampleRate = sampleRate;
            slot.channels = channels;
        }
        slot.effect->process(samples, frames);
    }
}

void DspRack::reset()
{
    std::lock_guard lock(m_mutex);
    for (Slot &slot : m_slots)
        if (slot.enabled && slot.sampleRate)
            slot.effect->reset();
}

} // namespace newamp

#include "audio/TrackMixer.h"

#include <algorithm>
#include <cmath>

namespace newamp {

namespace {

constexpr double HalfPi = 1.57079632679489661923;

void scale(float *samples, qint64 count, float gain)
{
    if (gain == 1.0f)
        return;
    for (qint64 i = 0; i < count; ++i)
        samples[i] *= gain;
}

} // namespace

TrackMixer::TrackMixer()
{
    m_retired.reserve(4); // so retiring rarely allocates on the audio thread
}

void TrackMixer::configure(int channels)
{
    m_channels = qMax(1, channels);
}

void TrackMixer::setCurrent(std::unique_ptr<PcmSource> source, float gain)
{
    retire(m_current);
    clearNext();
    stopFade();
    m_current = {std::move(source), gain};
    m_transitionFrame = -1;
}

void TrackMixer::setNext(std::unique_ptr<PcmSource> source, float gain)
{
    retire(m_next);
    m_next = {std::move(source), gain};
}

void TrackMixer::clearNext()
{
    retire(m_next);
}

void TrackMixer::stopFade()
{
    retire(m_fading);
    m_fadeLength = m_fadePosition = 0;
}

void TrackMixer::resetClock()
{
    m_rendered = 0;
    m_transitionFrame = -1;
}

std::vector<std::unique_ptr<PcmSource>> TrackMixer::takeRetired()
{
    std::vector<std::unique_ptr<PcmSource>> retired;
    retired.reserve(4);
    std::swap(retired, m_retired);
    return retired;
}

void TrackMixer::retire(Slot &slot)
{
    if (slot.source)
        m_retired.push_back(std::move(slot.source));
    slot = {};
}

void TrackMixer::advance(qint64 at)
{
    retire(m_current);
    m_current = std::move(m_next);
    m_next = {};
    m_transitionFrame = m_rendered + at;
}

qint64 TrackMixer::render(float *out, qint64 frames)
{
    const int channels = m_channels;
    qint64 produced = 0;
    m_fadeStart = 0;
    while (produced < frames) {
        float *at = out + produced * channels;
        qint64 want = frames - produced;

        // Crossfade: once the current track is within the fade of its end,
        // the next one takes over and the current one fades out under it.
        if (m_current.source && m_next.source && m_crossfadeFrames > 0 && !m_fading.source) {
            const qint64 length = m_current.source->lengthFrames();
            if (length > 0) {
                const qint64 remaining = length - m_current.source->positionFrames();
                if (remaining <= m_crossfadeFrames) {
                    m_fading = std::move(m_current);
                    m_current = {};
                    m_fadeLength = qMax<qint64>(1, remaining);
                    m_fadePosition = 0;
                    m_fadeStart = produced;
                    m_current = std::move(m_next);
                    m_next = {};
                    m_transitionFrame = m_rendered + produced;
                    continue;
                }
                want = qMin(want, remaining - m_crossfadeFrames);
            }
        }

        const qint64 got = m_current.source ? m_current.source->read(at, want) : 0;
        scale(at, got * channels, m_current.gain);
        produced += got;
        if (got == want)
            continue;

        if (m_current.source && !m_current.source->finished()) {
            // Still decoding: play silence, don't end.
            std::fill(out + produced * channels, out + (produced + want - got) * channels, 0.0f);
            produced += want - got;
            continue;
        }
        if (m_next.source) {
            advance(produced); // gapless
            continue;
        }
        if (m_fading.source) {
            // The new track ended inside the fade; let the old one finish.
            std::fill(out + produced * channels, out + frames * channels, 0.0f);
            produced = frames;
            break;
        }
        break;
    }

    mixFade(out, produced);
    m_rendered += produced;
    return produced;
}

void TrackMixer::mixFade(float *out, qint64 frames)
{
    if (!m_fading.source)
        return;
    const int channels = m_channels;
    const qint64 start = m_fadeStart;
    const qint64 count = qMin(frames - start, m_fadeLength - m_fadePosition);
    if (count <= 0)
        return;
    m_fadeScratch.resize(size_t(count * channels));
    qint64 got = m_fading.source->read(m_fadeScratch.data(), count);
    if (got < count)
        std::fill(m_fadeScratch.begin() + got * channels, m_fadeScratch.end(), 0.0f);

    // Equal power, so the loudness holds steady through the fade.
    for (qint64 i = 0; i < count; ++i) {
        const double t = double(m_fadePosition + i) / double(m_fadeLength);
        const float in = float(std::sin(t * HalfPi));
        const float outGain = float(std::cos(t * HalfPi)) * m_fading.gain;
        float *frame = out + (start + i) * channels;
        const float *old = m_fadeScratch.data() + i * channels;
        for (int ch = 0; ch < channels; ++ch)
            frame[ch] = frame[ch] * in + old[ch] * outGain;
    }
    m_fadePosition += count;
    if (m_fadePosition >= m_fadeLength || (got < count && m_fading.source->finished()))
        stopFade();
}

} // namespace newamp

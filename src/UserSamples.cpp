#include "UserSamples.h"

#include <memory>

namespace forrobox
{

float UserSample::read (int channel, double position) const noexcept
{
    const auto length = audio.getNumSamples();

    if (length <= 0 || position < 0.0)
        return 0.0f;

    const auto index = static_cast<int> (position);

    if (index >= length - 1)
        return 0.0f;

    // Mono sources feed both output channels; `channel` is clamped rather than
    // assumed, because a caller asking for the right of a mono file is asking a
    // reasonable question.
    const auto ch = juce::jlimit (0, audio.getNumChannels() - 1, channel);

    const auto* data = audio.getReadPointer (ch);
    const auto frac = static_cast<float> (position - static_cast<double> (index));

    return data[index] + frac * (data[index + 1] - data[index]);
}

const UserSample* UserSamples::active (int channel) const noexcept
{
    if (! juce::isPositiveAndBelow (channel, kNumChannels))
        return nullptr;

    // ACQUIRED, so everything the loading thread wrote into the buffer is
    // visible before any of it is read. The pointer it returns names a buffer
    // that is never reused, so it stays valid for as long as any voice holds it.
    return published[static_cast<size_t> (channel)].load (std::memory_order_acquire);
}

void UserSamples::clear (int channel) noexcept
{
    if (! juce::isPositiveAndBelow (channel, kNumChannels))
        return;

    // Unpublishes; frees nothing. The buffer stays owned until `prepare`, when
    // no voice can still be reading it.
    published[static_cast<size_t> (channel)].store (nullptr, std::memory_order_release);
}

void UserSamples::prepare (double hostSampleRate)
{
    rate = hostSampleRate;

    // THE ONLY SAFE MOMENT TO FREE. The processor calls this from
    // `prepareToPlay`, after `VoiceEngine::prepare` has reset every voice — so
    // nothing can hold a pointer into a retired buffer. Everything is dropped,
    // including what is currently published, because `restoreUserSamples` runs
    // straight after and re-decodes at the new rate anyway.
    for (auto& slot : published)
        slot.store (nullptr, std::memory_order_release);

    owned.clear();
}

bool UserSamples::load (int channel, const juce::File& file)
{
    if (! juce::isPositiveAndBelow (channel, kNumChannels) || rate <= 0.0)
        return false;

    if (! file.existsAsFile())
        return false;

    // A READER, not an extension. `existsAsFile()` answers a different question,
    // and 09-07 shipped a load path that treated the two as the same — an mp3
    // renamed `.wav` was accepted and then produced nothing, with no diagnostic.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0
          || reader->sampleRate <= 0.0)
        return false;

    // THIRTY SECONDS OF THE FILE, measured at the FILE's rate. The first version
    // compared `rate * 30.0` — a count of HOST-rate samples — against
    // `lengthInSamples`, a count of FILE-rate samples, so a 22 kHz file on a
    // 96 kHz host admitted over two minutes and produced exactly the hundreds of
    // megabytes this cap exists to prevent. /code-review.
    const auto maxSamples = static_cast<juce::int64> (reader->sampleRate * 30.0);
    const auto sourceSamples = juce::jmin (reader->lengthInSamples, maxSamples);

    juce::AudioBuffer<float> source (static_cast<int> (reader->numChannels),
                                     static_cast<int> (sourceSamples));

    if (! reader->read (&source, 0, static_cast<int> (sourceSamples), 0, true, true))
        return false;

    // RESAMPLED HERE, once, into a buffer that is new every time — see the
    // header. Nothing a voice may be reading is touched.
    const auto ratio = reader->sampleRate / rate;
    const auto outSamples = static_cast<int> (static_cast<double> (sourceSamples) / ratio);

    if (outSamples <= 1)
        return false;

    auto fresh = std::make_unique<UserSample>();
    fresh->audio.setSize (source.getNumChannels(), outSamples, false, true, false);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        const auto* in = source.getReadPointer (ch);
        auto* out = fresh->audio.getWritePointer (ch);

        for (int i = 0; i < outSamples; ++i)
        {
            const auto position = static_cast<double> (i) * ratio;
            const auto index = static_cast<int> (position);

            if (index >= source.getNumSamples() - 1)
            {
                out[i] = 0.0f;
                continue;
            }

            const auto frac = static_cast<float> (position - static_cast<double> (index));
            out[i] = in[index] + frac * (in[index + 1] - in[index]);
        }
    }

    const auto* raw = fresh.get();
    owned.push_back (std::move (fresh));

    // PUBLISHED LAST, release-ordered, so the buffer is complete before it is
    // reachable. 09-07's convolution engine published its pointer one line
    // before preparing the object; this is that lesson applied.
    published[static_cast<size_t> (channel)].store (raw, std::memory_order_release);

    return true;
}

} // namespace forrobox

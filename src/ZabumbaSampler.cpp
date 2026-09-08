#include "ZabumbaSampler.h"

#include "BinaryData.h"

#include <algorithm>
#include <cmath>

namespace forrobox
{
namespace
{
    struct EmbeddedSample
    {
        const char* data;
        int size;
    };

    /** The four embedded one-shots, in filename order. Deliberately NOT in
        velocity order: the classifier derives that from the audio, so
        replacing a file cannot silently reorder the mapping. */
    const std::array<EmbeddedSample, ZabumbaSampler::kMaxSlots>& embeddedSamples()
    {
        static const std::array<EmbeddedSample, ZabumbaSampler::kMaxSlots> samples {{
            { BinaryData::ZAB_LOW_01_wav, BinaryData::ZAB_LOW_01_wavSize },
            { BinaryData::ZAB_LOW_02_wav, BinaryData::ZAB_LOW_02_wavSize },
            { BinaryData::ZAB_LOW_03_wav, BinaryData::ZAB_LOW_03_wavSize },
            { BinaryData::ZAB_LOW_04_wav, BinaryData::ZAB_LOW_04_wavSize },
        }};
        return samples;
    }

    float bufferRms (const juce::AudioBuffer<float>& buffer)
    {
        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = buffer.getNumChannels();

        if (numSamples <= 0 || numChannels <= 0)
            return 0.0f;

        auto sum = 0.0;

        for (int s = 0; s < numSamples; ++s)
        {
            auto mono = 0.0;
            for (int c = 0; c < numChannels; ++c)
                mono += static_cast<double> (buffer.getSample (c, s));

            mono /= static_cast<double> (numChannels);
            sum += mono * mono;
        }

        return static_cast<float> (std::sqrt (sum / static_cast<double> (numSamples)));
    }

    /** RMS after a two-pole 250 Hz highpass, over total RMS.

        A cheap stand-in for the spectral centroid, and enough to tell a 96 Hz
        boom from a 527 Hz slap: the three boom hits measure 0.143-0.235 and the
        slap 0.810. Two cascaded one-poles rather than a biquad because the
        decision only needs an octave of resolution, and this way there is no
        resonance to reason about. */
    float bufferBrightness (const juce::AudioBuffer<float>& buffer, double sampleRate)
    {
        const auto total = bufferRms (buffer);

        if (total <= 0.0f)
            return 0.0f;

        const auto numSamples = buffer.getNumSamples();
        const auto numChannels = juce::jmax (1, buffer.getNumChannels());
        const auto a = std::exp (-2.0 * juce::MathConstants<double>::pi * 250.0 / sampleRate);

        double previousIn[2] { 0.0, 0.0 };
        double previousOut[2] { 0.0, 0.0 };
        auto sum = 0.0;

        for (int s = 0; s < numSamples; ++s)
        {
            auto mono = 0.0;
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                mono += static_cast<double> (buffer.getSample (c, s));

            mono /= static_cast<double> (numChannels);

            for (int stage = 0; stage < 2; ++stage)
            {
                const auto out = a * (previousOut[stage] + mono - previousIn[stage]);
                previousIn[stage] = mono;
                previousOut[stage] = out;
                mono = out;
            }

            sum += mono * mono;
        }

        const auto highpassed = std::sqrt (sum / juce::jmax (1.0, static_cast<double> (numSamples)));

        return static_cast<float> (highpassed) / total;
    }
}

void ZabumbaSampler::prepare (double hostSampleRate)
{
    loadAndMeasure();

    // Rate conversion is NOT a separate resampling pass into a host-rate
    // buffer. It is folded into the read increment, so the file stays at its
    // native rate and the read path interpolates exactly once.
    //
    // This deviates from 03-01's plan, which specified a resample in prepare.
    // The reason: PITCH already forces a fractional read on every sample, so a
    // pre-resampled buffer would be interpolated a second time — worse quality
    // for more memory, to no end. The property the plan actually wanted is
    // preserved and tested: no resampling work and no allocation in
    // processBlock, and the rendered length does change with the host rate.
    const auto safeRate = hostSampleRate > 0.0 ? hostSampleRate : 44100.0;
    baseReadRate = fileSampleRate / safeRate;
}

void ZabumbaSampler::loadAndMeasure()
{
    if (loaded)
        return;

    loaded = true;

    juce::WavAudioFormat wav;

    for (int i = 0; i < kMaxSlots; ++i)
    {
        const auto& embedded = embeddedSamples()[static_cast<size_t> (i)];

        if (embedded.data == nullptr || embedded.size <= 0)
            continue;

        // The stream is owned by the reader on success; createReaderFor takes
        // ownership either way.
        auto* stream = new juce::MemoryInputStream (embedded.data,
                                                   static_cast<size_t> (embedded.size),
                                                   false);
        std::unique_ptr<juce::AudioFormatReader> reader { wav.createReaderFor (stream, true) };

        if (reader == nullptr || reader->lengthInSamples <= 0)
            continue;

        auto& slot = slots[static_cast<size_t> (i)];

        const auto numChannels = static_cast<int> (juce::jlimit (1u, 2u, reader->numChannels));
        const auto numSamples = static_cast<int> (juce::jmin (reader->lengthInSamples,
                                                              static_cast<juce::int64> (1 << 22)));

        slot.audio.setSize (numChannels, numSamples);
        reader->read (&slot.audio, 0, numSamples, 0, true, numChannels > 1);

        // All four files are 48 kHz. Taking it from the first readable file
        // rather than hard-coding it means a replacement at another rate still
        // plays at the right speed.
        if (numSlots == 0 && reader->sampleRate > 0.0)
            fileSampleRate = reader->sampleRate;

        slot.rms  = bufferRms (slot.audio);
        slot.peak = slot.audio.getMagnitude (0, numSamples);
        slot.brightness = bufferBrightness (slot.audio, reader->sampleRate > 0.0 ? reader->sampleRate
                                                                                 : fileSampleRate);
        slot.isLayer = slot.rms > 0.0f
                    && slot.brightness < kArticulationBrightnessThreshold;

        ++numSlots;
    }

    // Order the velocity layers softest to loudest by MEASURED RMS. On this
    // material that comes out 04, 02, 01 — the reverse of filename order,
    // which is exactly why the order is measured rather than written down.
    numVelocityLayers = 0;
    numAlternates = 0;

    for (int i = 0; i < kMaxSlots; ++i)
    {
        if (slots[static_cast<size_t> (i)].audio.getNumSamples() <= 0)
            continue;

        if (slots[static_cast<size_t> (i)].isLayer)
            layerOrder[static_cast<size_t> (numVelocityLayers++)] = i;
        else
            ++numAlternates;
    }

    std::sort (layerOrder.begin(), layerOrder.begin() + numVelocityLayers,
               [this] (int a, int b)
               {
                   return slots[static_cast<size_t> (a)].rms < slots[static_cast<size_t> (b)].rms;
               });
}

ZabumbaSampler::LayerBlend ZabumbaSampler::blendForVelocity (float velocity) const noexcept
{
    LayerBlend blend;

    if (numVelocityLayers <= 0)
        return blend;

    const auto v = juce::jlimit (0.0f, 1.0f, velocity);

    if (numVelocityLayers == 1)
    {
        blend.slotA = layerOrder[0];
        blend.gainA = 1.0f;
        return blend;
    }

    // Velocity maps onto the layer set as a continuous position, so every hit
    // sits between two layers rather than picking one. A hard switch is audible
    // even with the levels matched, because the layers differ in body.
    const auto position = v * static_cast<float> (numVelocityLayers - 1);
    const auto lower = juce::jlimit (0, numVelocityLayers - 2,
                                     static_cast<int> (std::floor (position)));
    const auto fraction = juce::jlimit (0.0f, 1.0f, position - static_cast<float> (lower));

    blend.slotA = layerOrder[static_cast<size_t> (lower)];
    blend.slotB = layerOrder[static_cast<size_t> (lower + 1)];
    blend.gainA = 1.0f - fraction;
    blend.gainB = fraction;

    return blend;
}

int ZabumbaSampler::getLengthSamples (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kMaxSlots))
        return 0;

    return slots[static_cast<size_t> (slot)].audio.getNumSamples();
}

float ZabumbaSampler::getNormalisationGain (int slot) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kMaxSlots))
        return 0.0f;

    const auto rms = slots[static_cast<size_t> (slot)].rms;

    return rms > 0.0f ? kTargetRms / rms : 0.0f;
}

float ZabumbaSampler::readSample (int slot, int channel, double position) const noexcept
{
    if (! juce::isPositiveAndBelow (slot, kMaxSlots))
        return 0.0f;

    const auto& buffer = slots[static_cast<size_t> (slot)].audio;
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    if (numSamples <= 0 || numChannels <= 0)
        return 0.0f;

    // A mono replacement file feeds both output channels rather than silencing
    // the right one.
    const auto readChannel = juce::jlimit (0, numChannels - 1, channel);

    const auto index = static_cast<int> (std::floor (position));

    if (index < 0 || index >= numSamples)
        return 0.0f;

    const auto fraction = static_cast<float> (position - static_cast<double> (index));
    const auto* data = buffer.getReadPointer (readChannel);

    const auto at = [data, numSamples] (int i) noexcept
    {
        return juce::isPositiveAndBelow (i, numSamples) ? data[i] : 0.0f;
    };

    // 4-point, 3rd-order Hermite (Catmull-Rom).
    const auto y0 = at (index - 1);
    const auto y1 = at (index);
    const auto y2 = at (index + 1);
    const auto y3 = at (index + 2);

    const auto c0 = y1;
    const auto c1 = 0.5f * (y2 - y0);
    const auto c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3;
    const auto c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);

    return ((c3 * fraction + c2) * fraction + c1) * fraction + c0;
}

float ZabumbaSampler::getMeasuredRms (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, kMaxSlots) ? slots[static_cast<size_t> (slot)].rms : 0.0f;
}

float ZabumbaSampler::getMeasuredPeak (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, kMaxSlots) ? slots[static_cast<size_t> (slot)].peak : 0.0f;
}

float ZabumbaSampler::getMeasuredBrightness (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, kMaxSlots) ? slots[static_cast<size_t> (slot)].brightness : 0.0f;
}

bool ZabumbaSampler::isVelocityLayer (int slot) const noexcept
{
    return juce::isPositiveAndBelow (slot, kMaxSlots) && slots[static_cast<size_t> (slot)].isLayer;
}

} // namespace forrobox

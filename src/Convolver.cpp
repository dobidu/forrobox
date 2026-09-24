#include "Convolver.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include "ParameterIDs.h"

namespace forrobox
{

void Convolver::prepare (double sampleRate, int maximumBlockSize, int numChannels)
{
    preparedRate     = sampleRate;
    preparedBlock    = juce::jmax (1, maximumBlockSize);
    preparedChannels = juce::jmax (1, numChannels);

    // SIZED HERE, so `process` never allocates. The scratch holds the wet copy
    // the engine writes into while the dry stays in `buffer` — the blend needs
    // both, and growing this from the audio thread is the allocation the
    // counter in the suite exists to catch.
    wetScratch.setSize (preparedChannels, preparedBlock, false, true, true);

    // The ENGINE is only prepared if one exists. See the member's comment: it
    // is built on first load, so an instance that never loads an IR never
    // starts its background thread.
    // Lowered across the re-prepare so the audio thread cannot use the engine
    // mid-reconfiguration. `prepareToPlay` is not called concurrently with
    // `processBlock` by JUCE's contract, but the gate costs nothing and the
    // contract is not this class's to assume.
    if (convolution != nullptr)
    {
        const auto wasReady = engineReady.load (std::memory_order_acquire);
        engineReady.store (false, std::memory_order_release);

        convolution->prepare (specFor());

        engineReady.store (wasReady, std::memory_order_release);
    }

    prepared = true;
}

juce::dsp::ProcessSpec Convolver::specFor() const noexcept
{
    return { preparedRate,
             static_cast<juce::uint32> (preparedBlock),
             static_cast<juce::uint32> (preparedChannels) };
}

void Convolver::reset() noexcept
{
    if (convolution != nullptr)
        convolution->reset();

    wetScratch.clear();
}

void Convolver::clear() noexcept
{
    // LOWERS THE GATE; does not destroy. Destroying it here raced the audio
    // thread — see `engineReady`. The engine and its thread stay for the
    // instance's life once an IR has ever been loaded, which is the price of
    // not freeing an object another thread may be inside.
    engineReady.store (false, std::memory_order_release);
}

int Convolver::latencySamples() const noexcept
{
    // Asked of the engine rather than assumed from its constructor argument: a
    // number this function made up would be exactly the unreported latency the
    // header warns about. No engine means no latency.
    if (latencyOverride >= 0)
        return latencyOverride;

    return convolution != nullptr ? static_cast<int> (convolution->getLatency()) : 0;
}

bool Convolver::loadImpulseResponse (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    // EXISTING IS NOT READABLE. `juce::dsp::Convolution::loadImpulseResponse`
    // is fire-and-forget and reports nothing, and JUCE's own loader returns an
    // EMPTY buffer with `sampleRate == 0` when it cannot make a reader — which
    // then goes through `resampleImpulseResponse` with a ratio of zero. The
    // audible result is a wet path producing nothing while `1 - wet` still
    // attenuates the dry, so at full mix the bus collapses to near-silence with
    // no diagnostic anywhere. An mp3 renamed `.wav` does exactly that.
    //
    // So the reader is built HERE, where failure can still be reported.
    // /code-review.
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();

    const std::unique_ptr<juce::AudioFormatReader> reader { formats.createReaderFor (file) };

    if (reader == nullptr || reader->numChannels == 0 || reader->lengthInSamples <= 0)
        return false;

    // The engine copies what it needs and swaps it in on its own schedule, so
    // this is safe to call while audio runs — but it opens a file, so it is
    // still message-thread only. `Stereo::yes` keeps a stereo IR stereo;
    // `Trim::no` because trimming silence would change an IR's pre-delay, which
    // is part of the room the user chose.
    // BUILT NOW, on the message thread, because this is the first moment an
    // engine is actually needed.
    if (convolution == nullptr)
    {
        auto engine = std::make_unique<juce::dsp::Convolution> (
                          juce::dsp::Convolution::Latency { 0 });

        if (prepared)
            engine->prepare (specFor());

        // Only now is it a complete object, so only now does the member point
        // at it. The first version assigned `convolution` and prepared it on
        // the NEXT line, which published a half-built engine.
        convolution = std::move (engine);
    }

    convolution->loadImpulseResponse (file,
                                      juce::dsp::Convolution::Stereo::yes,
                                      juce::dsp::Convolution::Trim::no,
                                      0,
                                      juce::dsp::Convolution::Normalise::yes);

    // THE GATE GOES UP LAST, with release ordering, so everything above is
    // visible to the audio thread before it may touch any of it.
    engineReady.store (true, std::memory_order_release);
    return true;
}

void Convolver::process (juce::AudioBuffer<float>& buffer, float wetPercent) noexcept
{
    const auto numSamples = buffer.getNumSamples();
    const auto numChannels = buffer.getNumChannels();

    // ACQUIRED, and the pointer is only touched after it succeeds.
    if (! prepared || ! engineReady.load (std::memory_order_acquire)
          || numSamples <= 0 || numChannels <= 0)
        return;

    const auto wet = ids::normalisedPercent (wetPercent);

    // AT ZERO, DO NO WORK — and the first version of this comment claimed more
    // than that. It said returning here is "the difference between the same
    // buffer and the same buffer within a rounding error", which is false:
    // `applyGain (1.0f)` and `addFrom (…, 0.0f)` are exact in IEEE-754, so the
    // output is bit-identical either way. A mutation removing this branch passed
    // every check, which is how the overclaim was found.
    //
    // What it actually buys is the FFT work of an inaudible reverb, which is the
    // whole cost of this stage. One consequence worth knowing: the engine's tail
    // does not build while the wet is down, so turning it up starts the room
    // from cold rather than revealing what it would have been.
    if (wet <= 0.0f)
        return;

    const auto usableChannels = juce::jmin (numChannels, wetScratch.getNumChannels());
    const auto usableSamples  = juce::jmin (numSamples, wetScratch.getNumSamples());

    // A block larger than `prepare` was told about would mean writing past the
    // scratch. Dropping the stage for that block is wrong-sounding; writing past
    // the buffer is worse, and `prepareToPlay` is the contract that stops it.
    if (usableChannels < numChannels || usableSamples < numSamples)
        return;

    for (int ch = 0; ch < numChannels; ++ch)
        wetScratch.copyFrom (ch, 0, buffer, ch, 0, numSamples);

    juce::dsp::AudioBlock<float> block { wetScratch.getArrayOfWritePointers(),
                                         static_cast<size_t> (numChannels),
                                         0,
                                         static_cast<size_t> (numSamples) };

    juce::dsp::ProcessContextReplacing<float> context { block };
    convolution->process (context);

    // Equal-gain crossfade, the same shape `MixBus::wetGainFor` uses for the
    // character bus: the two stages should not disagree about what a "mix"
    // control means just because they are different stages.
    for (int ch = 0; ch < numChannels; ++ch)
    {
        buffer.applyGain (ch, 0, numSamples, 1.0f - wet);
        buffer.addFrom (ch, 0, wetScratch, ch, 0, numSamples, wet);
    }
}

} // namespace forrobox

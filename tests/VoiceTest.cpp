/* ============================================================================
   FORRÓ BOX — voice engine tests

   There is no guaranteed audio device on WSL2, so every audio claim here is
   proved by rendering offline and MEASURING the result: onset position against
   what the clock reports, energy in the bands the spec names, and duration to
   -60 dBFS.

   "Is it silent" is not a test. A voice at the wrong frequency, a voice whose
   duration ignores DECAY, and a voice panned to the wrong side all pass it.
   Phase 2 shipped five assertions that could not fail; each tolerance below was
   chosen by measuring the thing it bounds, and the measurement is recorded next
   to it.
============================================================================ */
#include "TestSuites.h"
#include "TestHarness.h"

#include "PluginProcessor.h"
#include "Voices.h"
#include "VoiceEngine.h"
#include "ZabumbaSampler.h"
#include "Profiles.h"

#include <iostream>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace fbtest;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm = 120.0;

    /** Samples per sixteenth at kBpm / kSampleRate: 60/120/4 * 48000 = 6000. */
    constexpr double kStepSamples = 6000.0;

    // ── measurement ─────────────────────────────────────────────────────────

    /** Power at one frequency, by Goertzel. Cheaper than a DFT and exact at the
        bin, which is all a "does this voice sit where the spec says" question
        needs. */
    double goertzelPower (const float* samples, int numSamples, double freq, double sampleRate)
    {
        if (numSamples <= 0 || freq <= 0.0 || freq >= sampleRate * 0.5)
            return 0.0;

        const auto w = 2.0 * juce::MathConstants<double>::pi * freq / sampleRate;
        const auto c = 2.0 * std::cos (w);

        auto s1 = 0.0, s2 = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            const auto s0 = static_cast<double> (samples[i]) + c * s1 - s2;
            s2 = s1;
            s1 = s0;
        }

        return juce::jmax (0.0, s1 * s1 + s2 * s2 - c * s1 * s2);
    }

    /** Summed power over a band, sampled on a semitone grid so a wide band and
        a narrow one are comparable per-octave rather than per-Hz. */
    double bandEnergy (const juce::AudioBuffer<float>& buffer, double lo, double hi,
                       double sampleRate = kSampleRate)
    {
        if (buffer.getNumSamples() <= 0)
            return 0.0;

        const auto* samples = buffer.getReadPointer (0);
        const auto n = buffer.getNumSamples();

        auto total = 0.0;

        for (auto f = lo; f < hi; f *= std::pow (2.0, 1.0 / 12.0))
            total += goertzelPower (samples, n, f, sampleRate);

        return total;
    }

    float bufferPeak (const juce::AudioBuffer<float>& buffer)
    {
        return buffer.getNumSamples() > 0 ? buffer.getMagnitude (0, buffer.getNumSamples()) : 0.0f;
    }

    double bufferRms (const juce::AudioBuffer<float>& buffer)
    {
        const auto n = buffer.getNumSamples();

        if (n <= 0)
            return 0.0;

        auto sum = 0.0;

        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto* samples = buffer.getReadPointer (c);

            for (int i = 0; i < n; ++i)
                sum += static_cast<double> (samples[i]) * static_cast<double> (samples[i]);
        }

        return std::sqrt (sum / static_cast<double> (n * juce::jmax (1, buffer.getNumChannels())));
    }

    /** First sample that is not exactly zero.

        The right detector for a synthetic render, and the level-relative
        `findOnset` above is the wrong one for asking WHEN a voice started: it
        answers "when did the waveform get loud", which for a 130 Hz sine is a
        quarter period later and for the zabumba's 3.6 ms sample attack is 174
        samples later. Before a voice starts, the buffer holds exact zeroes, so
        this is exact. */
    int firstNonZeroSample (const juce::AudioBuffer<float>& buffer)
    {
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                if (! juce::exactlyEqual (buffer.getSample (c, i), 0.0f))
                    return i;

        return -1;
    }

    /** Last sample above -60 dBFS relative to the buffer's peak. */
    int findTailEnd (const juce::AudioBuffer<float>& buffer)
    {
        const auto peak = bufferPeak (buffer);

        if (peak <= 0.0f)
            return -1;

        const auto threshold = peak * 0.001f;

        for (int i = buffer.getNumSamples() - 1; i >= 0; --i)
            for (int c = 0; c < buffer.getNumChannels(); ++c)
                if (std::abs (buffer.getSample (c, i)) > threshold)
                    return i;

        return -1;
    }

    /** Exactly silent, asserted exactly.

        checkEqual compares floats with a 1.0e-4 tolerance, which is the right
        default for a measured level and the wrong one for this: a mute leaking
        at -80 dBFS would pass it. Mute and velocity 0 claim exact zero, so
        exact zero is what gets checked. */
    void checkSilent (const juce::AudioBuffer<float>& buffer, const juce::String& description)
    {
        const auto peak = bufferPeak (buffer);

        check (! (peak > 0.0f),
               description + (peak > 0.0f ? juce::String (" (leaked peak ") + juce::String (peak, 9) + ")"
                                          : juce::String()));
    }

    bool isFinite (const juce::AudioBuffer<float>& buffer)
    {
        for (int c = 0; c < buffer.getNumChannels(); ++c)
        {
            const auto* samples = buffer.getReadPointer (c);

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                if (! std::isfinite (samples[i]))
                    return false;
        }

        return true;
    }

    // ── the rig ─────────────────────────────────────────────────────────────

    /** A processor on its internal transport, with an empty pattern, rendering
        into one contiguous buffer.

        Internal rather than host-synced: this suite is about what the voices
        sound like, and 02-03's suite already covers where the steps land. */
    struct AudioRig
    {
        ForroBoxAudioProcessor processor;
        double sampleRate;

        explicit AudioRig (double rate = kSampleRate, int blockSize = 512)
            : sampleRate (rate)
        {
            processor.prepareToPlay (rate, blockSize);

            setValue (forrobox::ids::sync, 0.0f);
            setValue (forrobox::ids::bpm, static_cast<float> (kBpm));
            setValue (forrobox::ids::swing, 0.0f);
            setChoice (forrobox::ids::steps, 0);

            // Every channel fully open, centred, nothing muted or soloed, so a
            // test that cares about one parameter is not reading another's
            // default. DECAY is left at each channel's own default, which is
            // what the spec's durations are quoted against.
            for (const auto& info : forrobox::ids::channelInfos)
            {
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::vol), 100.0f);
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::pitch), 0.0f);
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::pan), 0.0f);
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::mute), 0.0f);
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::solo), 0.0f);
            }

            clearPattern();
        }

        void setValue (juce::StringRef id, float value)
        {
            if (auto* param = processor.getAPVTS().getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        }

        void setChoice (juce::StringRef id, int index)
        {
            if (auto* param = processor.getAPVTS().getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
        }

        void clearPattern()
        {
            auto state = processor.lockPatternState();

            for (auto& lane : state->lanes)
                lane.fill (0);
        }

        void setStep (int lane, int step, std::uint8_t velocity)
        {
            auto state = processor.lockPatternState();
            state->lanes[static_cast<size_t> (lane)][static_cast<size_t> (step)] = velocity;
        }

        /** Renders `numSamples` in blocks of `blockSize` into one buffer. */
        juce::AudioBuffer<float> render (int numSamples, int blockSize = 512)
        {
            juce::AudioBuffer<float> output (2, numSamples);
            output.clear();

            juce::AudioBuffer<float> block (2, blockSize);
            juce::MidiBuffer midi;

            processor.setPlaying (true);

            for (int written = 0; written < numSamples; written += blockSize)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);

                const auto toCopy = juce::jmin (blockSize, numSamples - written);

                for (int c = 0; c < 2; ++c)
                    output.copyFrom (c, written, block, c, 0, toCopy);
            }

            return output;
        }
    };

    /** One hit on one lane at one velocity, rendered long enough for its whole
        tail. Every per-voice test goes through this so no test can accidentally
        measure two voices at once. */
    juce::AudioBuffer<float> renderSingleHit (int lane, std::uint8_t velocity,
                                              double seconds = 2.0,
                                              int blockSize = 512,
                                              double sampleRate = kSampleRate)
    {
        AudioRig rig { sampleRate, blockSize };
        rig.setStep (lane, 0, velocity);

        const auto numSamples = static_cast<int> (seconds * sampleRate);

        return rig.render (numSamples - numSamples % blockSize, blockSize);
    }

    const char* laneName (int lane)
    {
        return forrobox::ids::lanes[static_cast<size_t> (lane)];
    }

    /** DECAY each channel defaults to, which is what the spec's durations are
        quoted against. */
    float defaultDecayForLane (int lane)
    {
        const auto channel = forrobox::VoiceEngine::channelForLane (lane);

        return forrobox::ids::channelInfos[static_cast<size_t> (channel)].decay;
    }

    double decayScale (float decayPercent)
    {
        return 0.4 + static_cast<double> (decayPercent) / 100.0 * 1.4;
    }

    /** The duration the spec gives a lane at a velocity and a DECAY. */
    double specDurationSeconds (int lane, float velocity01, float decayPercent)
    {
        const auto& spec = forrobox::voiceSpecs[static_cast<size_t> (lane)];

        const auto base = (spec.openThreshold > 0.0f && velocity01 > spec.openThreshold)
                            ? spec.openDurBase
                            : spec.durBase;

        return (static_cast<double> (base) + static_cast<double> (spec.durPerVelocity) * velocity01)
                 * decayScale (decayPercent);
    }
}

namespace
{
    // ── AC-4: the sampled zabumba, and the classification behind it ─────────

    void testSamplerClassification()
    {
        section ("zabumba sampler: what the measurements found");

        AudioRig rig;
        const auto& sampler = rig.processor.getVoiceEngine().getSampler();

        checkEqual (sampler.getNumSlots(), 4, "all four embedded one-shots decoded");
        checkEqual (sampler.getFileSampleRate(), 48000.0, "the files are 48 kHz");
        check (sampler.isReady(), "the sampler has at least one velocity layer");

        // Three velocity layers and one alternate articulation. This is the
        // assertion that would have caught the original description: the
        // project notes recorded "4 one-shot variants, single articulation",
        // and the hybrid engine decision was taken on that.
        checkEqual (sampler.getNumVelocityLayers(), 3, "three slots classify as velocity layers");
        checkEqual (sampler.getNumAlternateArticulations(), 1,
                    "one slot is a different articulation, not a layer");

        // Slot 2 is ZAB_LOW_03 — centroid 527 Hz against the others' 96-126,
        // and 78% of its energy above 160 Hz. Named by index because the
        // sampler deliberately knows the files only by position.
        check (! sampler.isVelocityLayer (2), "ZAB_LOW_03 is held aside");

        for (const int slot : { 0, 1, 3 })
            check (sampler.isVelocityLayer (slot),
                   juce::String ("slot ") + juce::String (slot) + " is a velocity layer");

        // The classifier's threshold is 0.45. Measured margins: the brightest
        // boom is 0.235 and the outlier 0.810, so each side clears it by more
        // than 1.8x. Asserting the margin, not just the verdict — a threshold
        // that happened to sit 1% from the data would pass the verdict check
        // today and break on the next sample set.
        const auto threshold = forrobox::ZabumbaSampler::kArticulationBrightnessThreshold;

        for (const int slot : { 0, 1, 3 })
            check (sampler.getMeasuredBrightness (slot) < threshold * 0.6f,
                   juce::String ("slot ") + juce::String (slot)
                       + " brightness clears the threshold with margin ("
                       + juce::String (sampler.getMeasuredBrightness (slot), 3) + ")");

        check (sampler.getMeasuredBrightness (2) > threshold * 1.6f,
               juce::String ("the alternate's brightness clears it the other way (")
                   + juce::String (sampler.getMeasuredBrightness (2), 3) + ")");

        // Peak and RMS disagree about which file is loudest, which is why the
        // ordering is by RMS and the normalisation is too. ZAB_LOW_03 has the
        // HIGHEST peak of all four and the second-lowest RMS.
        check (sampler.getMeasuredPeak (2) > sampler.getMeasuredPeak (0),
               "peak ordering puts the alternate above the open layer");
        check (sampler.getMeasuredRms (2) < sampler.getMeasuredRms (0),
               "RMS ordering puts it below — the two measures disagree, as designed for");
    }

    void testSamplerVelocityBlend()
    {
        section ("zabumba sampler: velocity blends across layers");

        AudioRig rig;
        const auto& sampler = rig.processor.getVoiceEngine().getSampler();

        // Softest layer alone at the bottom, loudest alone at the top.
        const auto quietest = sampler.blendForVelocity (0.0f);
        const auto loudest  = sampler.blendForVelocity (1.0f);

        checkEqual (quietest.gainA, 1.0f, "velocity 0 plays one layer at unity");
        checkEqual (loudest.gainB, 1.0f, "velocity 1 plays one layer at unity");
        check (quietest.slotA != loudest.slotB, "the two ends are different layers");

        // Ordered softest first by MEASURED RMS. On this material that comes
        // out 04, 02, 01 — the reverse of filename order, so a hard-coded
        // order would have been backwards.
        check (sampler.getMeasuredRms (quietest.slotA) < sampler.getMeasuredRms (loudest.slotB),
               "the layer at velocity 0 measures quieter than the one at velocity 1");

        // Gains sum to 1 everywhere, so the crossfade neither dips nor bumps.
        for (auto v = 0.0f; v <= 1.0f; v += 0.05f)
        {
            const auto blend = sampler.blendForVelocity (v);
            checkEqual (blend.gainA + blend.gainB, 1.0f,
                        juce::String ("blend gains sum to 1 at velocity ") + juce::String (v, 2));
        }

        // Mid-range velocities really do blend two layers rather than snapping.
        const auto middle = sampler.blendForVelocity (0.4f);
        check (middle.slotA >= 0 && middle.slotB >= 0 && middle.gainA > 0.0f && middle.gainB > 0.0f,
               "a mid velocity sounds two layers at once");
    }

    void testLayersAreLevelMatched()
    {
        section ("zabumba layers are matched by RMS, not by peak");

        // Added after a negative control went UNDETECTED. Normalising by peak
        // instead of RMS left the whole suite green, even though the choice
        // between them is the most consequential decision in the sampler and is
        // documented at length in ZabumbaSampler.h. That is the 02-04 pattern
        // exactly: nothing tested the reason the code is shaped as it is.
        //
        // The velocity monotonicity test could not see it. Velocity spans 8x
        // (18 dB) across the sampled points while peak-normalisation's level
        // error between layers is only 4.7 dB, and the crossfade smears that
        // across neighbours — so the ramp stayed monotonic while every layer
        // sat at the wrong level.
        //
        // Asserted directly instead: normalisation exists to bring every layer
        // to a common RMS, so gain x measured RMS must equal that target.
        AudioRig rig;
        const auto& sampler = rig.processor.getVoiceEngine().getSampler();

        auto worstError = 0.0f;

        for (int slot = 0; slot < sampler.getNumSlots(); ++slot)
        {
            if (! sampler.isVelocityLayer (slot))
                continue;

            const auto normalised = sampler.getNormalisationGain (slot) * sampler.getMeasuredRms (slot);

            check (std::abs (normalised - forrobox::ZabumbaSampler::kTargetRms) < 1.0e-4f,
                   juce::String ("layer ") + juce::String (slot) + " normalises to the target RMS ("
                       + juce::String (normalised, 5) + ")");

            worstError = juce::jmax (worstError,
                                     std::abs (normalised - forrobox::ZabumbaSampler::kTargetRms));
        }

        // Under peak normalisation these products span 0.026 to 0.28 — an 11x
        // spread — so this bound is nowhere near the wrong answer.
        check (worstError < 1.0e-4f,
               juce::String ("every layer lands on the same RMS (worst error ")
                   + juce::String (worstError, 6) + ")");

        // And rendered: two layers played at the velocity that selects each one
        // outright must come out at comparable loudness. The end-to-end version
        // of the claim above, which also covers the render path.
        auto softest = renderSingleHit (0, 1, 1.5);
        auto loudest = renderSingleHit (0, 127, 1.5);

        // Velocity itself scales the gain, so divide it back out: what is being
        // compared is the LAYERS, not the velocities.
        const auto softLevel = bufferRms (softest) / (1.0 / 127.0);
        const auto loudLevel = bufferRms (loudest);
        const auto spreadDb = std::abs (20.0 * std::log10 (loudLevel / juce::jmax (1.0e-9, softLevel)));

        check (spreadDb < 6.0,
               juce::String ("the softest and loudest layers are within 6 dB once velocity is "
                             "divided out (") + juce::String (spreadDb, 2) + " dB)");
    }

    void testPitchTracksPerComponent()
    {
        section ("PITCH is applied per component, not globally");

        // Also added after a negative control went undetected. Turning off
        // ganzá's `pitchTracksFilter` left the suite green, even though "the
        // pitch factor is applied PER COMPONENT and inconsistently" is the
        // single most surprising thing about these recipes and is called out at
        // the top of Voices.h. The old PITCH test only measured TOM, whose
        // pitch lives in an oscillator.
        //
        // Ganzá is the case that matters: it has NO oscillator at all — it is
        // noise, and its bandpass centre IS its pitch. If the filter stops
        // tracking, the instrument stops responding to PITCH entirely while
        // still sounding perfectly fine.
        const auto* ganza = forrobox::ids::channelInfos[3].id;

        double atCentre[2] {}, anOctaveUp[2] {};

        for (int i = 0; i < 2; ++i)
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (ganza, forrobox::ids::pitch),
                          i == 0 ? 0.0f : 12.0f);
            rig.setStep (3, 0, 127);

            auto buffer = rig.render (48000);

            // 6.8 kHz nominal, 13.6 kHz an octave up. Bands are narrow enough
            // that a Q-1.2 peak sits clearly in one and not the other.
            atCentre[i]   = bandEnergy (buffer, 5600.0, 8200.0);
            anOctaveUp[i] = bandEnergy (buffer, 11200.0, 16400.0);
        }

        check (atCentre[0] > anOctaveUp[0] * 2.0,
               juce::String ("at PITCH 0 the ganzá's band sits at 6.8 kHz (ratio ")
                   + juce::String (atCentre[0] / juce::jmax (1.0e-12, anOctaveUp[0]), 2) + ")");

        check (anOctaveUp[1] > atCentre[1],
               juce::String ("at PITCH +12 its FILTER has moved up an octave (ratio ")
                   + juce::String (anOctaveUp[1] / juce::jmax (1.0e-12, atCentre[1]), 2)
                   + ") — pitchTracksFilter is honoured");

        // The mirror case, and the half that pins "per component": HH's
        // highpass is deliberately FIXED at 9 kHz, so PITCH must not move it.
        // Without this, making every filter track pitch would pass the test
        // above and be just as wrong.
        const auto* bateria = forrobox::ids::channelInfos[4].id;

        double hhLow[2] {}, hhHigh[2] {};

        for (int i = 0; i < 2; ++i)
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::pitch),
                          i == 0 ? 0.0f : 12.0f);
            rig.setStep (6, 0, 127);

            auto buffer = rig.render (48000);
            hhLow[i]  = bandEnergy (buffer, 9000.0, 12000.0);
            hhHigh[i] = bandEnergy (buffer, 16000.0, 22000.0);
        }

        const auto shift = (hhHigh[1] / juce::jmax (1.0e-12, hhLow[1]))
                             / juce::jmax (1.0e-12, hhHigh[0] / juce::jmax (1.0e-12, hhLow[0]));

        check (shift < 2.0,
               juce::String ("HH's highpass does NOT move with PITCH, as the spec has it (band "
                             "balance changed by ") + juce::String (shift, 3) + "x)");
    }

    void testZabumbaRendersAtBothRates()
    {
        section ("zabumba: rate conversion and level");

        for (const double rate : { 44100.0, 48000.0 })
        {
            auto buffer = renderSingleHit (0, 110, 2.0, 512, rate);

            check (bufferPeak (buffer) > 0.001f,
                   juce::String ("the sampled zabumba sounds at ") + juce::String (rate, 0) + " Hz");
            check (isFinite (buffer),
                   juce::String ("no NaN or infinity at ") + juce::String (rate, 0) + " Hz");
        }

        // Rate conversion is applied: the same file must occupy the same amount
        // of TIME at both rates, not the same number of samples. At 44.1 kHz a
        // 48 kHz file read at 1.0 would run 8.8% long.
        auto at48 = renderSingleHit (0, 110, 2.0, 512, 48000.0);
        auto at44 = renderSingleHit (0, 110, 2.0, 512, 44100.0);

        const auto seconds48 = static_cast<double> (findTailEnd (at48)) / 48000.0;
        const auto seconds44 = static_cast<double> (findTailEnd (at44)) / 44100.0;

        check (seconds48 > 0.0 && seconds44 > 0.0, "both renders have a measurable tail");
        check (std::abs (seconds48 - seconds44) < 0.02,
               juce::String ("the hit lasts the same TIME at both rates (")
                   + juce::String (seconds48, 4) + " vs " + juce::String (seconds44, 4)
                   + " s) — the rate conversion is applied, not skipped");

        // Ignoring the conversion would stretch it by 48000/44100 = 8.8%, which
        // on a ~0.4 s hit is ~35 ms — well outside the 20 ms bound above.
    }

    void testZabumbaVelocityIsMonotonic()
    {
        section ("zabumba: level rises with velocity");

        // RMS, not peak, and deliberately.
        //
        // The three layers' peak-to-RMS ratios are 3.8, 6.2 and 3.6 softest to
        // loudest, so rendered PEAK is not monotonic across the whole velocity
        // range even though loudness is. Normalising by peak instead would fix
        // that number and break the sound: the softest layer's peak/RMS is 3.8
        // against the alternate's 21.3, so matching peaks makes the quietest
        // sample the loudest thing in the set.
        //
        // This is a recorded deviation from 03-01's AC-4, which said "peak".
        auto previous = 0.0;
        auto rises = 0;
        auto falls = 0;

        for (const std::uint8_t velocity : std::array<std::uint8_t, 8> { 16, 32, 48, 64, 80, 96, 112, 127 })
        {
            auto buffer = renderSingleHit (0, velocity, 1.5);
            const auto rms = bufferRms (buffer);

            if (previous > 0.0)
            {
                if (rms > previous) ++rises;
                else                ++falls;
            }

            previous = rms;
        }

        checkEqual (falls, 0, "zabumba RMS never falls as velocity rises");
        checkEqual (rises, 7, "zabumba RMS rises at every one of the seven velocity steps");

        // And the span is wide enough to be a real dynamic range rather than a
        // rounding artefact: measured 27 dB from velocity 16 to 127.
        auto quiet = renderSingleHit (0, 16, 1.5);
        auto loud  = renderSingleHit (0, 127, 1.5);

        const auto ratioDb = 20.0 * std::log10 (bufferRms (loud) / juce::jmax (1.0e-9, bufferRms (quiet)));

        check (ratioDb > 12.0,
               juce::String ("velocity 16 to 127 spans a real dynamic range (")
                   + juce::String (ratioDb, 1) + " dB)");

        // The whole point of the crossfade: no step in that ramp is a cliff. A
        // hard layer switch measured 8-10 dB at a boundary; 6 dB bounds it with
        // margin while still catching one.
        auto worstStep = 0.0;
        auto previousRms = 0.0;

        for (std::uint8_t velocity = 8; velocity <= 127; velocity = static_cast<std::uint8_t> (velocity + 8))
        {
            auto buffer = renderSingleHit (0, velocity, 1.0);
            const auto rms = bufferRms (buffer);

            if (previousRms > 0.0)
                worstStep = juce::jmax (worstStep, std::abs (20.0 * std::log10 (rms / previousRms)));

            previousRms = rms;
        }

        check (worstStep < 6.0,
               juce::String ("no velocity step jumps more than 6 dB (worst ")
                   + juce::String (worstStep, 2) + " dB)");
    }
}

namespace
{
    // ── AC-2: each voice is the voice it claims to be ───────────────────────

    /** Where each synthesised lane's energy must sit, and where it must not.

        Taken from the Voice Specifications, not from listening: the triângulo's
        bandpass is at 7.6 kHz, the ganzá's at 6.8 kHz, BB sweeps 130->48 Hz and
        so on. `quietBand` is a band the voice has no business occupying, which
        is what makes this more than a level check. */
    struct SpectralExpectation
    {
        int lane;
        double loudLo, loudHi;
        double quietLo, quietHi;
        double minRatio;         ///< loud/quiet power, measured then bounded
    };

    void testVoiceSpectra()
    {
        section ("each synthesised voice sits where the spec says");

        // Ratios below were measured, then bounded well under the measurement
        // so the assertion has room to be true and room to fail. A voice
        // rendered at the wrong frequency, or not rendered at all, fails.
        const std::array<SpectralExpectation, 7> expectations {{
            // triângulo: five partials 5.4-11.2 kHz through a 7.6 kHz bandpass
            { 1,  5000.0, 12000.0,   80.0,  400.0,  20.0 },
            // pandeiro: membrane sweeps 330->180 Hz
            { 2,   170.0,   400.0, 2000.0, 4000.0,   3.0 },
            // ganzá: narrow band at 6.8 kHz, Q 1.2
            { 3,  5000.0,  9000.0,   80.0,  400.0,  20.0 },
            // bateria BB: 130->48 Hz
            { 4,    45.0,   150.0, 1500.0, 4000.0,  20.0 },
            // bateria CX: noise highpassed at 1.7 kHz, against the trough between
            // its 190 Hz body and that corner.
            //
            // 40-90 Hz was the first choice and it was wrong — measured, CX has
            // MORE energy at 50-120 Hz than at 2-8 kHz, and that is correct: a
            // 190 Hz note decaying exponentially over 94 ms has a Lorentzian
            // skirt about 29 Hz wide, putting 65 Hz only 14 dB down. HH, whose
            // envelope is similar but which has no low carrier, measures 1e-5
            // there — so the highpass itself was never in doubt.
            { 5,  2000.0,  8000.0,  800.0, 1300.0,   5.0 },
            // bateria HH: noise highpassed at 9 kHz
            { 6, 10000.0, 18000.0,  200.0,  600.0,  10.0 },
            // bateria TOM: 190->110 Hz
            { 7,   100.0,   250.0, 1500.0, 4000.0,  20.0 },
        }};

        for (const auto& expectation : expectations)
        {
            auto buffer = renderSingleHit (expectation.lane, 127);
            const auto name = juce::String (laneName (expectation.lane));

            check (bufferPeak (buffer) > 0.0005f, name + " sounds at all");
            check (isFinite (buffer), name + " renders no NaN or infinity");
            check (bufferPeak (buffer) <= 1.0f,
                   name + " does not clip on its own (peak "
                        + juce::String (bufferPeak (buffer), 4) + ")");

            const auto loud  = bandEnergy (buffer, expectation.loudLo, expectation.loudHi);
            const auto quiet = bandEnergy (buffer, expectation.quietLo, expectation.quietHi);
            const auto ratio = loud / juce::jmax (1.0e-12, quiet);

            check (ratio > expectation.minRatio,
                   name + " puts its energy in " + juce::String (expectation.loudLo, 0) + "-"
                        + juce::String (expectation.loudHi, 0) + " Hz rather than "
                        + juce::String (expectation.quietLo, 0) + "-"
                        + juce::String (expectation.quietHi, 0) + " Hz (ratio "
                        + juce::String (ratio, 1) + ")");
        }
    }

    void testCaixaHasBothLayers()
    {
        section ("bateria CX: body and rattle are both there");

        // CX is the only voice built from a tone AND noise where each half has
        // its own duration (the triangle runs 0.7x the noise). One band ratio
        // cannot say both are present, so both are asserted.
        auto buffer = renderSingleHit (5, 127);

        const auto body   = bandEnergy (buffer, 170.0, 220.0);   // the 190 Hz triangle
        const auto rattle = bandEnergy (buffer, 2000.0, 8000.0); // noise above the 1.7 kHz corner
        const auto trough = bandEnergy (buffer, 800.0, 1300.0);  // neither

        check (body > trough * 20.0,
               juce::String ("the 190 Hz body is present (") + juce::String (body / trough, 1)
                   + "x the trough)");
        check (rattle > trough * 5.0,
               juce::String ("the highpassed rattle is present (") + juce::String (rattle / trough, 1)
                   + "x the trough)");

        // And the body really is the louder half, per the spec's 0.6v noise
        // against 0.4v triangle summed over very different bandwidths.
        check (body > rattle,
               "the body dominates the rattle, as the spec's amplitudes imply");
    }

    void testVoiceDurations()
    {
        section ("durations follow the spec, and DECAY scales them");

        for (int lane = 1; lane < forrobox::State::kNumLanes; ++lane)
        {
            const auto name = juce::String (laneName (lane));
            const auto velocity01 = 127.0f / 127.0f;
            const auto expected = specDurationSeconds (lane, velocity01, defaultDecayForLane (lane));

            auto buffer = renderSingleHit (lane, 127);
            const auto measured = static_cast<double> (findTailEnd (buffer)) / kSampleRate;

            check (measured > 0.0, name + " has a measurable tail");

            // The bound is wide on purpose and still useful. The envelope
            // reaches an ABSOLUTE 0.0001 at the spec duration, while this
            // measures -60 dB relative to the voice's own peak, so the measured
            // length is a peak-dependent fraction of the spec figure — between
            // roughly 0.6x and 1.0x across these eight voices. What it catches
            // is the class of error that matters: a duration out by 2x, or a
            // decayScale formula that is wrong.
            check (measured > expected * 0.45 && measured < expected * 1.25,
                   name + " lasts about as long as the spec says (" + juce::String (measured, 4)
                        + " s measured against " + juce::String (expected, 4) + " s)");
        }

        // DECAY as a RATIO, which needs no envelope arithmetic at all:
        // decayScale spans 0.4 to 1.8, so DECAY 100 must last 4.5x DECAY 0.
        // This is the assertion that would catch a wrong decayScale formula,
        // which the absolute bound above is too loose to see.
        for (const int lane : { 4, 7 })
        {
            const auto name = juce::String (laneName (lane));
            const auto channel = forrobox::VoiceEngine::channelForLane (lane);
            const auto* channelId = forrobox::ids::channelInfos[static_cast<size_t> (channel)].id;

            double lengths[2] {};

            for (int i = 0; i < 2; ++i)
            {
                AudioRig rig;
                rig.setValue (forrobox::ids::channelParam (channelId, forrobox::ids::decay),
                              i == 0 ? 0.0f : 100.0f);
                rig.setStep (lane, 0, 127);

                auto buffer = rig.render (96000);
                lengths[i] = static_cast<double> (findTailEnd (buffer)) / kSampleRate;
            }

            check (lengths[0] > 0.0 && lengths[1] > 0.0, name + ": both DECAY renders sound");

            const auto ratio = lengths[1] / juce::jmax (1.0e-6, lengths[0]);

            check (ratio > 3.6 && ratio < 5.4,
                   name + ": DECAY 100 lasts ~4.5x DECAY 0 (measured " + juce::String (ratio, 2)
                        + "x), so decayScale = 0.4 + decay/100 x 1.4 holds");
        }
    }

    void testTrianguloArticulation()
    {
        section ("triângulo: the velocity split is its defining behaviour");

        // The spec's threshold is v > 0.55, and v = velocity / 127, so 69/127 =
        // 0.543 is closed and 71/127 = 0.559 is open. Two velocities two apart
        // must produce durations 7.5x apart (0.45 / 0.06).
        auto closed = renderSingleHit (1, 69);
        auto open   = renderSingleHit (1, 71);

        const auto closedLength = static_cast<double> (findTailEnd (closed)) / kSampleRate;
        const auto openLength   = static_cast<double> (findTailEnd (open)) / kSampleRate;

        check (closedLength > 0.0 && openLength > 0.0, "both articulations sound");
        check (openLength > closedLength * 3.0,
               juce::String ("velocity 71 rings far longer than velocity 69 (")
                   + juce::String (openLength, 4) + " s against "
                   + juce::String (closedLength, 4) + " s)");

        // The boundary velocity itself is CLOSED — the spec says `v > 0.55`,
        // not `>=`. 70/127 = 0.5512, just above, so 69 is the last closed one.
        // Asserting the direction of the comparison, which an off-by-one in the
        // operator would flip.
        auto justBelow = renderSingleHit (1, 69);
        auto justAbove = renderSingleHit (1, 70);

        check (static_cast<double> (findTailEnd (justAbove))
                 > static_cast<double> (findTailEnd (justBelow)) * 3.0,
               "the split sits between velocity 69 and 70, where v crosses 0.55");
    }
}

namespace
{
    // ── AC-1: onsets land where the clock says, including mid-block ──────────

    void testOnsetAccuracy()
    {
        section ("onsets land at the step's own sample position");

        // Step 4 at 120 BPM / 48 kHz is sample 24000. With 512-sample blocks
        // that is block 46, offset 448 — deliberately NOT a block boundary.
        //
        // This is the first thing in the project ever to read
        // StepEvent::sampleOffset. Phase 2 built the field and consumed only
        // event.step, so an emitter that passed 0 instead would have gone
        // unnoticed: it would place the hit at 23552, the start of its block.
        constexpr int step = 4;
        constexpr int blockSize = 512;
        const auto expected = static_cast<int> (step * kStepSamples);

        checkEqual (expected % blockSize, 448, "step 4 really does fall mid-block");

        for (const int lane : { 0, 4 })
        {
            AudioRig rig { kSampleRate, blockSize };
            rig.setStep (lane, step, 127);

            auto buffer = rig.render (48000, blockSize);
            const auto onset = firstNonZeroSample (buffer);

            check (onset >= 0, juce::String (laneName (lane)) + " produced output");

            // Within two samples, not one: a voice whose first sample is
            // legitimately zero — a sine at phase 0, or a file beginning at
            // zero crossing — writes nothing at its own start sample. The claim
            // that matters is on the other side and is exact.
            check (onset >= expected && onset <= expected + 2,
                   juce::String (laneName (lane)) + " starts at sample " + juce::String (expected)
                       + " (measured " + juce::String (onset) + ")");

            // Nothing at all before the step. Exact, and this is the half that
            // catches a dropped sampleOffset: passing 0 instead would put the
            // hit at 23552, the start of its block, 448 samples early.
            auto energyBefore = 0.0;

            for (int i = 0; i < expected; ++i)
                for (int c = 0; c < 2; ++c)
                    energyBefore += std::abs (static_cast<double> (buffer.getSample (c, i)));

            check (! (energyBefore > 0.0),
                   juce::String (laneName (lane))
                       + " writes nothing before its step, so event.sampleOffset is honoured"
                       + (energyBefore > 0.0 ? juce::String (" (leaked ")
                                                 + juce::String (energyBefore, 9) + ")"
                                             : juce::String()));
        }

        // Step 0 at sample 0, the simplest case that would still catch an
        // off-by-one-block error in the render call order.
        AudioRig atZero { kSampleRate, blockSize };
        atZero.setStep (4, 0, 127);

        auto buffer = atZero.render (24000, blockSize);
        const auto zeroOnset = firstNonZeroSample (buffer);
        check (zeroOnset >= 0 && zeroOnset <= 2,
               juce::String ("a step-0 hit starts at sample 0 (measured ")
                   + juce::String (zeroOnset) + ")");
    }

    void testBlockSizeIndependence()
    {
        section ("rendering does not depend on the host's block size");

        // Lane 4 (bateria BB) is a pure swept sine: it draws no random numbers
        // at trigger and none per sample, so it is the one voice whose output
        // can be compared SAMPLE BY SAMPLE across block sizes. The noise voices
        // cannot be — the render loop interleaves per-voice noise draws
        // differently at a different block size, which changes the noise
        // without changing the timing.
        constexpr int numSamples = 24576;   // divisible by 32, 512 and 2048

        AudioRig tiny { kSampleRate, 32 };
        tiny.setStep (4, 4, 120);
        auto small = tiny.render (numSamples, 32);

        AudioRig huge { kSampleRate, 2048 };
        huge.setStep (4, 4, 120);
        auto large = huge.render (numSamples, 2048);

        // Compared allowing a shift of at most one sample, which is Phase 2's
        // stated partition-equality standard: "indices exact, positions within
        // one sample". Demanding bit-identity here was wrong and the test
        // caught it — the internal path integrates positionInSteps once per
        // block, so a different block size lands the step up to a sample
        // apart, and a 1-sample shift of a 130 Hz sine at this level is a
        // 0.018 sample difference. Bit-identity within one partitioning is
        // asserted separately, by testDeterminism.
        auto best = 1.0f;
        auto bestShift = 99;

        for (const int shift : { -1, 0, 1 })
        {
            auto worst = 0.0f;

            for (int i = 2; i < numSamples - 2; ++i)
                worst = juce::jmax (worst, std::abs (small.getSample (0, i)
                                                       - large.getSample (0, i + shift)));

            if (worst < best)
            {
                best = worst;
                bestShift = shift;
            }
        }

        check (bufferPeak (small) > 0.001f, "the 32-sample-block render is not silent");
        check (best < 1.0e-6f,
               juce::String ("a sine voice renders identically at block 32 and 2048 up to a "
                             "one-sample shift (worst difference ") + juce::String (best, 9)
                   + " at shift " + juce::String (bestShift) + ")");
        check (std::abs (bestShift) <= 1,
               "and the shift needed is at most one sample, as Phase 2's standard allows");

        // And for a noise voice, the timing still has to match even though the
        // samples cannot: onset within one sample, which is Phase 2's stated
        // partition-equality standard.
        AudioRig noisyTiny { kSampleRate, 32 };
        noisyTiny.setStep (6, 4, 120);
        AudioRig noisyHuge { kSampleRate, 2048 };
        noisyHuge.setStep (6, 4, 120);

        auto noisySmall = noisyTiny.render (24576, 32);
        auto noisyLarge = noisyHuge.render (24576, 2048);
        const auto onsetSmall = firstNonZeroSample (noisySmall);
        const auto onsetLarge = firstNonZeroSample (noisyLarge);

        check (onsetSmall >= 0 && onsetLarge >= 0, "the noise voice sounds at both block sizes");
        check (std::abs (onsetSmall - onsetLarge) <= 1,
               juce::String ("a noise voice's onset matches within one sample (")
                   + juce::String (onsetSmall) + " against " + juce::String (onsetLarge) + ")");
    }

    void testDeterminism()
    {
        section ("two identical renders are identical");

        // Two FRESH processors, not one rendered twice: prepareToPlay reseeds
        // the engine's RNG, and a stop/start deliberately does NOT — a voice
        // ringing across a transport stop is correct behaviour, so the seed
        // cannot be reset there. "Identical parameters" therefore means a
        // freshly prepared instance, which is also the state a host bounce
        // starts from.
        const auto renderOnce = []
        {
            AudioRig rig;
            rig.setStep (0, 0, 100);
            rig.setStep (1, 2, 90);
            rig.setStep (6, 1, 70);
            return rig.render (24576);
        };

        auto first  = renderOnce();
        auto second = renderOnce();

        auto worst = 0.0f;

        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < first.getNumSamples(); ++i)
                worst = juce::jmax (worst, std::abs (first.getSample (c, i) - second.getSample (c, i)));

        check (bufferPeak (first) > 0.001f, "the render is not silent");
        checkEqual (worst, 0.0f, "two fresh renders are bit-identical, noise included");
    }
}

namespace
{
    // ── AC-3: VOL, PITCH and PAN ────────────────────────────────────────────

    void testChannelParameters()
    {
        section ("VOL, PITCH and PAN");

        const auto* bateria = forrobox::ids::channelInfos[4].id;

        // VOL is a linear gain: 25 against 100 is a quarter.
        float peaks[2] {};

        for (int i = 0; i < 2; ++i)
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::vol),
                          i == 0 ? 25.0f : 100.0f);
            rig.setStep (4, 0, 127);
            peaks[i] = bufferPeak (rig.render (24576));
        }

        check (peaks[0] > 0.0f && peaks[1] > 0.0f, "both VOL settings sound");
        checkEqual (peaks[1] / peaks[0], 4.0f, "VOL 100 is exactly 4x VOL 25 (linear gain)");

        // PITCH is 2^(semitones/12) applied to the oscillator: +12 doubles the
        // frequency. Measured on TOM, whose 190->110 Hz sweep is well clear of
        // both band edges at both pitches.
        double lowBand[2] {}, highBand[2] {};

        for (int i = 0; i < 2; ++i)
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::pitch),
                          i == 0 ? 0.0f : 12.0f);
            rig.setStep (7, 0, 127);

            auto buffer = rig.render (48000);
            lowBand[i]  = bandEnergy (buffer, 100.0, 220.0);
            highBand[i] = bandEnergy (buffer, 220.0, 440.0);
        }

        check (lowBand[0] > highBand[0], "at PITCH 0 the TOM's energy is in 100-220 Hz");
        check (highBand[1] > lowBand[1], "at PITCH +12 it has moved into 220-440 Hz");

        // PAN. The seven synthesised voices are mono and take Web Audio's
        // equal-power law, so hard left is silent on the right and centre is
        // -3 dB on both.
        struct PanCase { float pan; const char* name; };

        for (const auto& panCase : std::array<PanCase, 3> {{ { -100.0f, "hard left" },
                                                             {    0.0f, "centred" },
                                                             {  100.0f, "hard right" } }})
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::pan), panCase.pan);
            rig.setStep (4, 0, 127);

            auto buffer = rig.render (24576);
            const auto left  = buffer.getMagnitude (0, 0, buffer.getNumSamples());
            const auto right = buffer.getMagnitude (1, 0, buffer.getNumSamples());

            if (panCase.pan < 0.0f)
                check (left > right * 50.0f, juce::String (panCase.name) + " puts the energy left");
            else if (panCase.pan > 0.0f)
                check (right > left * 50.0f, juce::String (panCase.name) + " puts the energy right");
            else
                checkEqual (left, right, juce::String (panCase.name) + " is equal on both sides");
        }

        // The sampled zabumba takes the STEREO law instead, because its files
        // are true stereo rather than dual-mono. Panning it hard left must
        // attenuate the right side and fold it inwards — which leaves the LEFT
        // channel at full level, where the mono law would drop it to unity-gain
        // cos(0) as well. Distinguishing the two laws, not just checking that
        // panning does something.
        AudioRig centred;
        centred.setStep (0, 0, 127);
        auto zabumbaCentred = centred.render (48000);

        AudioRig hardLeft;
        hardLeft.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                        forrobox::ids::pan), -100.0f);
        hardLeft.setStep (0, 0, 127);
        auto zabumbaLeft = hardLeft.render (48000);

        const auto centredLeft = zabumbaCentred.getMagnitude (0, 0, zabumbaCentred.getNumSamples());
        const auto leftLeft    = zabumbaLeft.getMagnitude (0, 0, zabumbaLeft.getNumSamples());
        const auto leftRight   = zabumbaLeft.getMagnitude (1, 0, zabumbaLeft.getNumSamples());

        check (leftRight < leftLeft * 0.05f, "a hard-left zabumba is near-silent on the right");
        check (leftLeft > centredLeft * 0.95f,
               juce::String ("and its LEFT channel keeps full level (") + juce::String (leftLeft, 4)
                   + " against " + juce::String (centredLeft, 4)
                   + ") — the stereo pan law, not the mono one");
    }

    void testVelocityMonotonicForSynthVoices()
    {
        section ("synthesised voices: peak rises with velocity");

        // Peak here, unlike the sampled lane: the envelope's peak is literally
        // peakPerVelocity x v, so this is exact rather than approximate.
        for (const int lane : { 1, 3, 4, 7 })
        {
            const auto name = juce::String (laneName (lane));
            auto previous = 0.0f;
            auto falls = 0;

            for (const std::uint8_t velocity : std::array<std::uint8_t, 6> { 20, 40, 60, 80, 100, 127 })
            {
                auto buffer = renderSingleHit (lane, velocity, 1.5);
                const auto peak = bufferPeak (buffer);

                if (previous > 0.0f && peak <= previous)
                    ++falls;

                previous = peak;
            }

            // The triângulo is exempt from a simple "always rises" claim: its
            // articulation changes at v > 0.55, and an open note spread over
            // 7.5x the time through the same bandpass does not have to peak
            // higher than a closed one. Its velocity response is checked as
            // ENERGY instead.
            if (lane == 1)
            {
                auto quiet = renderSingleHit (1, 20, 1.5);
                auto loud  = renderSingleHit (1, 127, 1.5);

                check (bufferRms (loud) > bufferRms (quiet) * 2.0,
                       name + ": velocity raises energy across the articulation split");
            }
            else
            {
                checkEqual (falls, 0, name + ": peak never falls as velocity rises");
            }
        }

        // Velocity 0 is silence, not a quiet hit — the pattern's rests must not
        // sound. A schedule() that treated 0 as "very quiet" would pass every
        // test above and fill every rest in the groove.
        auto silent = renderSingleHit (4, 0, 0.5);
        checkSilent (silent, "velocity 0 renders exact silence");
    }
}

namespace
{
    // ── AC-5: mute and solo ─────────────────────────────────────────────────

    void testMuteSoloTruthTable()
    {
        section ("mute and solo across all 32 combinations");

        constexpr int numChannels = forrobox::State::kNumChannels;

        auto combinationsChecked = 0;
        auto wrong = 0;

        // The full truth table is swept through resolveChannelSettings rather
        // than by rendering 1024 buffers. The rendered half of the claim — that
        // an inaudible channel really is silent — is checked separately below,
        // so neither half is taken on trust.
        for (int muteMask = 0; muteMask < (1 << numChannels); ++muteMask)
        {
            for (int soloMask = 0; soloMask < (1 << numChannels); ++soloMask)
            {
                AudioRig rig;

                for (int c = 0; c < numChannels; ++c)
                {
                    const auto* id = forrobox::ids::channelInfos[static_cast<size_t> (c)].id;
                    rig.setValue (forrobox::ids::channelParam (id, forrobox::ids::mute),
                                  (muteMask & (1 << c)) != 0 ? 1.0f : 0.0f);
                    rig.setValue (forrobox::ids::channelParam (id, forrobox::ids::solo),
                                  (soloMask & (1 << c)) != 0 ? 1.0f : 0.0f);
                }

                const auto settings = rig.processor.resolveChannelSettings();
                const auto anySoloed = soloMask != 0;

                for (int c = 0; c < numChannels; ++c)
                {
                    const auto soloed = (soloMask & (1 << c)) != 0;
                    const auto muted  = (muteMask & (1 << c)) != 0;

                    // Solo wins over mute for a channel that is both: solo
                    // decides which channels are in the mix at all.
                    const auto expected = anySoloed ? soloed : ! muted;

                    ++combinationsChecked;

                    if (settings[static_cast<size_t> (c)].audible != expected)
                    {
                        ++wrong;

                        // Report the first few rather than 5120 lines.
                        if (wrong <= 4)
                            check (false, juce::String ("mute mask ") + juce::String (muteMask)
                                            + " solo mask " + juce::String (soloMask)
                                            + " channel " + juce::String (c)
                                            + ": expected audible=" + juce::String (expected ? 1 : 0));
                    }
                }
            }
        }

        // Counted, not asserted-as-true. `check (true, ...)` here would be the
        // fifth assertion in this project that cannot fail, and the count is
        // what proves the sweep ran at all: a `numChannels` that came back 0,
        // or a loop bound that collapsed, would leave this at zero.
        checkEqual (combinationsChecked, 32 * 32 * numChannels,
                    "the sweep covered all 32 mute x 32 solo combinations, five channels each");
        checkEqual (wrong, 0, "every combination resolves as PLANNING.md specifies");
    }

    void testMuteSoloSilencesAudio()
    {
        section ("mute and solo actually silence audio");

        const auto* bateria = forrobox::ids::channelInfos[4].id;
        const auto* ganza   = forrobox::ids::channelInfos[3].id;

        // Muting silences.
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::mute), 1.0f);
            rig.setStep (4, 0, 127);
            checkSilent (rig.render (24576), "a muted channel renders silence");
        }

        // Bateria's mute covers all FOUR of its lanes, not just the one the
        // collapsed sequencer row edits. Four separate assertions, because a
        // mapping that happened to cover CX and miss HH would pass a single one.
        for (const int lane : { 4, 5, 6, 7 })
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::mute), 1.0f);
            rig.setStep (lane, 0, 127);

            checkSilent (rig.render (24576),
                         juce::String ("muting BATERIA silences its ") + laneName (lane) + " lane");
        }

        // Soloing one channel silences the others.
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (ganza, forrobox::ids::solo), 1.0f);
            rig.setStep (4, 0, 127);      // bateria, not soloed
            checkSilent (rig.render (24576),
                         "a non-soloed channel is silent while another is soloed");
        }

        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (ganza, forrobox::ids::solo), 1.0f);
            rig.setStep (3, 0, 127);      // ganzá, soloed
            check (bufferPeak (rig.render (24576)) > 0.0005f, "the soloed channel itself sounds");
        }

        // Both muted and soloed: it sounds. The combination a naive
        // `audible = !muted && (!anySolo || soloed)` gets wrong.
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (ganza, forrobox::ids::mute), 1.0f);
            rig.setValue (forrobox::ids::channelParam (ganza, forrobox::ids::solo), 1.0f);
            rig.setStep (3, 0, 127);
            check (bufferPeak (rig.render (24576)) > 0.0005f,
                   "a channel that is both muted and soloed sounds — solo wins");
        }

        // Muting must not consume voices from channels that are still audible:
        // the gate is applied at schedule time, so a muted lane costs nothing.
        {
            AudioRig rig;
            rig.setValue (forrobox::ids::channelParam (bateria, forrobox::ids::mute), 1.0f);

            for (int step = 0; step < 16; ++step)
                for (const int lane : { 4, 5, 6, 7 })
                    rig.setStep (lane, step, 127);

            auto buffer = rig.render (24576);
            checkSilent (buffer, "64 muted hits render silence");
            checkEqual (rig.processor.getVoiceEngine().getActiveVoiceCount(), 0,
                        "and consume no voices at all");
        }
    }

    // ── AC-6: the audio-thread contract ─────────────────────────────────────

    void testNoAllocationsWhileRendering()
    {
        section ("audio-thread contract: no allocation while voices sound");

        AudioRig rig { kSampleRate, 512 };

        // A dense pattern: every lane on every step, so voices are constantly
        // triggered, stolen and retired during the measured window.
        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            for (int step = 0; step < 16; ++step)
                rig.setStep (lane, step, static_cast<std::uint8_t> (60 + (step * 4) % 60));

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        rig.processor.setPlaying (true);

        // Warm up outside the measured window: the first blocks touch the
        // pattern try-lock and JUCE's message-manager lazily, and counting
        // those would make the assertion about startup rather than steady state.
        for (int i = 0; i < 16; ++i)
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        }

        const auto before = fbtest::allocations.load (std::memory_order_relaxed);

        for (int i = 0; i < 2000; ++i)
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        }

        const auto after = fbtest::allocations.load (std::memory_order_relaxed);

        checkEqual (static_cast<long long> (after - before), 0LL,
                    "2000 blocks with voices sounding allocate nothing");

        // The counter must be able to register a reading, or the check above is
        // "zero because nothing is watching". This is the rule 02-01 earned:
        // assert the instrument works before trusting its silence.
        const auto beforeProbe = fbtest::allocations.load (std::memory_order_relaxed);
        {
            auto* probe = new int (7);
            check (*probe == 7, "the allocation probe allocated");
            delete probe;
        }
        check (fbtest::allocations.load (std::memory_order_relaxed) > beforeProbe,
               "the allocation counter registers a real allocation");

        // Voices were genuinely in flight during the window, not idle.
        check (rig.processor.getVoiceEngine().getActiveVoiceCount() > 0,
               "voices were sounding throughout the measured window");
        check (rig.processor.getEmittedStepCount() > 100,
               juce::String ("steps kept firing (") + juce::String (rig.processor.getEmittedStepCount())
                   + " emitted)");
    }

    void testVoicePoolUnderPressure()
    {
        section ("voice pool: dense patterns steal rather than break");

        AudioRig rig { kSampleRate, 512 };
        rig.setValue (forrobox::ids::bpm, 300.0f);      // the fastest supported tempo
        rig.setChoice (forrobox::ids::steps, 1);        // 32 steps

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            for (int step = 0; step < 32; ++step)
                rig.setStep (lane, step, 127);

        // Every channel at maximum DECAY, which is the worst case the pool was
        // sized against.
        for (const auto& info : forrobox::ids::channelInfos)
            rig.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::decay), 100.0f);

        auto buffer = rig.render (static_cast<int> (kSampleRate) * 4, 512);

        check (isFinite (buffer), "the densest possible pattern renders no NaN or infinity");
        check (bufferPeak (buffer) > 0.0f, "and is not silent");

        // Nothing is dropped for want of a voice. Stealing is acceptable and
        // sized for; dropping means the pool arithmetic in VoiceEngine.h is
        // wrong.
        checkEqual (rig.processor.getVoiceEngine().getVoicesDropped(), 0,
                    "no trigger is dropped for want of a voice");
    }
}

namespace
{
    void testProfileHeadroom()
    {
        section ("the four real grooves: what they render, and how hot");

        // Pins the number 03-03's limiter has to handle. Measured 2026-09-08:
        //
        //   CAMPINA GRANDE  132 BPM  swing 38  peak 0.814  (bateria muted)
        //   CARUARU         138 BPM  swing 54  peak 1.336
        //   PETROLINA       128 BPM  swing 26  peak 1.299
        //   UNIVERSITARIO   124 BPM  swing 16  peak 1.201
        //
        // Three of four sum past full scale with the parameters at their
        // profile defaults, which is EXPECTED at this point: the chain
        // PLANNING.md specifies is voices -> gain -> pan -> character bus ->
        // limiter -> master, and the last three are 03-03's. 03-01 deliberately
        // built no stand-in for them.
        //
        // Pinned rather than merely noted so that a later change to gain
        // staging cannot move it silently: if this fires, the limiter's design
        // input has changed and wants re-reading.
        auto worstPeak = 0.0f;
        const char* worstProfile = "";

        for (const auto& profile : forrobox::allProfiles())
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::bpm, static_cast<float> (profile.bpm));
            rig.setValue (forrobox::ids::swing, profile.swing);

            {
                auto state = rig.processor.lockPatternState();
                forrobox::applyProfile (*state, profile);
            }

            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                       forrobox::ids::mute),
                          profile.bateriaMuted ? 1.0f : 0.0f);

            auto buffer = rig.render (98304, 512);
            const auto peak = bufferPeak (buffer);
            const auto name = juce::String (profile.displayName());

            check (peak > 0.1f, name + " renders a substantial groove (peak "
                                     + juce::String (peak, 4) + ")");
            check (isFinite (buffer), name + " renders no NaN or infinity");

            // Every lane the profile writes must be audible in the render. This
            // is what would catch a lane silently mapped to the wrong voice, or
            // a profile lane that never reaches the engine at all.
            check (rig.processor.getEmittedStepCount() > 0, name + " emitted steps");

            if (peak > worstPeak)
            {
                worstPeak = peak;
                worstProfile = profile.displayName();
            }
        }

        check (worstPeak > 1.15f && worstPeak < 1.55f,
               juce::String ("the hottest profile (") + worstProfile + ") peaks at "
                   + juce::String (worstPeak, 3)
                   + " — pinned as 03-03's limiter design input, ~+2.5 dBFS of summed material");

        // CAMPINA GRANDE, the default on load, is the one profile that does NOT
        // clip, because it mutes the bateria. Worth its own assertion: it is
        // what a user hears first.
        const auto* campina = forrobox::findProfile (forrobox::ids::defaultProfile);
        check (campina != nullptr, "the default profile resolves");

        if (campina != nullptr)
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::bpm, static_cast<float> (campina->bpm));
            rig.setValue (forrobox::ids::swing, campina->swing);

            {
                auto state = rig.processor.lockPatternState();
                forrobox::applyProfile (*state, *campina);
            }

            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                       forrobox::ids::mute), 1.0f);

            const auto peak = bufferPeak (rig.render (98304, 512));

            check (peak < 1.0f,
                   juce::String ("the default groove does not clip even without a limiter (peak ")
                       + juce::String (peak, 4) + ")");
        }
    }

    void testLaneToChannelMapping()
    {
        section ("lane to channel mapping is derived, not written down");

        // The four kit lanes must all land on BATERIA, and the other four on
        // their own channels. Checked against the id lists rather than against
        // literals, so this test cannot agree with a wrong table for the same
        // reason the table is wrong.
        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
        {
            const auto channel = forrobox::VoiceEngine::channelForLane (lane);

            check (juce::isPositiveAndBelow (channel, forrobox::State::kNumChannels),
                   juce::String ("lane ") + laneName (lane) + " maps to a real channel");

            const auto* laneId = laneName (lane);
            const auto* channelId = forrobox::ids::channelInfos[static_cast<size_t> (channel)].id;

            auto laneHasOwnChannel = false;

            for (const auto& info : forrobox::ids::channelInfos)
                if (juce::String (info.id) == juce::String (laneId))
                    laneHasOwnChannel = true;

            if (laneHasOwnChannel)
                check (juce::String (channelId) == juce::String (laneId),
                       juce::String ("lane ") + laneId + " maps to its own channel");
            else
                check (juce::String (channelId) == juce::String ("bateria"),
                       juce::String ("kit lane ") + laneId + " maps to the composite channel");
        }

        // Out-of-range indices fall back rather than reading past the table.
        checkEqual (forrobox::VoiceEngine::channelForLane (-1), 0, "a negative lane falls back to 0");
        checkEqual (forrobox::VoiceEngine::channelForLane (99), 0, "an over-range lane falls back to 0");
    }

    void testTailIsReportedToHost()
    {
        section ("the plugin reports its tail");

        AudioRig rig;

        // Was 0.0 while the plugin was silent. A host bouncing to disk stops
        // rendering after the last event plus the reported tail, so a 0 here
        // truncates every decay in the bounce.
        check (rig.processor.getTailLengthSeconds() > 0.5,
               juce::String ("getTailLengthSeconds reports a real tail (")
                   + juce::String (rig.processor.getTailLengthSeconds(), 3) + " s)");

        // And it is not shorter than the longest voice can actually ring: the
        // 0.498 s zabumba layer pitched down an octave.
        const auto& sampler = rig.processor.getVoiceEngine().getSampler();
        auto longestLayer = 0;

        for (int slot = 0; slot < sampler.getNumSlots(); ++slot)
            longestLayer = juce::jmax (longestLayer, sampler.getLengthSamples (slot));

        const auto worstCaseSeconds = static_cast<double> (longestLayer)
                                        / sampler.getFileSampleRate() * 2.0;   // PITCH -12

        check (rig.processor.getTailLengthSeconds() >= worstCaseSeconds * 0.99,
               juce::String ("the reported tail covers the longest possible voice (")
                   + juce::String (worstCaseSeconds, 3) + " s)");
    }

    void testVoicesRingThroughTransportStop()
    {
        section ("stopping the transport does not cut a sounding voice");

        // PLANNING.md's "stopping clears the playhead and all playing pad
        // outlines, and resets the step counter to 0" is about visible state.
        // Hard-cutting a voice mid-decay would click, and every instrument lets
        // a struck note finish.
        AudioRig rig { kSampleRate, 512 };
        rig.setStep (0, 0, 127);          // the zabumba, whose tail is longest

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        rig.processor.setPlaying (true);

        // Two blocks: enough for the hit to start and be well into its decay.
        for (int i = 0; i < 2; ++i)
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        }

        rig.processor.setPlaying (false);

        block.clear();
        midi.clear();
        rig.processor.processBlock (block, midi);

        check (bufferPeak (block) > 0.0005f,
               "the zabumba is still sounding in the block after the transport stopped");
        checkEqual (rig.processor.getCurrentStep(), forrobox::Clock::kStoppedStep,
                    "while the playhead does report stopped");

        // It does eventually go quiet rather than ringing forever.
        for (int i = 0; i < 200; ++i)
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        }

        checkSilent (block, "and it has fallen silent by two seconds later");
    }
}

/** Renders every profile to a WAV for A/B listening against the prototype.

    Not a test — nothing here asserts. Phase 3's goal is that the grooves
    audibly match, and that judgement is a person's; this exists so the person
    has something to play. Writes outside the repository: audio renders are
    output, not source.

    Deliberately part of the one test executable rather than a second target. A
    second executable re-compiles the whole JUCE module set, which is why there
    is only one. */
void renderAuditionFiles (const juce::String& outputDirectory)
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
                             .getChildFile (outputDirectory);
    directory.createDirectory();

    std::cout << "Rendering auditions to " << directory.getFullPathName() << "\n";

    for (const auto& profile : forrobox::allProfiles())
    {
        AudioRig rig { kSampleRate, 512 };

        // The profile's own tempo and swing, so what is rendered is what the
        // prototype plays at the same settings.
        rig.setValue (forrobox::ids::bpm, static_cast<float> (profile.bpm));
        rig.setValue (forrobox::ids::swing, profile.swing);

        // CACHACA is deliberately NOT applied: 03-02 owns humanisation, and
        // rendering with it set would suggest it does something when it does
        // not yet.
        rig.setValue (forrobox::ids::cachaca, 0.0f);

        {
            auto state = rig.processor.lockPatternState();
            forrobox::applyProfile (*state, profile);
        }

        // The profile's bateria mute, which is data the profile carries.
        rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                   forrobox::ids::mute),
                      profile.bateriaMuted ? 1.0f : 0.0f);

        // Four bars at the profile's tempo, plus a tail.
        const auto barSeconds = 4.0 * 60.0 / static_cast<double> (profile.bpm);
        const auto numSamples = static_cast<int> ((barSeconds * 4.0 + 1.0) * kSampleRate);

        auto buffer = rig.render (numSamples - numSamples % 512, 512);

        const auto rawPeak = bufferPeak (buffer);

        // Normalised to -3 dBFS, and the applied gain is printed.
        //
        // Three of the four profiles sum past 1.0 — CARUARU measures 1.34 —
        // because the limiter (-6 dB, 20:1) and the master's squared taper are
        // 03-03's, and this plan deliberately did not build a stand-in for
        // them. Writing a clipped file would make the A/B comparison about
        // clipping rather than about the grooves, so the audition normalises
        // and says so. The real gain staging arrives with the limiter.
        const auto target = juce::Decibels::decibelsToGain (-3.0f);
        const auto normalisation = rawPeak > 0.0f ? target / rawPeak : 1.0f;

        buffer.applyGain (normalisation);

        const auto file = directory.getChildFile (juce::String (profile.id()) + ".wav");
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream() };

        if (stream == nullptr)
        {
            std::cout << "  FAILED to open " << file.getFullPathName() << "\n";
            continue;
        }

        const auto options = juce::AudioFormatWriterOptions()
                               .withSampleRate (kSampleRate)
                               .withNumChannels (2)
                               .withBitsPerSample (24);

        auto writer = wav.createWriterFor (stream, options);

        if (writer == nullptr)
        {
            std::cout << "  FAILED to create a writer for " << file.getFullPathName() << "\n";
            continue;
        }

        writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
        writer.reset();

        std::cout << "  " << profile.displayName()
                  << "  " << profile.bpm << " BPM"
                  << "  swing " << juce::String (profile.swing, 0).toStdString()
                  << "  raw peak " << juce::String (rawPeak, 4).toStdString()
                  << (rawPeak > 1.0f ? " (CLIPS — awaiting 03-03's limiter)" : "")
                  << "  normalised by " << juce::String (juce::Decibels::gainToDecibels (normalisation), 1).toStdString()
                  << " dB  -> " << file.getFileName() << "\n";
    }
}

void runVoiceTests()
{
    testSamplerClassification();
    testSamplerVelocityBlend();
    testLayersAreLevelMatched();
    testPitchTracksPerComponent();
    testZabumbaRendersAtBothRates();
    testZabumbaVelocityIsMonotonic();
    testVoiceSpectra();
    testCaixaHasBothLayers();
    testVoiceDurations();
    testTrianguloArticulation();
    testOnsetAccuracy();
    testBlockSizeIndependence();
    testDeterminism();
    testChannelParameters();
    testVelocityMonotonicForSynthVoices();
    testMuteSoloTruthTable();
    testMuteSoloSilencesAudio();
    testNoAllocationsWhileRendering();
    testVoicePoolUnderPressure();
    testProfileHeadroom();
    testLaneToChannelMapping();
    testTailIsReportedToHost();
    testVoicesRingThroughTransportStop();
}

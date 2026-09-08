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
#include <vector>
#include <cmath>

using namespace fbtest;

namespace
{
    constexpr double kSampleRate = 48000.0;
    constexpr double kBpm = 120.0;

    /** Samples per sixteenth at kBpm / kSampleRate: 60/120/4 * 48000 = 6000. */
    constexpr double kStepSamples = 6000.0;

    // ── measurement ─────────────────────────────────────────────────────────

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

            // CACHAÇA off, for the same reason swing is off: its default is 22,
            // not 0, so leaving it alone makes every timing and level assertion
            // in this suite stochastic. The humanisation tests set it
            // explicitly.
            //
            // It is not a cosmetic default either. At CACHAÇA 22 the velocity
            // multiplier spans (0.945, 1.0], which is enough to push velocity
            // 71 — normalised 0.559 — below the triângulo's 0.55 articulation
            // threshold and flip the note from open to closed. The prototype
            // does exactly the same thing, so that is faithful behaviour, not
            // a bug; it just cannot be tested at the same time as the boundary.
            setValue (forrobox::ids::cachaca, 0.0f);

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
                // Ghosts off by default: their per-channel defaults are 6-14%,
                // so a "one hit on one lane" test would otherwise measure
                // several.
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost), 0.0f);
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

    /** The duration the spec gives a lane at a velocity and a DECAY.

        Uses production's own decayScaleFor. The local copy this replaced
        divided by a literal 100.0 rather than ids::kPercentMax — the same
        drift that halved every pan earlier in this plan, and it would have made
        the test agree with a wrong implementation for the same reason the
        implementation was wrong. */
    double specDurationSeconds (int lane, float velocity01, float decayPercent)
    {
        const auto& spec = forrobox::voiceSpecs[static_cast<size_t> (lane)];

        const auto base = (spec.openThreshold > 0.0f && velocity01 > spec.openThreshold)
                            ? spec.openDurBase
                            : spec.durBase;

        return (static_cast<double> (base) + static_cast<double> (spec.durPerVelocity) * velocity01)
                 * static_cast<double> (forrobox::decayScaleFor (decayPercent));
    }
}

namespace
{
    // ── the instruments, before anything that uses them ─────────────────────

    void testMeasurementInstruments()
    {
        section ("the measurement instruments measure what they claim");

        // Every audio claim in this project rests on these helpers, and across
        // 03-01 and 03-02 the MEASUREMENT was wrong before the code was SEVEN
        // times: comparing lanes across separate renders; the peak of filtered
        // noise as a velocity probe; a search radius reaching into the previous
        // hit's tail; a band that measured HH while claiming BB; a highpass that
        // attenuates rather than erases, which failed a test against CORRECT
        // code; a 2-sigma bound on 16 trials; and per-bucket floors that cannot
        // tell triangular from uniform.
        //
        // TestHarness.h already carries the right pattern for this in
        // checkAllocationCounterRegisters — "whoever asserts on it must also
        // prove it can register a reading" — and it was applied to one
        // instrument out of ten. These are the other nine, each against a
        // synthetic signal whose answer is known by construction.

        constexpr int length = 48000;

        // ── a known sine ────────────────────────────────────────────────────
        juce::AudioBuffer<float> sine (2, length);

        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < length; ++s)
                sine.setSample (c, s, 0.5f * std::sin (juce::MathConstants<float>::twoPi
                                                         * 1000.0f * static_cast<float> (s)
                                                         / static_cast<float> (kSampleRate)));

        checkEqual (bufferPeak (sine), 0.5f, "bufferPeak finds a known amplitude");
        checkEqual (bufferRms (sine), 0.5 / std::sqrt (2.0), "bufferRms matches a sine's 0.707 factor");
        check (isFinite (sine), "isFinite accepts finite audio");

        const auto atTone = fbtest::goertzelPower (sine.getReadPointer (0), length, 1000.0, kSampleRate);
        const auto offTone = fbtest::goertzelPower (sine.getReadPointer (0), length, 4000.0, kSampleRate);

        check (atTone > offTone * 1.0e6,
               juce::String ("goertzelPower separates a 1 kHz tone from 4 kHz by ")
                   + juce::String (atTone / juce::jmax (1.0e-12, offTone), 0) + "x");

        // bandEnergy is tested with NOISE, not with the sine above.
        //
        // Its grid steps by a semitone, so it can step OVER a pure tone: the
        // 900-1100 Hz band samples 900, 953.5, 1010.2 and 1070.3 and never
        // 1000 Hz, so a 1 kHz sine reads as leakage. That is a real limitation
        // of the instrument, found by writing this test, and now documented at
        // the helper — the voices it was written for are broadband or sweeping,
        // so it had never shown.
        juce::AudioBuffer<float> band (1, length);
        juce::Random noise { 1234 };

        for (int s = 0; s < length; ++s)
            band.setSample (0, s, noise.nextFloat() * 2.0f - 1.0f);

        auto highBandOnly = fbtest::highpassed (band, 8000.0, kSampleRate);

        check (bandEnergy (highBandOnly, 10000.0, 16000.0)
                 > bandEnergy (highBandOnly, 200.0, 400.0) * 100.0,
               juce::String ("bandEnergy separates broadband content by band (")
                   + juce::String (bandEnergy (highBandOnly, 10000.0, 16000.0)
                                     / juce::jmax (1.0e-12, bandEnergy (highBandOnly, 200.0, 400.0)), 0)
                   + "x)");

        // ── NaN and infinity are caught ─────────────────────────────────────
        juce::AudioBuffer<float> broken (1, 8);
        broken.clear();
        broken.setSample (0, 4, std::numeric_limits<float>::quiet_NaN());
        check (! isFinite (broken), "isFinite rejects a NaN");

        broken.setSample (0, 4, std::numeric_limits<float>::infinity());
        check (! isFinite (broken), "and an infinity");

        // ── onsets at known positions ───────────────────────────────────────
        juce::AudioBuffer<float> impulses (1, length);
        impulses.clear();

        constexpr int firstAt = 1000, spacing = 6000, count = 6;
        const std::array<int, count> displacements { 0, 250, -250, 100, -400, 0 };

        for (int i = 0; i < count; ++i)
            for (int s = 0; s < 300; ++s)   // a 300-sample burst, like a short voice
                impulses.setSample (0, firstAt + i * spacing + displacements[static_cast<size_t> (i)] + s,
                                    0.4f);

        checkEqual (fbtest::firstNonZeroSample (impulses), firstAt + displacements[0],
                    "firstNonZeroSample finds a known start");

        const auto measured = fbtest::measureHitDisplacements (impulses, firstAt, spacing, count, 1200);

        auto wrong = 0;

        for (int i = 0; i < count; ++i)
            if (measured[static_cast<size_t> (i)] != displacements[static_cast<size_t> (i)])
                ++wrong;

        checkEqual (wrong, 0, "measureHitDisplacements recovers known displacements exactly");

        // A hit outside its window reports notFound rather than the nearest
        // thing it can see — the failure mode that made a ghost's tail read as
        // the next step's onset.
        const auto tooFar = fbtest::measureHitDisplacements (impulses, firstAt, spacing, count, 50);
        auto found = 0;

        for (const auto d : tooFar)
            if (d != fbtest::notFound)
                ++found;

        check (found < count,
               juce::String ("and reports notFound when a hit is outside its window (")
                   + juce::String (count - found) + " of " + juce::String (count) + ")");

        // ── separate onsets are counted separately ──────────────────────────
        checkEqual (fbtest::countOnsets (impulses, 0, spacing), 1,
                    "countOnsets finds one onset in a window holding one burst");
        checkEqual (fbtest::countOnsets (impulses, 0, spacing * 2), 2,
                    "and two in a window holding two");
        checkEqual (fbtest::countOnsets (impulses, 0, 500), 0,
                    "and none before the first");

        // ── tail and note length ────────────────────────────────────────────
        juce::AudioBuffer<float> burst (1, length);
        burst.clear();

        for (int s = 2000; s < 5000; ++s)
            burst.setSample (0, s, 0.25f);

        checkEqual (fbtest::findTailEnd (burst), 4999, "findTailEnd finds the last audible sample");
        checkEqual (fbtest::renderedNoteLength (burst), 2999,
                    "renderedNoteLength measures from the onset, not from sample 0");

        // ── the highpass, asserted on its REJECTION RATIO ───────────────────
        //
        // This is the property whose absence caused a false failure: the filter
        // ATTENUATES, it does not erase, and a first version of the mute test
        // treated its 5e-4 residual as an onset. So what is asserted is how far
        // down the stopband goes, not that it reaches zero.
        juce::AudioBuffer<float> twoTone (1, length);

        for (int s = 0; s < length; ++s)
        {
            const auto phase = juce::MathConstants<float>::twoPi * static_cast<float> (s)
                                 / static_cast<float> (kSampleRate);
            twoTone.setSample (0, s, 0.5f * std::sin (phase * 130.0f)
                                   + 0.5f * std::sin (phase * 12000.0f));
        }

        auto filtered = fbtest::highpassed (twoTone, 4000.0, kSampleRate);

        const auto lowBefore = bandEnergy (twoTone, 100.0, 180.0);
        const auto lowAfter  = bandEnergy (filtered, 100.0, 180.0);

        check (lowAfter < lowBefore * 0.01,
               juce::String ("highpassed rejects a 130 Hz tone by at least 20 dB (")
                   + juce::String (10.0 * std::log10 (lowAfter / juce::jmax (1.0e-12, lowBefore)), 1)
                   + " dB)");
        // Measured at the grid point nearest the tone, not through bandEnergy,
        // for the reason above: 12 kHz falls between 11892 and 12599.
        const auto passedTone = fbtest::goertzelPower (filtered.getReadPointer (0), length,
                                                       12000.0, kSampleRate);
        const auto rejectedTone = fbtest::goertzelPower (filtered.getReadPointer (0), length,
                                                         130.0, kSampleRate);

        check (passedTone > rejectedTone * 1000.0,
               juce::String ("and passes a 12 kHz one (") 
                   + juce::String (10.0 * std::log10 (passedTone / juce::jmax (1.0e-12, rejectedTone)), 1)
                   + " dB apart)");
        check (lowAfter > 0.0,
               "while leaving a measurable residual — it attenuates, it does not erase, "
               "which is why its output needs a level-relative threshold");

        // ── exact silence ───────────────────────────────────────────────────
        juce::AudioBuffer<float> quiet (2, 64);
        quiet.clear();
        checkSilent (quiet, "checkSilent accepts an exactly silent buffer");
        checkEqual (fbtest::firstNonZeroSample (quiet), -1, "and firstNonZeroSample reports -1");
        checkEqual (fbtest::renderedNoteLength (quiet), -1, "and renderedNoteLength reports -1");
    }

    // ── AC-4: the sampled zabumba, and the classification behind it ─────────

    void testSamplerClassification()
    {
        section ("zabumba sampler: what the measurements found");

        AudioRig rig;
        const auto& sampler = rig.processor.getVoiceEngine().getSampler();

        checkEqual (sampler.getNumLoadedSlots(), 4, "all four embedded one-shots decoded");
        check (sampler.isReady(), "the sampler has at least one velocity layer");

        // Per slot, not once for the set. A single rate taken from the first
        // file that decoded made the class's own promise false for a mixed-rate
        // set — a replacement at 44.1 kHz played 8.8% fast while the others
        // stayed correct — and the archive these came from does contain 44.1
        // kHz material. Every slot is asked, and each must also carry its own
        // read rate.
        for (int slot = 0; slot < forrobox::ZabumbaSampler::kMaxSlots; ++slot)
        {
            if (! sampler.isLoaded (slot))
                continue;

            checkEqual (sampler.getFileSampleRate (slot), 48000.0,
                        juce::String ("slot ") + juce::String (slot) + " is 48 kHz");

            // At a 48 kHz host these are the identity; the point is that the
            // value is per slot at all, so a mixed-rate set cannot share one.
            checkEqual (sampler.getBaseReadRate (slot),
                        sampler.getFileSampleRate (slot) / kSampleRate,
                        juce::String ("slot ") + juce::String (slot)
                            + " carries its own read rate");
        }

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

        // A tripwire, not a behaviour test.
        //
        // render picks the PAN law from the source's channel count, because a
        // mono file through the stereo law comes out at 2x amplitude when
        // hard-panned. Every shipped file is stereo, so that fix is currently
        // unobservable — a control forcing the stereo law back left all checks
        // green. This fails the moment a mono file is added, which is when
        // someone needs to go and look at the pan path.
        for (int slot = 0; slot < forrobox::ZabumbaSampler::kMaxSlots; ++slot)
        {
            if (! sampler.isLoaded (slot))
                continue;

            checkEqual (sampler.getNumChannels (slot), 2,
                        juce::String ("slot ") + juce::String (slot)
                            + " is stereo — if this fails, the mono pan law is now reachable "
                              "and needs a real test");
        }
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

        // kMaxSlots and isLoaded, never a count: a file that fails to decode
        // leaves its index empty without shifting the others up, so a count as
        // an index bound would visit the hole and miss a real slot.
        for (int slot = 0; slot < forrobox::ZabumbaSampler::kMaxSlots; ++slot)
        {
            if (! sampler.isLoaded (slot) || ! sampler.isVelocityLayer (slot))
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

        const auto seconds48 = static_cast<double> (fbtest::renderedNoteLength (at48)) / 48000.0;
        const auto seconds44 = static_cast<double> (fbtest::renderedNoteLength (at44)) / 44100.0;

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
            const auto measured = static_cast<double> (fbtest::renderedNoteLength (buffer))
                                    / kSampleRate;

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
                lengths[i] = static_cast<double> (fbtest::renderedNoteLength (buffer))
                               / kSampleRate;
            }

            check (lengths[0] > 0.0 && lengths[1] > 0.0, name + ": both DECAY renders sound");

            const auto ratio = lengths[1] / juce::jmax (1.0e-6, lengths[0]);

            check (ratio > 3.6 && ratio < 5.4,
                   name + ": DECAY 100 lasts ~4.5x DECAY 0 (measured " + juce::String (ratio, 2)
                        + "x), so decayScale = 0.4 + decay/100 x 1.4 holds");
        }
    }

    void testFrequencySweeps()
    {
        section ("swept voices actually sweep");

        // Added after a negative control went UNDETECTED: forcing the sweep
        // ratio to 1, so the frequency never moves, left all 830 checks green.
        // The spectral test could not see it, because bb's loud band is
        // 45-150 Hz and its START frequency of 130 Hz is inside it — a voice
        // stuck at 130 looks exactly like one sweeping 130 -> 48.
        //
        // The obvious repair does not work either. bb has EIGHT TIMES more
        // energy near its sweep start than its end, because the envelope decays
        // exponentially while the frequency falls, so "more energy at f1" is
        // false for a correct sweep. And a static 130 Hz tone's Lorentzian
        // skirt puts roughly 5% of its power near 55 Hz against the sweep's
        // 12% — only 2.5x apart, which is not a bound worth trusting.
        //
        // A sweep is a property of frequency over TIME, so it is measured over
        // time: which frequency dominates early against which dominates late.
        // Measured margins are enormous — bb 16x early and 33x late, tom 75x
        // and 177x — so a 5x bound has room to be true and room to fail.
        struct Sweep { int lane; double f0, f1; };

        for (const auto& sweep : std::array<Sweep, 2> {{ { 4, 130.0, 48.0 }, { 7, 190.0, 110.0 } }})
        {
            const auto name = juce::String (laneName (sweep.lane));
            auto buffer = renderSingleHit (sweep.lane, 127);

            const auto seconds = specDurationSeconds (sweep.lane, 1.0f,
                                                      defaultDecayForLane (sweep.lane));
            const auto total = static_cast<int> (seconds * kSampleRate);
            const auto onset = fbtest::firstNonZeroSample (buffer);

            check (onset >= 0, name + ": the note sounds");

            // From the ONSET: every trigger is delayed by the 32 ms lookahead,
            // so reading from sample 0 would put both windows in silence.
            const auto* data = buffer.getReadPointer (0) + juce::jmax (0, onset);

            check (total > 0 && onset + total <= buffer.getNumSamples(),
                   name + ": the note fits the render");

            // The sweep spans a fraction of the note, so "late" is taken past
            // its end, where the frequency has reached f1 and holds.
            const auto earlyLength = static_cast<int> (0.12 * total);
            const auto lateStart   = static_cast<int> (0.62 * total);
            const auto lateLength  = total - lateStart;

            const auto earlyAtStart = fbtest::goertzelPower (data, earlyLength, sweep.f0, kSampleRate);
            const auto earlyAtEnd   = fbtest::goertzelPower (data, earlyLength, sweep.f1, kSampleRate);
            const auto lateAtStart  = fbtest::goertzelPower (data + lateStart, lateLength, sweep.f0, kSampleRate);
            const auto lateAtEnd    = fbtest::goertzelPower (data + lateStart, lateLength, sweep.f1, kSampleRate);

            check (earlyAtStart > earlyAtEnd * 5.0,
                   name + ": early on, " + juce::String (sweep.f0, 0) + " Hz dominates (ratio "
                        + juce::String (earlyAtStart / juce::jmax (1.0e-12, earlyAtEnd), 1) + ")");

            check (lateAtEnd > lateAtStart * 5.0,
                   name + ": by the end, " + juce::String (sweep.f1, 0) + " Hz dominates (ratio "
                        + juce::String (lateAtEnd / juce::jmax (1.0e-12, lateAtStart), 1)
                        + ") — the frequency really moved");
        }
    }

    void testTrianguloIsBandLimited()
    {
        section ("triângulo: the surviving harmonics are there");

        // Also added after an undetected control: forcing the odd-harmonic
        // count to 1 — which throws away the band-limiting and leaves five bare
        // sines — kept every check green. The spectral test looks at
        // 5000-12000 Hz, and the partials themselves are all in that band; the
        // harmonics it drops are the 3rd of the two lowest partials, at 16.2
        // and 20.6 kHz.
        //
        // Measured at 48 kHz: that 14-22 kHz band holds 0.00205 against the
        // 80-400 Hz floor's 0.000196, a ratio of 10.4. The bound is 4.
        auto buffer = renderSingleHit (1, 127);

        const auto harmonics = bandEnergy (buffer, 14000.0, 22000.0);
        const auto floorBand = bandEnergy (buffer, 80.0, 400.0);

        check (harmonics > floorBand * 4.0,
               juce::String ("the 3rd harmonics of the lower partials are present (ratio ")
                   + juce::String (harmonics / juce::jmax (1.0e-12, floorBand), 1) + ")");

        // And they are far below the partials themselves — 7.6 kHz bandpass at
        // Q 0.7 rolls them off — so this is band-limiting, not aliasing junk.
        check (harmonics < bandEnergy (buffer, 5000.0, 12000.0) * 0.05,
               "and well below the partials, as the bandpass requires");
    }

    void testTrianguloArticulation()
    {
        section ("triângulo: the velocity split is its defining behaviour");

        // The spec's threshold is v > 0.55, and v = velocity / 127, so 69/127 =
        // 0.543 is closed and 71/127 = 0.559 is open. Two velocities two apart
        // must produce durations 7.5x apart (0.45 / 0.06).
        auto closed = renderSingleHit (1, 69);
        auto open   = renderSingleHit (1, 71);

        const auto closedLength = static_cast<double> (fbtest::renderedNoteLength (closed)) / kSampleRate;
        const auto openLength   = static_cast<double> (fbtest::renderedNoteLength (open)) / kSampleRate;

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

        check (static_cast<double> (fbtest::renderedNoteLength (justAbove))
                 > static_cast<double> (fbtest::renderedNoteLength (justBelow)) * 3.0,
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
        const auto gridPosition = static_cast<int> (step * kStepSamples);

        checkEqual (gridPosition % blockSize, 448, "step 4 really does fall mid-block");

        for (const int lane : { 0, 4 })
        {
            AudioRig rig { kSampleRate, blockSize };
            rig.setStep (lane, step, 127);

            // Every trigger is delayed by the engine's lookahead so CACHAÇA's
            // bipolar jitter can place a hit before its step. The host is told,
            // so the recording lands on the grid — but the RENDER is late by
            // exactly that much, and this asserts the exact figure rather than
            // relaxing into a tolerance. CACHAÇA is 0 here, so there is no
            // jitter on top.
            const auto latency = rig.processor.getLatencySamples();
            checkEqual (latency, 1536, "the reported latency is 32 ms at 48 kHz");

            const auto expected = gridPosition + latency;

            auto buffer = rig.render (48000, blockSize);
            const auto onset = fbtest::firstNonZeroSample (buffer);

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
        const auto zeroLatency = atZero.processor.getLatencySamples();
        const auto zeroOnset = fbtest::firstNonZeroSample (buffer);
        check (zeroOnset >= zeroLatency && zeroOnset <= zeroLatency + 2,
               juce::String ("a step-0 hit starts at the lookahead (") + juce::String (zeroLatency)
                   + ", measured " + juce::String (zeroOnset) + ")");
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
        // Divisible by 32, 512 and 2048, and long enough to contain step 4 at
        // 24000 PLUS the 32 ms lookahead (1536) plus the note's own tail.
        constexpr int numSamples = 32768;

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

        auto noisySmall = noisyTiny.render (numSamples, 32);
        auto noisyLarge = noisyHuge.render (numSamples, 2048);
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

    // ── AC-1, AC-2: CACHAÇA's timing jitter ─────────────────────────────────

    /** Half-width of each hit's search window, in samples.

        Must EXCEED the maximum jitter (0.022 x 48000 = 1056) so a fully
        displaced hit is still inside its own window, and must stay UNDER the
        gap to the previous hit's tail so the detector cannot find that instead.
        BB at DECAY 0 rings 2688 samples, so with a 6000-sample step the
        previous tail can reach centre - 6000 + 1056 + 2688 = centre - 2256 —
        which a radius of 2400 caught, reporting the wrong hit on one step in
        twelve. 1200 clears the jitter by 144 samples and the tail by 1056. */
    constexpr int kHitSearchRadius = 1200;

    /** A rig whose HH lane fires on every step with the shortest possible
        voice, so consecutive hits never overlap and each one's onset can be
        measured on its own.

        HH at DECAY 0 lasts (0.03 + 0.04) x 0.4 = 28 ms = 1344 samples against a
        6000-sample step, and the jitter reaches +/-1056, so the search windows
        stay clear of each other and of the previous hit's tail. */


    struct JitterRig
    {
        AudioRig rig { kSampleRate, 512 };
        static constexpr int kLane = 6;      // bateria HH
        static constexpr int kSteps = 16;

        explicit JitterRig (float cachaca)
        {
            rig.setValue (forrobox::ids::cachaca, cachaca);
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                       forrobox::ids::decay), 0.0f);

            for (int step = 0; step < kSteps; ++step)
                rig.setStep (kLane, step, 127);
        }

        std::vector<int> displacements (int steps)
        {
            const auto samples = static_cast<int> ((steps + 2) * kStepSamples);
            auto buffer = rig.render (samples - samples % 512, 512);

            return fbtest::measureHitDisplacements (buffer,
                                                    static_cast<double> (rig.processor.getLatencySamples()),
                                                    kStepSamples, steps, kHitSearchRadius);
        }
    };

    void testJitterIsOneDrawPerStep()
    {
        section ("CACHAÇA: one jitter draw per step, shared by every lane");

        // The claim the step-shaped seam exists for. app.js draws the jitter
        // OUTSIDE scheduleStep and passes one time in, so every lane of a step
        // moves together. A per-lane draw is not a compile error and not a
        // failing spectral test — it is the whole step breathing against the
        // lanes flamming apart.
        //
        // BOTH LANES IN ONE RENDER, and this is the whole point.
        //
        // The first version of this test measured the two lanes in two SEPARATE
        // single-lane renders and compared the displacements. That cannot fail:
        // with one hit lane per render, a per-lane draw consumes the stream in
        // exactly the same order as a per-step draw, so the two renders still
        // agree. A review moved the draw into scheduleLane and every check here
        // stayed green — an assertion that could not detect the one thing it
        // was named for. (The negative control for it "passed" only because the
        // mutation failed to COMPILE, which was recorded as detection.)
        //
        // Summed into one buffer the two lanes cannot be separated by onset
        // detection, so the discriminator is the NUMBER of onsets per step: one
        // shared offset gives one, two independent offsets give two whenever
        // they differ by more than the voices' own length.
        //
        // 64 steps, because the detector's per-step sensitivity is limited: BB
        // rings 2688 samples and HH 1344, so two independent offsets only open
        // a visible gap when they differ by more than about 1344 — which for
        // two uniform +/-1056 draws happens on roughly 15% of steps. At 24
        // steps a per-lane draw showed 4 splits; at 64 it shows around 9, and
        // the chance of seeing ZERO falls to about 3 in 100 000.
        constexpr int steps = 64;

        AudioRig rig { kSampleRate, 512 };
        rig.setValue (forrobox::ids::cachaca, 100.0f);
        rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                   forrobox::ids::decay), 0.0f);

        // BB (a sine under 150 Hz, 2688 samples at DECAY 0) and HH (noise above
        // 9 kHz, 1344 samples). Both short enough that a gap opens between them
        // if their offsets differ.
        for (int step = 0; step < 16; ++step)
        {
            rig.setStep (4, step, 127);
            rig.setStep (6, step, 127);
        }

        const auto samples = static_cast<int> ((steps + 2) * kStepSamples);
        auto buffer = rig.render (samples - samples % 512, 512);

        const auto latency = rig.processor.getLatencySamples();

        auto measuredSteps = 0;
        auto split = 0;
        auto worstSplit = 0;

        for (int step = 0; step < steps; ++step)
        {
            const auto centre = latency + static_cast<int> (kStepSamples * static_cast<double> (step));
            const auto onsets = fbtest::countOnsets (buffer,
                                                     centre - kHitSearchRadius,
                                                     centre + kHitSearchRadius);

            if (onsets == 0)
                continue;

            ++measuredSteps;

            if (onsets > 1)
            {
                ++split;
                worstSplit = juce::jmax (worstSplit, onsets);
            }
        }

        check (measuredSteps >= steps - 2,
               juce::String ("both lanes sounded on ") + juce::String (measuredSteps)
                   + " of " + juce::String (steps) + " steps");
        checkEqual (split, 0,
                    juce::String ("every step has exactly ONE onset — the two lanes share a single "
                                  "jitter draw (") + juce::String (split)
                        + " steps split, worst " + juce::String (worstSplit) + ")");

        // And the steps really are being displaced, or the check above would
        // hold just as well for a jitter that does nothing.
        const auto displacements = fbtest::measureHitDisplacements (
            buffer, static_cast<double> (latency), kStepSamples, steps, kHitSearchRadius);

        auto moved = 0;

        for (const auto d : displacements)
            if (d != fbtest::notFound && std::abs (d) > 8)
                ++moved;

        check (moved >= steps / 2,
               juce::String ("and the steps are genuinely displaced (") + juce::String (moved)
                   + " of " + juce::String (steps) + " moved by more than 8 samples)");

        // The instrument can see a split when there is one: the same two lanes
        // on ADJACENT steps land 6000 samples apart, which a window centred on
        // either one resolves as a single onset each — so widening the window
        // to span both must report two. Without this, "split == 0" could mean
        // countOnsets never counts anything.
        const auto centre = latency;
        const auto spanningTwoSteps = fbtest::countOnsets (buffer, centre - kHitSearchRadius,
                                                           centre + static_cast<int> (kStepSamples)
                                                             + kHitSearchRadius);

        check (spanningTwoSteps >= 2,
               juce::String ("and countOnsets does resolve separate onsets when they exist (")
                   + juce::String (spanningTwoSteps) + " across two steps)");
    }

    void testMutingDoesNotRetimeOtherChannels()
    {
        section ("muting one channel does not re-time another");

        // Added after a review MEASURED the opposite: with CACHACA at 100 and
        // BB and ganza on all sixteen steps, muting the GANZA moved BB's hits
        // on 11 of 12 steps, by up to 1000 samples — 21 ms.
        //
        // The cause was that the velocity and ghost draws were CONDITIONAL: a
        // gated channel returned before its draw, so the number of draws per
        // step depended on mute, solo, GHOST and the pattern, and every later
        // step's jitter shifted. Splitting the noise generator off had fixed
        // the render side of exactly this coupling and left the scheduling side
        // untouched — while this file's own rationale claimed otherwise.
        //
        // A control restoring the conditional draw passed all 900 checks,
        // because nothing asserted the property that had just been fixed.
        constexpr int steps = 32;

        const auto displacementsWith = [] (bool muteZabumba)
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::cachaca, 100.0f);
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                       forrobox::ids::decay), 0.0f);

            // The ZABUMBA is the channel muted, not the ganzá: it has a
            // channel of its own and all its energy is below 200 Hz, so it can
            // be filtered out of the measurement. The ganzá's 6.8 kHz band
            // overlaps HH's, and its early ghosts were being reported as HH's
            // onsets — which made a first version of this test fail against
            // correct code.
            //
            // It gets a ghost probability too, so its ghost ROLLS also consume
            // the stream: the conditional-draw bug reached through ghosts as
            // well as through hits.
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                       forrobox::ids::ghost), 60.0f);

            if (muteZabumba)
                rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                           forrobox::ids::mute), 1.0f);

            for (int step = 0; step < 16; ++step)
            {
                rig.setStep (6, step, 127);                        // HH, the lane measured
                rig.setStep (0, step, step % 2 == 0 ? 110 : 0);    // zabumba: hits and gaps
            }

            const auto samples = static_cast<int> ((32 + 2) * kStepSamples);
            auto buffer = rig.render (samples - samples % 512, 512);

            // HH alone: everything below 4 kHz removed, so the zabumba cannot
            // be mistaken for it.
            auto isolated = fbtest::highpassed (buffer, 4000.0, kSampleRate);

            // A threshold relative to the filtered buffer's own peak, not
            // exact-zero detection: a highpass cannot make the zabumba
            // vanish, only attenuate it — at 4 kHz its 0.5 peak still leaves
            // about 5e-4, which "not exactly zero" happily reports as HH's
            // onset. 2% of the filtered peak is far above that residual and
            // far below HH's own level, and the same bias applies to both
            // runs, which is all this comparison needs.
            return fbtest::measureHitDisplacements (
                isolated, static_cast<double> (rig.processor.getLatencySamples()),
                kStepSamples, 32, kHitSearchRadius, 0.02f);
        };

        const auto unmuted = displacementsWith (false);
        const auto muted   = displacementsWith (true);

        auto compared = 0, differed = 0, worst = 0;

        for (size_t i = 0; i < unmuted.size(); ++i)
        {
            if (unmuted[i] == fbtest::notFound || muted[i] == fbtest::notFound)
                continue;

            ++compared;

            const auto delta = std::abs (unmuted[i] - muted[i]);

            if (delta > 2)
            {
                ++differed;
                worst = juce::jmax (worst, delta);
            }
        }

        check (compared >= steps - 4,
               juce::String ("HH was measurable on ") + juce::String (compared)
                   + " of " + juce::String (steps) + " steps in both runs");
        checkEqual (differed, 0,
                    juce::String ("HH lands identically whether the zabumba is muted or not (")
                        + juce::String (differed) + " steps differed, worst "
                        + juce::String (worst) + " samples)");

        // And the zabumba really was silenced, or this compares two identical
        // runs and proves nothing.
        AudioRig audible { kSampleRate, 512 };
        audible.setStep (0, 0, 110);
        const auto zabumbaBand = bandEnergy (audible.render (24576, 512), 60.0, 200.0);

        AudioRig silenced { kSampleRate, 512 };
        silenced.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                        forrobox::ids::mute), 1.0f);
        silenced.setStep (0, 0, 110);
        const auto mutedBand = bandEnergy (silenced.render (24576, 512), 60.0, 200.0);

        check (zabumbaBand > 0.0 && mutedBand < zabumbaBand * 1.0e-6,
               "and muting the zabumba does silence it");
    }

    void testMutingDoesNotRelevelOtherChannels()
    {
        section ("muting one channel does not re-level another");

        // The other half of the mute-independence property, and the half the
        // onset test cannot see.
        //
        // testMutingDoesNotRetimeOtherChannels measures HH's onset POSITIONS,
        // which come from the jitter key alone. A review pointed out that the
        // velocity multipliers and ghost rolls were still order-dependent —
        // SynthVoice::trigger drew five detune values for the triângulo only,
        // after the audibility gate, only for a claimed voice — so muting the
        // triângulo re-levelled every other channel while every timing
        // assertion stayed green.
        //
        // Keying every humanisation value on (seed, step, lane, purpose, index)
        // makes that structural. This asserts it: the rendered audio of one
        // channel must be BIT-IDENTICAL whether another is muted or not.
        const auto renderWith = [] (bool muteTriangulo)
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::cachaca, 100.0f);

            // The triângulo is the channel muted: it is the ONE lane whose
            // trigger drew extra values, so it is the case that was broken.
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[1].id,
                                                       forrobox::ids::ghost), 70.0f);

            if (muteTriangulo)
                rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[1].id,
                                                           forrobox::ids::mute), 1.0f);

            // Ganzá is measured: its own channel, its own ghost probability,
            // and hits on half the steps so both the hit and ghost paths run.
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[3].id,
                                                       forrobox::ids::ghost), 50.0f);

            for (int step = 0; step < 16; ++step)
            {
                rig.setStep (3, step, step % 2 == 0 ? 100 : 0);
                rig.setStep (1, step, 120);
            }

            return rig.render (98304, 512);
        };

        auto unmuted = renderWith (false);
        auto muted   = renderWith (true);

        // The triângulo sits at 5.4-11.2 kHz and the ganzá at 6.8 kHz, so they
        // overlap and the buffers are NOT comparable directly. What is
        // comparable is the ganzá's own contribution — so render it alone and
        // check that IT is unchanged by the other channel's mute state.
        //
        // Rendering the ganzá alone in both runs would be trivially equal, so
        // the test is: ganzá-alone must equal (ganzá + muted triângulo).
        AudioRig alone { kSampleRate, 512 };
        alone.setValue (forrobox::ids::cachaca, 100.0f);
        alone.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[3].id,
                                                     forrobox::ids::ghost), 50.0f);

        for (int step = 0; step < 16; ++step)
            alone.setStep (3, step, step % 2 == 0 ? 100 : 0);

        auto ganzaOnly = alone.render (98304, 512);

        auto worstMuted = 0.0f, worstUnmuted = 0.0f;

        for (int c = 0; c < 2; ++c)
            for (int s = 0; s < ganzaOnly.getNumSamples(); ++s)
            {
                const auto reference = ganzaOnly.getSample (c, s);
                worstMuted = juce::jmax (worstMuted,
                                         std::abs (muted.getSample (c, s) - reference));
                worstUnmuted = juce::jmax (worstUnmuted,
                                           std::abs (unmuted.getSample (c, s) - reference));
            }

        check (bufferPeak (ganzaOnly) > 0.001f, "the ganzá alone is not silent");

        // A muted triângulo contributes nothing, so the sum must equal the
        // ganzá exactly — every humanisation value the ganzá used has to be
        // untouched by the triângulo's presence in the pattern.
        checkEqual (worstMuted, 0.0f,
                    "the ganzá renders BIT-IDENTICALLY with a muted triângulo alongside it");

        // And the unmuted run is genuinely different, or the comparison above
        // is between two identical buffers and proves nothing.
        check (worstUnmuted > 0.001f,
               juce::String ("while an audible triângulo does change the mix (")
                   + juce::String (worstUnmuted, 4) + ")");
    }

    void testHumanisationKeying()
    {
        section ("humanisation keys: independent, per lane, and not per bar");

        // These test the KEY, not the audio. Four negative controls on the
        // keyed refactor went undetected because their mutations are no-ops on
        // the inputs the audio tests happen to use — a wrong key still yields
        // valid-looking random values. What can be asserted is the properties
        // the keying exists to provide.

        constexpr std::uint64_t seed = forrobox::kHumanisationSeed;

        // ── the same step index in different bars must differ ────────────────
        //
        // The key uses a MONOTONIC step counter, not the pattern index. Keyed
        // on the index it would wrap, so step 0 of bar 2 would humanise exactly
        // like step 0 of bar 1 and the groove would repeat its deviations every
        // bar — audible as a loop, the opposite of the intent.
        auto repeats = 0;

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            for (std::uint64_t step = 0; step < 16; ++step)
                if (juce::exactlyEqual (
                        forrobox::humanisedValue (seed, step, lane, forrobox::Purpose::velocity),
                        forrobox::humanisedValue (seed, step + 16, lane, forrobox::Purpose::velocity)))
                    ++repeats;

        checkEqual (repeats, 0,
                    "the same step index one bar later humanises differently");

        // ── lanes are independent ───────────────────────────────────────────
        auto laneCollisions = 0;

        for (std::uint64_t step = 0; step < 32; ++step)
            for (int a = 0; a < forrobox::State::kNumLanes; ++a)
                for (int b = a + 1; b < forrobox::State::kNumLanes; ++b)
                    if (juce::exactlyEqual (
                            forrobox::humanisedValue (seed, step, a, forrobox::Purpose::velocity),
                            forrobox::humanisedValue (seed, step, b, forrobox::Purpose::velocity)))
                        ++laneCollisions;

        checkEqual (laneCollisions, 0, "two lanes never share a value at the same step");

        // ── purposes are independent ────────────────────────────────────────
        //
        // A ghost's ROLL and its VELOCITY must not correlate: sharing a purpose
        // would mean a ghost that barely passed its chance always came out
        // quiet, so at a low GHOST setting every ghost would be soft. Measured
        // as a correlation coefficient over many keys.
        auto sumRoll = 0.0, sumVel = 0.0, sumRollVel = 0.0, sumRoll2 = 0.0, sumVel2 = 0.0;
        constexpr int samples = 4096;

        for (int i = 0; i < samples; ++i)
        {
            const auto step = static_cast<std::uint64_t> (i);
            const auto roll = static_cast<double> (
                forrobox::humanisedValue (seed, step, i % 8, forrobox::Purpose::ghostRoll));
            const auto vel = static_cast<double> (
                forrobox::humanisedValue (seed, step, i % 8, forrobox::Purpose::ghostVelocity));

            sumRoll += roll;   sumVel += vel;   sumRollVel += roll * vel;
            sumRoll2 += roll * roll;   sumVel2 += vel * vel;
        }

        const auto n = static_cast<double> (samples);
        const auto covariance = sumRollVel / n - (sumRoll / n) * (sumVel / n);
        const auto sdRoll = std::sqrt (sumRoll2 / n - (sumRoll / n) * (sumRoll / n));
        const auto sdVel  = std::sqrt (sumVel2  / n - (sumVel  / n) * (sumVel  / n));
        const auto correlation = covariance / juce::jmax (1.0e-12, sdRoll * sdVel);

        // Independent draws correlate at ~1/sqrt(n) = 0.016; sharing a purpose
        // correlates at exactly 1.0.
        check (std::abs (correlation) < 0.1,
               juce::String ("a ghost's roll and its velocity are independent (correlation ")
                   + juce::String (correlation, 4) + ")");

        // ── the values are uniform ──────────────────────────────────────────
        std::array<int, 8> buckets {};

        for (int i = 0; i < samples; ++i)
            ++buckets[static_cast<size_t> (juce::jlimit (0, 7, static_cast<int> (
                forrobox::humanisedValue (seed, static_cast<std::uint64_t> (i), i % 8,
                                          forrobox::Purpose::jitter) * 8.0f)))];

        auto thinnest = samples, fattest = 0;

        for (const auto count : buckets)
        {
            thinnest = juce::jmin (thinnest, count);
            fattest  = juce::jmax (fattest, count);
        }

        // Expected 512 per bucket, sigma = sqrt(4096 x 0.125 x 0.875) = 21.2,
        // so 4 sigma is 85. Computed, not guessed.
        check (thinnest > 512 - 85 && fattest < 512 + 85,
               juce::String ("the keyed values are uniform (buckets ") + juce::String (thinnest)
                   + " to " + juce::String (fattest) + ", expected 512 +/- 85)");

        // ── only one lane uses the detune purpose ───────────────────────────
        //
        // A tripwire, not a behaviour test. The detune is keyed on its own lane,
        // and a control keying it on lane 0 instead is currently a no-op —
        // there is only one detunedSquares lane, so nothing can collide. This
        // fails the moment a second one exists, which is when that stops being
        // true.
        auto detuningLanes = 0;

        for (const auto& spec : forrobox::voiceSpecs)
            for (const auto& layer : spec.layers)
                if (layer.generator == forrobox::Generator::detunedSquares)
                    ++detuningLanes;

        checkEqual (detuningLanes, 1,
                    "exactly one lane uses per-partial detune — if this fails, its key must be "
                    "checked for lane independence");
    }

    void testLookaheadCoversEveryRate()
    {
        section ("the lookahead covers the excursion at every sample rate");

        // The figure is built from the SAME expressions as the excursions, not
        // by rounding their sum. Rounding the sum is exact at every standard
        // rate — which is why a control that did so went undetected — but at
        // 24 545 of the 192 001 integer rates in [8 kHz, 200 kHz] the reachable
        // early excursion comes out one sample LARGER than the reported
        // lookahead. Three comments claim the offset is non-negative by
        // construction; this is what makes that true rather than true-at-the-
        // rates-we-tried.
        auto shortfalls = 0;
        auto worstRate = 0;

        for (int rate = 8000; rate <= 200000; rate += 7)
        {
            const auto reported = forrobox::lookaheadSamplesFor (static_cast<double> (rate));

            // The worst early excursion the two draws can produce, computed the
            // way the scheduling path computes them.
            const auto excursion = static_cast<int> (std::lround (forrobox::kMaxJitterSeconds
                                                                    * rate))
                                 + static_cast<int> (std::lround (forrobox::kGhostJitterSeconds
                                                                    * rate));

            if (reported < excursion)
            {
                ++shortfalls;
                worstRate = rate;
            }
        }

        checkEqual (shortfalls, 0,
                    juce::String ("no rate reports less lookahead than its excursion (worst ")
                        + juce::String (worstRate) + " Hz)");

        // And 8069 Hz specifically, where rounding the sum gives 258 against a
        // reachable 259.
        checkEqual (forrobox::lookaheadSamplesFor (8069.0), 259,
                    "8069 Hz, where rounding the sum would give 258");
    }

    void testPercentNormalisation()
    {
        section ("parameter normalisation is bounded and NaN-safe");

        // Unit tests of the helpers, not of a path. A control removing the
        // isfinite guard went undetected because NaN cannot reach CACHAÇA
        // through the APVTS — AudioParameterFloat clamps — so the guard is
        // defensive. Its CONTRACT is still worth asserting: jlimit passes NaN
        // through unchanged, which is documented in Clock.cpp for swing and was
        // the reason a `>= 0.0f` test that looked dead was load-bearing.
        checkEqual (forrobox::ids::normalisedPercent (0.0f), 0.0f, "0 maps to 0");
        checkEqual (forrobox::ids::normalisedPercent (100.0f), 1.0f, "100 maps to 1");
        checkEqual (forrobox::ids::normalisedPercent (50.0f), 0.5f, "50 maps to 0.5");
        checkEqual (forrobox::ids::normalisedPercent (-10.0f), 0.0f, "below range clamps");
        checkEqual (forrobox::ids::normalisedPercent (250.0f), 1.0f, "above range clamps");
        checkEqual (forrobox::ids::normalisedPercent (std::numeric_limits<float>::quiet_NaN()), 0.0f,
                    "NaN becomes 0 rather than propagating");
        checkEqual (forrobox::ids::normalisedPercent (std::numeric_limits<float>::infinity()), 0.0f,
                    "and so does infinity");

        checkEqual (forrobox::ids::normalisedPan (0.0f), 0.0f, "centre pan maps to 0");
        checkEqual (forrobox::ids::normalisedPan (50.0f), 1.0f, "hard right maps to +1");
        checkEqual (forrobox::ids::normalisedPan (-50.0f), -1.0f, "hard left maps to -1");
        checkEqual (forrobox::ids::normalisedPan (std::numeric_limits<float>::quiet_NaN()), 0.0f,
                    "a NaN pan centres rather than propagating");
    }

    void testJitterDistribution()
    {
        section ("CACHAÇA: the jitter is uniform, bipolar and scaled by the knob");

        // 128 trials. A uniform bipolar draw over +/-1056 has
        // sigma = 1056/sqrt(3) = 610 per sample, so the standard error of the
        // MEAN is 610/sqrt(n): 152 samples at n = 16, 76 at 64, 54 at 128.
        //
        // It started at 16, where a 2-sigma bound of 320 samples is tripped by
        // a 2.5-sigma realisation about once in eighty runs — and was, at
        // 376.5, the first time it ran against a changed key. Under-powered,
        // not wrong. 128 is also what the uniformity check below needs to
        // separate a flat distribution from a triangular one.
        constexpr int steps = 128;
        const auto bound = static_cast<int> (forrobox::kMaxJitterSeconds * kSampleRate);   // 1056

        JitterRig full { 100.0f };
        const auto displacements = full.displacements (steps);

        auto measured = 0;
        auto negative = 0, positive = 0;
        auto sum = 0.0;
        auto worst = 0;

        for (const auto d : displacements)
        {
            if (d == fbtest::notFound)
                continue;

            ++measured;
            sum += static_cast<double> (d);
            worst = juce::jmax (worst, std::abs (d));

            if (d < -8) ++negative;
            if (d >  8) ++positive;
        }

        check (measured >= steps - 2,
               juce::String ("measured ") + juce::String (measured) + " of "
                   + juce::String (steps) + " steps");

        // BIPOLAR. A one-sided implementation — which is what a clamp at zero
        // produces, and what the rejected "late-only jitter" option would have
        // shipped — passes a bound-only test and fails this one.
        check (negative > 0 && positive > 0,
               juce::String ("displacements occur on BOTH sides of the grid (")
                   + juce::String (negative) + " early, " + juce::String (positive) + " late)");

        const auto mean = sum / juce::jmax (1, measured);

        // Three sigma, computed from the trial count actually achieved rather
        // than written down: sigma_mean = bound / sqrt(3) / sqrt(n).
        const auto standardError = static_cast<double> (bound)
                                     / std::sqrt (3.0)
                                     / std::sqrt (static_cast<double> (juce::jmax (1, measured)));
        const auto meanBound = 3.0 * standardError;

        check (std::abs (mean) < meanBound,
               juce::String ("the mean displacement is near zero (") + juce::String (mean, 1)
                   + " samples, 3-sigma bound " + juce::String (meanBound, 1) + " at "
                   + juce::String (measured) + " trials)");

        // UNIFORM, not merely bounded and centred. A triangular distribution —
        // what summing two draws would give — has the same mean and the same
        // bound.
        //
        // Per-bucket floors and ceilings do NOT separate the two, and a control
        // proved it: triangular puts about 8/24/24/8 across four buckets
        // against uniform's 16 each, and a 3-sigma floor of 5.6 with a ceiling
        // of 26.4 admits both. What separates them is the OUTER mass against
        // the INNER: 1.0 for uniform, 0.33 for triangular.
        //
        // At 128 trials each half has sigma = sqrt(128 x 0.25) = 5.7, so the
        // ratio's 3-sigma spread is about +/-0.38 around 1.0 — comfortably
        // clear of 0.33. The bound is 0.6.
        std::array<int, 4> buckets {};

        for (const auto d : displacements)
        {
            if (d == fbtest::notFound)
                continue;

            const auto normalised = (static_cast<double> (d) + bound) / (2.0 * bound);
            const auto index = juce::jlimit (0, 3, static_cast<int> (normalised * 4.0));
            ++buckets[static_cast<size_t> (index)];
        }

        auto emptiest = measured;

        for (const auto count : buckets)
            emptiest = juce::jmin (emptiest, count);

        check (emptiest > 0,
               juce::String ("every quarter of the range is used (thinnest bucket ")
                   + juce::String (emptiest) + " of " + juce::String (measured) + ")");

        const auto outer = buckets[0] + buckets[3];
        const auto inner = buckets[1] + buckets[2];
        const auto shape = static_cast<double> (outer) / juce::jmax (1, inner);

        check (shape > 0.6,
               juce::String ("the distribution is flat, not peaked: outer mass over inner is ")
                   + juce::String (shape, 3) + " (uniform 1.0, triangular 0.33, bound 0.6)");

        // The BOUND, and nothing clamped: a displacement of exactly 0 on every
        // step would mean the lookahead swallowed the jitter.
        check (worst <= bound + 4,
               juce::String ("no displacement exceeds +/-22 ms (worst ") + juce::String (worst)
                   + " against " + juce::String (bound) + ")");
        check (worst > bound / 3,
               juce::String ("and the range is genuinely used (worst ") + juce::String (worst) + ")");

        // Scaled by the knob. At CACHAÇA 50 the spread must be about half.
        JitterRig half { 50.0f };
        auto halfWorst = 0;

        for (const auto d : half.displacements (steps))
            if (d != fbtest::notFound)
                halfWorst = juce::jmax (halfWorst, std::abs (d));

        check (halfWorst <= bound / 2 + 4,
               juce::String ("CACHAÇA 50 halves the bound (worst ") + juce::String (halfWorst)
                   + " against " + juce::String (bound / 2) + ")");

        // CACHAÇA 0: exactly on the grid. Asserted exactly, not within a
        // tolerance — this is what proves the jitter is gated by the knob and
        // that the lookahead is a constant delay rather than a fudge.
        JitterRig off { 0.0f };
        auto offNonZero = 0;

        for (const auto d : off.displacements (steps))
            if (d != fbtest::notFound && std::abs (d) > 2)
                ++offNonZero;

        checkEqual (offNonZero, 0, "at CACHAÇA 0 every step lands on its grid position");
    }

    void testLatencyIsReported()
    {
        section ("the lookahead is reported to the host");

        // 32 ms, not 22: a ghost's +/-10 ms is applied on top of the step's
        // +/-22 ms, so 22 ms of headroom would still clamp a ghost — silently,
        // and at a rate rising with CACHAÇA.
        struct Rate { double rate; int expected; };

        for (const auto& r : std::array<Rate, 3> {{ { 44100.0, 1411 }, { 48000.0, 1536 }, { 96000.0, 3072 } }})
        {
            AudioRig rig { r.rate, 512 };

            checkEqual (rig.processor.getLatencySamples(), r.expected,
                        juce::String ("latency at ") + juce::String (r.rate, 0) + " Hz");

            const auto seconds = static_cast<double> (rig.processor.getLatencySamples()) / r.rate;

            check (std::abs (seconds - 0.032) < 0.0005,
                   juce::String ("which is 32 ms (") + juce::String (seconds * 1000.0, 2) + " ms)");
        }

        // Reported even at CACHAÇA 0. What must not vary is the KNOB: a
        // knob-scaled latency would force a host re-negotiation mid-session.
        AudioRig quiet;
        quiet.setValue (forrobox::ids::cachaca, 0.0f);
        checkEqual (quiet.processor.getLatencySamples(), 1536,
                    "and it does not change with CACHAÇA");

        // BEFORE the first prepareToPlay, which is when a host that queries at
        // scan or instantiation time reads it. A 0 here leaves the groove 32 ms
        // late in exactly the hosts that cache the value — and AudioRig always
        // prepares, so no other test in this suite can see it.
        ForroBoxAudioProcessor fresh;

        check (fresh.getLatencySamples() > 0,
               juce::String ("latency is sane before the first prepareToPlay (")
                   + juce::String (fresh.getLatencySamples()) + ")");
        checkEqual (fresh.getLatencySamples(), 1536,
                    "seeded from a nominal 48 kHz");
    }

    void testVelocityHumanisation()
    {
        section ("CACHAÇA: velocity variation is per hit, and only ever softer");

        // Per HIT, not per step — the opposite of the timing jitter, and the
        // sketch's asymmetry: `v *= (1 - cach * 0.25 * random())` sits inside
        // the per-hit play().
        constexpr int steps = 16;

        // BB, a swept sine, NOT HH. The peak of a filtered NOISE burst is itself
        // a random variable, so measuring it conflates the envelope with the
        // noise realisation — it read a 0.567 spread against a multiplier that
        // can only reach 0.75. A tone's peak is exactly its envelope peak.
        constexpr int toneLane = 4;

        AudioRig rig { kSampleRate, 512 };
        rig.setValue (forrobox::ids::cachaca, 100.0f);
        rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                   forrobox::ids::decay), 0.0f);

        for (int step = 0; step < steps; ++step)
            rig.setStep (toneLane, step, 127);

        auto buffer = rig.render (static_cast<int> ((steps + 2) * kStepSamples) / 512 * 512, 512);

        // Peak per step, measured in its own window.
        std::vector<float> peaks;

        for (int step = 0; step < steps; ++step)
        {
            const auto centre = rig.processor.getLatencySamples()
                              + static_cast<int> (kStepSamples * static_cast<double> (step));
            const auto from = juce::jmax (0, centre - kHitSearchRadius);
            const auto to   = juce::jmin (buffer.getNumSamples(), centre + kHitSearchRadius);

            if (to > from)
                peaks.push_back (buffer.getMagnitude (from, to - from));
        }

        check (peaks.size() >= static_cast<size_t> (steps - 2),
               "every step produced a measurable peak");

        const auto loudest = *std::max_element (peaks.begin(), peaks.end());
        const auto quietest = *std::min_element (peaks.begin(), peaks.end());

        // NEVER louder. The multiplier is 1 - 0.25 x [0,1), so (0.75, 1.0].
        // A sign error, or nextFloat() used as a bipolar draw, breaks this.
        check (quietest > loudest * 0.7f,
               juce::String ("no hit is softened past 75% (quietest is ")
                   + juce::String (quietest / loudest, 3) + " of the loudest)");

        // And it genuinely varies: a no-op multiplier would make every peak
        // identical, which the bound above would happily accept.
        check (quietest < loudest * 0.97f,
               juce::String ("and the hits really do vary (") + juce::String (quietest / loudest, 3)
                   + " spread)");

        // Two lanes on the SAME step get DIFFERENT multipliers, which is what
        // "per hit" means. Rendered together so they share one step and one
        // jitter draw.
        AudioRig pair { kSampleRate, 512 };
        pair.setValue (forrobox::ids::cachaca, 100.0f);
        pair.setStep (4, 0, 127);   // BB, a sine below 150 Hz
        pair.setStep (6, 0, 127);   // HH, noise above 9 kHz

        auto together = pair.render (24576, 512);

        // Compared against each lane alone at the same seed position would be
        // fragile; instead compare the two lanes' levels against their ratio
        // with CACHAÇA off, which removes the multiplier entirely.
        AudioRig pairOff { kSampleRate, 512 };
        pairOff.setStep (4, 0, 127);
        pairOff.setStep (6, 0, 127);

        auto togetherOff = pairOff.render (24576, 512);

        const auto lowWith  = bandEnergy (together, 45.0, 150.0);
        const auto highWith = bandEnergy (together, 10000.0, 18000.0);
        const auto lowOff   = bandEnergy (togetherOff, 45.0, 150.0);
        const auto highOff  = bandEnergy (togetherOff, 10000.0, 18000.0);

        const auto lowRatio  = lowWith / juce::jmax (1.0e-12, lowOff);
        const auto highRatio = highWith / juce::jmax (1.0e-12, highOff);

        check (std::abs (lowRatio - highRatio) > 0.01,
               juce::String ("two lanes on one step are scaled differently (")
                   + juce::String (lowRatio, 4) + " against " + juce::String (highRatio, 4)
                   + ") — the draw is per hit, not per step");

        // CACHAÇA 0: no variation at all, asserted exactly.
        AudioRig none { kSampleRate, 512 };
        none.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                    forrobox::ids::decay), 0.0f);

        for (int step = 0; step < steps; ++step)
            none.setStep (toneLane, step, 127);

        auto flat = none.render (static_cast<int> ((steps + 2) * kStepSamples) / 512 * 512, 512);

        auto flatLoudest = 0.0f, flatQuietest = 1.0f;

        for (int step = 0; step < steps; ++step)
        {
            const auto centre = none.processor.getLatencySamples()
                              + static_cast<int> (kStepSamples * static_cast<double> (step));
            const auto from = juce::jmax (0, centre - kHitSearchRadius);
            const auto to   = juce::jmin (flat.getNumSamples(), centre + kHitSearchRadius);

            if (to > from)
            {
                const auto peak = flat.getMagnitude (from, to - from);
                flatLoudest = juce::jmax (flatLoudest, peak);
                flatQuietest = juce::jmin (flatQuietest, peak);
            }
        }

        checkEqual (flatQuietest, flatLoudest,
                    "at CACHAÇA 0 every hit is exactly the same level");
    }

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
    // ── AC-4, AC-5: ghost notes ─────────────────────────────────────────────

    /** Counts ghosts by rendering a pattern that is silent on `lane` and
        looking for a hit in each step's window.

        `stepsRendered` steps at 6000 samples each; the lane's voice must be
        shorter than a step so a ghost cannot be mistaken for its neighbour's
        tail. Returns how many steps produced one. */
    int countGhosts (int lane, float ghostPercent, float cachaca, int stepsRendered,
                     bool muteChannel = false, int soloChannel = -1)
    {
        AudioRig rig { kSampleRate, 512 };
        rig.setValue (forrobox::ids::cachaca, cachaca);

        const auto channel = forrobox::VoiceEngine::channelForLane (lane);
        const auto* channelId = forrobox::ids::channelInfos[static_cast<size_t> (channel)].id;

        rig.setValue (forrobox::ids::channelParam (channelId, forrobox::ids::ghost), ghostPercent);
        rig.setValue (forrobox::ids::channelParam (channelId, forrobox::ids::decay), 0.0f);

        if (muteChannel)
            rig.setValue (forrobox::ids::channelParam (channelId, forrobox::ids::mute), 1.0f);

        if (soloChannel >= 0)
            rig.setValue (forrobox::ids::channelParam (
                              forrobox::ids::channelInfos[static_cast<size_t> (soloChannel)].id,
                              forrobox::ids::solo), 1.0f);

        // The pattern stays empty: every step is a ghost opportunity.
        const auto samples = static_cast<int> ((stepsRendered + 2) * kStepSamples);
        auto buffer = rig.render (samples - samples % 512, 512);

        // A ghost's own +/-10 ms (480 samples) sits on top of the step's
        // jitter, so the window must cover both.
        const auto radius = kHitSearchRadius + 520;
        const auto displacements = fbtest::measureHitDisplacements (
            buffer, static_cast<double> (rig.processor.getLatencySamples()),
            kStepSamples, stepsRendered, radius);

        auto fired = 0;

        for (const auto d : displacements)
            if (d != fbtest::notFound)
                ++fired;

        return fired;
    }

    void testGhostRate()
    {
        section ("ghost notes fire at the specified rate");

        // chance = (ghost/100) x (0.22 + (cachaca/100) x 0.6)
        //
        // Tolerance from the BINOMIAL standard error, computed rather than
        // tuned: for n trials at probability p, sigma = sqrt(n p (1-p)), and
        // three sigma is used. At n = 160 and p = 0.22 that is 3 x 5.2 = 16
        // ghosts, or 10 percentage points — wide, but honest for this trial
        // count, and still far tighter than the gap between the specified
        // formula and the plausible wrong ones this catches.
        constexpr int steps = 160;

        struct Case { float ghost, cachaca; };

        for (const auto& c : std::array<Case, 4> {{ { 100.0f, 0.0f },
                                                    { 100.0f, 100.0f },
                                                    { 50.0f,  100.0f },
                                                    { 50.0f,  0.0f } }})
        {
            const auto expected = (c.ghost / 100.0f)
                                    * (forrobox::kGhostBaseChance
                                         + (c.cachaca / 100.0f) * forrobox::kGhostCachacaSpan);

            const auto fired = countGhosts (6, c.ghost, c.cachaca, steps);
            const auto observed = static_cast<float> (fired) / static_cast<float> (steps);

            const auto sigma = std::sqrt (static_cast<double> (steps) * expected * (1.0 - expected));
            const auto tolerance = 3.0 * sigma / static_cast<double> (steps);

            const auto label = juce::String ("ghost ") + juce::String (c.ghost, 0)
                             + " / cachaça " + juce::String (c.cachaca, 0);

            check (std::abs (observed - expected) < tolerance,
                   label + ": rate " + juce::String (observed, 3) + " matches "
                         + juce::String (expected, 3) + " within 3 sigma ("
                         + juce::String (tolerance, 3) + ")");
        }

        // The two ends that separate the real formula from a plausible wrong
        // one. CACHAÇA 0 must still fire — at (ghost/100) x 0.22 — because
        // CACHAÇA RAISES the rate rather than gating it. An implementation that
        // multiplied by cachaca instead of adding a base term would produce
        // zero here and pass every other case above.
        check (countGhosts (6, 100.0f, 0.0f, 80) > 6,
               "at CACHAÇA 0 ghosts still fire — the 0.22 base term is not gated by the knob");

        // And ghost 0 fires nothing at any CACHAÇA.
        checkEqual (countGhosts (6, 0.0f, 100.0f, 80), 0,
                    "at ghost 0 nothing fires, even at CACHAÇA 100");

        // Rate rises with CACHAÇA at a fixed ghost: 0.22 -> 0.82, so nearly 4x.
        const auto low  = countGhosts (6, 100.0f, 0.0f, 160);
        const auto high = countGhosts (6, 100.0f, 100.0f, 160);

        check (high > low * 2,
               juce::String ("CACHAÇA raises the ghost rate (") + juce::String (low) + " -> "
                   + juce::String (high) + " of 160)");
    }

    void testGhostProperties()
    {
        section ("ghost notes: velocity and placement");

        // The TRIÂNGULO, which has a channel of its own.
        //
        // Not a bateria lane: BB, CX, HH and TOM all share the bateria
        // channel's single `ghost` parameter, so raising it makes HH ghost too
        // — and HH's amplitude is 0.45v against BB's 1.0v, which is exactly why
        // a first attempt measuring "BB ghosts" read a 0.13 velocity fraction
        // for a value that cannot go below 0.20. It was measuring HH.
        //
        // The reference is a programmed hit at velocity 32 (v = 0.252), inside
        // the ghost range, so both are the CLOSED articulation and share a
        // duration. Comparing against velocity 127 would compare a 0.45 s open
        // note with a 0.06 s closed one.
        constexpr int lane = 1;
        const auto* triangulo = forrobox::ids::channelInfos[1].id;
        constexpr float referenceVelocity = 32.0f;
        const auto referenceV = referenceVelocity / static_cast<float> (forrobox::State::kMaxVelocity);

        AudioRig reference { kSampleRate, 512 };
        reference.setStep (lane, 0, static_cast<std::uint8_t> (referenceVelocity));
        const auto referencePeak = bufferPeak (reference.render (24576, 512));

        check (referencePeak > 0.0f, "the reference hit sounds");

        AudioRig ghosts { kSampleRate, 512 };
        ghosts.setValue (forrobox::ids::cachaca, 0.0f);
        ghosts.setValue (forrobox::ids::channelParam (triangulo, forrobox::ids::ghost), 100.0f);

        constexpr int steps = 96;
        const auto samples = static_cast<int> ((steps + 2) * kStepSamples);
        auto buffer = ghosts.render (samples - samples % 512, 512);

        auto measured = 0;
        auto tooLoud = 0, tooQuiet = 0;
        auto worstDisplacement = 0;
        auto lowestFraction = 10.0f, highestFraction = 0.0f;

        // The expected peak fraction, relative to the reference velocity:
        // 0.20/0.252 = 0.794 up to 0.32/0.252 = 1.270. The 12% slack absorbs
        // the envelope's peak-dependent decay rate — a quieter note decays
        // more slowly, since both reach the same absolute floor — which shifts
        // the measured peak by a few percent either way.
        const auto lowBound  = forrobox::kGhostVelocityMin / referenceV * 0.88f;
        const auto highBound = (forrobox::kGhostVelocityMin + forrobox::kGhostVelocitySpan)
                                 / referenceV * 1.12f;

        for (int step = 0; step < steps; ++step)
        {
            const auto centre = ghosts.processor.getLatencySamples()
                              + static_cast<int> (kStepSamples * static_cast<double> (step));
            const auto from = juce::jmax (0, centre - 700);
            const auto to   = juce::jmin (buffer.getNumSamples(), centre + 700);

            if (to <= from)
                continue;

            const auto peak = buffer.getMagnitude (from, to - from);

            if (peak <= 0.0f)
                continue;

            ++measured;

            const auto fraction = peak / referencePeak;
            lowestFraction  = juce::jmin (lowestFraction, fraction);
            highestFraction = juce::jmax (highestFraction, fraction);

            if (fraction > highBound) ++tooLoud;
            if (fraction < lowBound)  ++tooQuiet;

            // Placement: +/-10 ms, with CACHAÇA at 0 so there is no step jitter
            // on top of it.
            for (int s = from; s < to; ++s)
            {
                if (! juce::exactlyEqual (buffer.getSample (0, s), 0.0f)
                    || ! juce::exactlyEqual (buffer.getSample (1, s), 0.0f))
                {
                    worstDisplacement = juce::jmax (worstDisplacement, std::abs (s - centre));
                    break;
                }
            }
        }

        check (measured > 10, juce::String ("ghosts fired on ") + juce::String (measured)
                                  + " of " + juce::String (steps) + " steps");
        checkEqual (tooLoud, 0,
                    juce::String ("no ghost exceeds velocity 0.32 (highest fraction ")
                        + juce::String (highestFraction, 3) + ", bound "
                        + juce::String (highBound, 3) + ")");
        checkEqual (tooQuiet, 0,
                    juce::String ("and none falls below 0.20 (lowest ")
                        + juce::String (lowestFraction, 3) + ", bound "
                        + juce::String (lowBound, 3) + ")");

        // The spread is real, not one repeated value.
        check (highestFraction > lowestFraction * 1.2f,
               juce::String ("ghost velocities genuinely vary (") + juce::String (lowestFraction, 3)
                   + " to " + juce::String (highestFraction, 3) + ")");

        const auto ghostBound = static_cast<int> (forrobox::kGhostJitterSeconds * kSampleRate);

        check (worstDisplacement <= ghostBound + 4,
               juce::String ("ghost placement stays inside +/-10 ms (worst ")
                   + juce::String (worstDisplacement) + " against " + juce::String (ghostBound) + ")");
        check (worstDisplacement > ghostBound / 3,
               juce::String ("and the range is used (worst ") + juce::String (worstDisplacement) + ")");
    }

    void testGhostLoudnessIsIndependentOfProbability()
    {
        section ("ghost loudness does not depend on the ghost probability");

        // Added after a control went undetected at the KEY level.
        //
        // testHumanisationKeying proves Purpose::ghostRoll and
        // Purpose::ghostVelocity are independent values, but a mutation at the
        // USE SITE — computing the velocity from the ROLL's purpose — is
        // invisible to that: the hash is unchanged, only which value is read.
        //
        // The audible consequence is specific. A ghost fires when its roll is
        // below the chance, so if the velocity is that same roll, only LOW
        // rolls ever reach the voice: at GHOST 30 with CACHAÇA 100 the chance
        // is 0.246, so velocities span [0.200, 0.230), while at GHOST 100 the
        // chance is 0.82 and they span [0.200, 0.298). Quiet ghosts at low
        // probability, louder ones at high — a coupling nothing should have.
        const auto* triangulo = forrobox::ids::channelInfos[1].id;
        constexpr int steps = 96;

        const auto meanGhostPeak = [&] (float ghostPercent)
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::cachaca, 100.0f);
            rig.setValue (forrobox::ids::channelParam (triangulo, forrobox::ids::ghost),
                          ghostPercent);

            const auto samples = static_cast<int> ((steps + 2) * kStepSamples);
            auto buffer = rig.render (samples - samples % 512, 512);

            auto sum = 0.0;
            auto counted = 0;

            for (int step = 0; step < steps; ++step)
            {
                const auto centre = rig.processor.getLatencySamples()
                                  + static_cast<int> (kStepSamples * static_cast<double> (step));
                const auto from = juce::jmax (0, centre - 700);
                const auto to   = juce::jmin (buffer.getNumSamples(), centre + 700);

                if (to <= from)
                    continue;

                const auto peak = buffer.getMagnitude (from, to - from);

                if (peak > 0.0f)
                {
                    sum += static_cast<double> (peak);
                    ++counted;
                }
            }

            return counted > 0 ? sum / counted : 0.0;
        };

        const auto atLow  = meanGhostPeak (30.0f);
        const auto atHigh = meanGhostPeak (100.0f);

        check (atLow > 0.0 && atHigh > 0.0, "ghosts fire at both probabilities");

        const auto ratio = atLow / juce::jmax (1.0e-9, atHigh);

        // Independent, the two means are both the midpoint of [0.20, 0.32] and
        // agree within sampling error. Coupled, the low-probability mean sits
        // near 0.215 against 0.249 — a ratio around 0.86. The bound is 0.94,
        // between the two.
        check (ratio > 0.94,
               juce::String ("mean ghost level is the same at GHOST 30 and 100 (ratio ")
                   + juce::String (ratio, 4) + ", bound 0.94)");
    }

    void testGhostsOnlyWhereAllowed()
    {
        section ("ghost notes: where they may not appear");

        // Only HH among the kit lanes. PLANNING.md: "For bateria, ghosts are
        // generated on the hi-hat (HH) only."
        //
        // Counted by BAND rather than by onset, because the four kit lanes
        // share one ghost parameter and an onset cannot say which lane fired.
        // HH is noise highpassed at 9 kHz; BB (130->48 Hz), TOM (190->110) and
        // CX's 190 Hz triangle body all sit below 300 Hz. So if anything but HH
        // ghosts, energy appears down there.
        AudioRig kit { kSampleRate, 512 };
        kit.setValue (forrobox::ids::cachaca, 100.0f);
        kit.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                   forrobox::ids::ghost), 100.0f);

        auto buffer = kit.render (static_cast<int> (66 * kStepSamples) / 512 * 512, 512);

        const auto lowBand  = bandEnergy (buffer, 40.0, 300.0);
        const auto highBand = bandEnergy (buffer, 10000.0, 18000.0);

        check (highBand > 0.0, "the kit is ghosting at all");
        check (lowBand < highBand * 0.01,
               juce::String ("only HH ghosts among the kit lanes — energy below 300 Hz stays at the "
                             "floor (") + juce::String (lowBand / juce::jmax (1.0e-12, highBand), 6)
                   + " of the 10-18 kHz band)");

        // The mirror: a programmed BB hit DOES put energy below 300 Hz, so the
        // measurement above can see a kit lane when there is one to see.
        AudioRig control { kSampleRate, 512 };
        control.setStep (4, 0, 127);
        const auto controlLow = bandEnergy (control.render (24576, 512), 40.0, 300.0);

        check (controlLow > lowBand * 100.0,
               "and a programmed BB hit is plainly visible in that band");

        // Each of the four lanes with a channel of its own ghosts — measured by
        // ENERGY against the same render with ghost at 0, not by counting
        // onsets.
        //
        // countGhosts searches +/-1720 samples around each step, and at DECAY 0
        // the zabumba still rings about 5300 samples and the pandeiro 3840. So
        // a ghost placed on the previous step still has energy inside this
        // step's window, and measureHitDisplacements reports first-non-zero —
        // it counts a tail as an onset. Those `> 5` checks would have passed at
        // a near-zero ghost rate. TestHarness.h states the non-overlap
        // requirement these two lanes violate.
        for (const int lane : { 0, 1, 2, 3 })
        {
            const auto energyWith = [lane] (float ghostPercent)
            {
                AudioRig rig { kSampleRate, 512 };
                rig.setValue (forrobox::ids::cachaca, 100.0f);
                rig.setValue (forrobox::ids::channelParam (
                                  forrobox::ids::channelInfos[static_cast<size_t> (lane)].id,
                                  forrobox::ids::ghost), ghostPercent);

                auto rendered = rig.render (static_cast<int> (34 * kStepSamples) / 512 * 512, 512);

                return static_cast<double> (bufferRms (rendered));
            };

            const auto silent = energyWith (0.0f);
            const auto ghosting = energyWith (100.0f);

            checkEqual (silent, 0.0,
                        juce::String (laneName (lane)) + " is silent at ghost 0");
            check (ghosting > 0.0005,
                   juce::String (laneName (lane)) + " ghosts at ghost 100 (RMS "
                       + juce::String (ghosting, 5) + ")");
        }

        // Mute and solo apply to ghosts, as they do to programmed hits.
        checkEqual (countGhosts (1, 100.0f, 100.0f, 64, true), 0,
                    "a muted channel produces no ghosts");
        checkEqual (countGhosts (1, 100.0f, 100.0f, 64, false, 3), 0,
                    "nor does a non-soloed one while another is soloed");

        // A step that HAS a programmed hit never also ghosts on that lane. With
        // ghost 100 and CACHAÇA 100 the chance would be 0.82 per step, so a
        // double trigger would push the level past a plain velocity-127 hit.
        const auto* triangulo = forrobox::ids::channelInfos[1].id;

        AudioRig plain { kSampleRate, 512 };
        plain.setStep (1, 0, 127);
        const auto plainPeak = bufferPeak (plain.render (static_cast<int> (18 * kStepSamples) / 512 * 512, 512));

        AudioRig both { kSampleRate, 512 };
        both.setValue (forrobox::ids::cachaca, 100.0f);
        both.setValue (forrobox::ids::channelParam (triangulo, forrobox::ids::ghost), 100.0f);

        for (int step = 0; step < 16; ++step)
            both.setStep (1, step, 127);

        const auto filledPeak = bufferPeak (both.render (static_cast<int> (18 * kStepSamples) / 512 * 512, 512));

        check (filledPeak <= plainPeak * 1.05f,
               juce::String ("a programmed step does not also ghost (peak ")
                   + juce::String (filledPeak, 4) + " against " + juce::String (plainPeak, 4) + ")");

        // And counted, because the peak comparison above CANNOT see it.
        //
        // A negative control that fired a ghost on every step alongside its hit
        // left all 896 checks green: a ghost at velocity 0.20-0.32 landing
        // beside a velocity-127 hit is simply drowned by it, and the two are
        // only 10 ms apart. Peak is the wrong instrument; the number of voices
        // is the right one.
        //
        // One programmed hit on one lane, ghost at 100 and CACHAÇA at 100 — so
        // a spurious ghost would fire with probability 0.82 — and the triângulo
        // sounds exactly one voice per hit, unlike the zabumba whose velocity
        // crossfade sounds two.
        AudioRig single { kSampleRate, 512 };
        single.setValue (forrobox::ids::cachaca, 100.0f);
        single.setValue (forrobox::ids::channelParam (triangulo, forrobox::ids::ghost), 100.0f);
        single.setStep (1, 0, 127);

        // 5632 samples = 11 blocks, which stops the clock short of step 1 at
        // 6000. A 1.5-step render reached step 1 — empty on this lane, so it
        // ghosted legitimately and the count was 2 for the right reason.
        single.render (5632, 512);

        checkEqual (single.processor.getVoiceEngine().getPeakActiveVoices(), 1,
                    "a step with a hit sounds ONE voice, not a hit plus a ghost");
    }

    void testGhostsStayOutOfTheUiChannel()
    {
        section ("ghosts are performance, not data");

        // PLANNING.md: ghosts are "not written into the pattern and are not
        // exported to MIDI — they are performance, not data". So they must not
        // reach lastStepVelocities either, which carries the step's PROGRAMMED
        // content for Phase 5's pads to draw ghost DOTS from — a different
        // thing that happens to share the word.
        AudioRig rig { kSampleRate, 512 };
        rig.setValue (forrobox::ids::cachaca, 100.0f);

        for (const auto& info : forrobox::ids::channelInfos)
            rig.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost), 100.0f);

        // One programmed hit, on one lane, on step 0. Everything else silent
        // and therefore a ghost opportunity.
        rig.setStep (0, 0, 100);

        auto buffer = rig.render (static_cast<int> (18 * kStepSamples) / 512 * 512, 512);

        check (bufferPeak (buffer) > 0.0f, "the render is not silent — ghosts are firing");

        // The pattern is unchanged.
        {
            auto state = rig.processor.lockPatternState();

            auto nonZero = 0;

            for (const auto& lane : state->lanes)
                for (const auto velocity : lane)
                    if (velocity != 0)
                        ++nonZero;

            checkEqual (nonZero, 1, "the pattern still holds exactly the one programmed hit");
        }

        // And the published velocities carry only programmed content: every
        // lane but the one is zero for every step the run visited.
        auto laneWithHits = 0;

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            if (rig.processor.getLastStepVelocity (lane) != 0)
                ++laneWithHits;

        check (laneWithHits <= 1,
               juce::String ("lastStepVelocities carries programmed content only (")
                   + juce::String (laneWithHits) + " non-zero lanes)");
    }

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
        // ONE rig for all 1024 combinations, not one per combination.
        //
        // Measured: 2658 ms of the suite's 4822 ms went here, for two checks —
        // and the cost was not the truth table. Constructing a processor is
        // 0.126 ms and destroying one 0.037 ms, but construct-then-destruct in a
        // loop measured 2.277 ms with 2.064 ms of it BLOCKED: JUCE's internal
        // timer thread, which APVTS drives, was being torn down and recreated
        // every time the live processor count went 1 -> 0 -> 1. Hoisting the rig
        // took it to 4.27 ms — 623x — with identical coverage, because every
        // iteration already sets all five channels' MUTE and SOLO explicitly,
        // so nothing carries over.
        AudioRig rig;

        for (int muteMask = 0; muteMask < (1 << numChannels); ++muteMask)
        {
            for (int soloMask = 0; soloMask < (1 << numChannels); ++soloMask)
            {
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

                    if (settings.channels[static_cast<size_t> (c)].audible != expected)
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
        //
        // The shared helper, not a local copy. The local copy written here could
        // not fail: it dropped the volatile escape that makes the probe
        // allocation unelidable, and it called check() — which builds a
        // juce::String, which allocates — between the two counter reads, so the
        // assertion passed on its own description string.
        fbtest::checkAllocationCounterRegisters();

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

        // Two seconds, not four. Measured: peak concurrency saturates at
        // exactly 2.0 s for all four velocities and reproduces the documented
        // worst case (48/53/70/61); 3 s and 4 s add nothing, and 1.0 s would
        // break it — velocity 90 reports 67 there rather than 70.
        auto buffer = rig.render (static_cast<int> (kSampleRate) * 2, 512);

        check (isFinite (buffer), "the densest possible pattern renders no NaN or infinity");
        check (bufferPeak (buffer) > 0.0f, "and is not silent");

        // getVoicesStolen, not getVoicesDropped.
        //
        // The drop counter CANNOT fire: claimSynthVoice and claimSampleVoice
        // steal rather than return null, so both ++voicesDropped branches are
        // unreachable while the pools are non-empty. Asserting it was zero was
        // the sixth assertion in this project that could not fail, and it was
        // guarding the one property it was written for — that the pool
        // arithmetic in VoiceEngine.h is right. Stealing is the real exhaustion
        // signal.
        constexpr auto capacity = forrobox::VoiceEngine::kSynthVoices
                                + forrobox::VoiceEngine::kSampleVoices;

        auto worstPeak = 0;

        const auto record = [&worstPeak] (const forrobox::VoiceEngine& engine,
                                          const juce::String& label)
        {
            const auto peak = engine.getPeakActiveVoices();
            worstPeak = juce::jmax (worstPeak, peak);

            checkEqual (engine.getVoicesStolen(), 0,
                        label + ": no voice is stolen at the densest possible pattern ("
                              + juce::String (peak) + " concurrent)");
            check (peak < capacity,
                   label + ": peak concurrency stays inside the pools (" + juce::String (peak)
                         + " of " + juce::String (capacity) + ")");
        };

        record (rig.processor.getVoiceEngine(), "velocity 127");

        // Mid velocities are the worse case, not the loudest: the zabumba's
        // crossfade sounds TWO layers there and collapses to one only at the
        // ends of the range. Measured 70 at velocity 90 against 61 at 127, so a
        // test that only tried full velocity would have understated the load.
        for (const std::uint8_t velocity : std::array<std::uint8_t, 3> { 40, 64, 90 })
        {
            AudioRig mid { kSampleRate, 512 };
            mid.setValue (forrobox::ids::bpm, 300.0f);
            mid.setChoice (forrobox::ids::steps, 1);

            for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
                for (int step = 0; step < 32; ++step)
                    mid.setStep (lane, step, velocity);

            for (const auto& info : forrobox::ids::channelInfos)
                mid.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::decay), 100.0f);

            auto midBuffer = mid.render (static_cast<int> (kSampleRate) * 2, 512);

            check (isFinite (midBuffer),
                   juce::String ("velocity ") + juce::String (velocity)
                       + ": renders no NaN or infinity");

            record (mid.processor.getVoiceEngine(),
                    juce::String ("velocity ") + juce::String (velocity));
        }

        // And the scenario really did load the pools. Without this the two
        // claims above would both pass against a pattern that triggered almost
        // nothing — which is how a green pool test proves nothing at all.
        check (worstPeak > 40,
               juce::String ("the pressure test really is dense (peak ") + juce::String (worstPeak)
                   + " concurrent voices)");
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

        // The same four profiles again, now with each one's OWN CACHAÇA and the
        // channels' own ghost probabilities — which is what a user actually
        // hears, and therefore what 03-03's limiter actually has to handle.
        //
        // Measured 2026-09-08 on this seed: 1.206 for CARUARU against 1.336
        // deterministic. It looked as though humanisation could only LOWER the
        // peak, since velocity variation only softens and ghosts are quiet —
        // and that conclusion was wrong. A review measured 1.408 against 1.251
        // under a different draw order: ghosts add VOICES, and voices sum.
        //
        // So 03-03's limiter is sized from the higher of the two, and this
        // asserts a distribution-wide bound rather than one seed's outcome.
        // Reproducible per instance — all three generators are seeded in
        // reset() — but one realisation of many.
        auto worstHumanised = 0.0f;

        for (const auto& profile : forrobox::allProfiles())
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::bpm, static_cast<float> (profile.bpm));
            rig.setValue (forrobox::ids::swing, profile.swing);
            rig.setValue (forrobox::ids::cachaca, profile.cachaca);

            for (const auto& info : forrobox::ids::channelInfos)
                rig.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost),
                              info.ghost);

            {
                auto state = rig.processor.lockPatternState();
                forrobox::applyProfile (*state, profile);
            }

            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                       forrobox::ids::mute),
                          profile.bateriaMuted ? 1.0f : 0.0f);

            worstHumanised = juce::jmax (worstHumanised, bufferPeak (rig.render (98304, 512)));
        }

        // NOT "humanisation does not raise the peak". That was asserted from a
        // single RNG realisation and is false in general: a review measured
        // 1.408 humanised against 1.251 deterministic under a different draw
        // order, because ghost notes ADD voices that can sum constructively.
        // The claim as written would have sized 03-03's limiter 12% low.
        //
        // What is asserted instead is a bound wide enough to be true of the
        // distribution rather than of one seed, and the limiter's design input
        // is taken from the HIGHER of the two figures.
        check (worstHumanised > 0.5f && worstHumanised < 1.8f,
               juce::String ("the humanised peak stays inside 1.8 (")
                   + juce::String (worstHumanised, 3) + " this realisation, against "
                   + juce::String (worstPeak, 3) + " deterministic)");

        check (worstPeak > 1.15f && worstPeak < 1.55f,
               juce::String ("the hottest profile (") + worstProfile + ") peaks at "
                   + juce::String (worstPeak, 3) + " deterministic");

        // ── the limiter's design input, over a DISTRIBUTION ─────────────────
        //
        // Not one realisation. 03-02 first asserted "humanisation does not
        // raise the worst peak" from a single draw and handed that figure to
        // 03-03; a review measured 1.408 where the suite measured 1.206, under
        // a different draw order, because ghosts ADD voices and voices sum. The
        // claim was false and the number 12% low.
        //
        // It could not be improved from outside either: the humanisation seed
        // was private with no injection point, so no test could render the same
        // configuration under a second realisation. setHumanisationSeedOffset
        // is that seam, and it exists for exactly this.
        auto worstAcrossSeeds = juce::jmax (worstPeak, worstHumanised);
        const char* worstSeedProfile = worstProfile;

        for (std::uint64_t seed = 1; seed <= 8; ++seed)
        {
            for (const auto& profile : forrobox::allProfiles())
            {
                AudioRig rig { kSampleRate, 512 };
                rig.processor.setHumanisationSeedOffset (seed);
                rig.setValue (forrobox::ids::bpm, static_cast<float> (profile.bpm));
                rig.setValue (forrobox::ids::swing, profile.swing);
                rig.setValue (forrobox::ids::cachaca, profile.cachaca);

                for (const auto& info : forrobox::ids::channelInfos)
                    rig.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost),
                                  info.ghost);

                {
                    auto state = rig.processor.lockPatternState();
                    forrobox::applyProfile (*state, profile);
                }

                rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                           forrobox::ids::mute),
                              profile.bateriaMuted ? 1.0f : 0.0f);

                const auto peak = bufferPeak (rig.render (49152, 512));

                if (peak > worstAcrossSeeds)
                {
                    worstAcrossSeeds = peak;
                    worstSeedProfile = profile.displayName();
                }
            }
        }

        check (worstAcrossSeeds >= juce::jmax (worstPeak, worstHumanised),
               "the seed seam finds at least what a single realisation did");

        // Measured 2026-09-08: 1.454, +3.25 dBFS, hottest CARUARU. Against the
        // 1.336 a single realisation reported — so even the corrected
        // single-draw figure was 9% low, and the original false claim 12%.
        // Recorded in 03-02-SUMMARY.md and STATE.md as 03-03's design input.
        check (worstAcrossSeeds > 1.15f && worstAcrossSeeds < 2.0f,
               juce::String ("03-03's limiter must handle at least ")
                   + juce::String (worstAcrossSeeds, 3) + " ("
                   + juce::String (juce::Decibels::gainToDecibels (worstAcrossSeeds), 1)
                   + " dBFS) — worst of 36 realisations, hottest " + worstSeedProfile);

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

    void testSampledLaneInvariant()
    {
        section ("sampled lanes read their own channel");

        // The render path used to hardcode channelForLane (0) for every sample
        // voice, while the actual selector is voiceSpecs[lane].usesSample. The
        // voice now carries its own channel, so a second sampled lane cannot
        // inherit ZABUMBA's VOL and PAN — but the invariant the old code
        // assumed is worth pinning either way, because it is the thing that
        // made the bug invisible.
        auto sampledLanes = 0;
        auto firstSampled = -1;

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
        {
            if (forrobox::voiceSpecs[static_cast<size_t> (lane)].usesSample)
            {
                ++sampledLanes;

                if (firstSampled < 0)
                    firstSampled = lane;
            }
        }

        checkEqual (sampledLanes, 1, "exactly one lane is sampled today");
        checkEqual (firstSampled, 0, "and it is lane 0");
        check (juce::String (laneName (0)) == juce::String ("zabumba"),
               "lane 0 is the zabumba");
        checkEqual (forrobox::VoiceEngine::channelForLane (0), 0,
                    "which maps to the zabumba channel");

        // The sampled lane's VOL is read from ITS channel: setting a DIFFERENT
        // channel's VOL to zero must not silence it. This is the assertion that
        // fails if a sampled voice ever reads the wrong channel's settings.
        AudioRig rig;
        rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                   forrobox::ids::vol), 0.0f);
        rig.setStep (0, 0, 127);

        check (bufferPeak (rig.render (48000)) > 0.0005f,
               "zeroing BATERIA's VOL does not silence the zabumba");

        // And its own VOL does silence it.
        AudioRig own;
        own.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                   forrobox::ids::vol), 0.0f);
        own.setStep (0, 0, 127);

        checkSilent (own.render (48000), "zeroing the ZABUMBA's own VOL does silence it");
    }

    void testMonoOutputFoldsDown()
    {
        section ("a mono output folds both sides down");

        // isBusesLayoutSupported accepts stereo only, so this path is
        // unreachable through a host today — but render() has an explicit
        // `right == nullptr` branch, and it used to write only the left-ward
        // matrix terms. At PAN +50 that is cos(pi/2) = 0, so a hard-right
        // channel disappeared completely instead of summing to mono. The
        // MULTI-OUT parameter is already declared, so this is a trap laid for
        // whoever relaxes the bus check, not dead code.
        for (const float pan : { -50.0f, 0.0f, 50.0f })
        {
            AudioRig rig { kSampleRate, 512 };
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                       forrobox::ids::pan), pan);
            rig.setStep (4, 0, 127);

            // A one-channel buffer handed straight to processBlock, which is
            // what a mono bus would deliver.
            //
            // Rendered until the hit actually arrives: the 32 ms lookahead is
            // three 512-sample blocks, so a single block would be silence and
            // this test would fail for a reason that has nothing to do with
            // panning.
            juce::AudioBuffer<float> mono (1, 512);
            juce::MidiBuffer midi;

            rig.processor.setPlaying (true);

            auto loudest = 0.0f;

            for (int block = 0; block < 8; ++block)
            {
                mono.clear();
                rig.processor.processBlock (mono, midi);
                loudest = juce::jmax (loudest, mono.getMagnitude (0, 0, 512));
            }

            check (loudest > 0.0005f,
                   juce::String ("a hard-panned channel survives a mono output at PAN ")
                       + juce::String (pan, 0));
        }

        // Same for the sampled path, which has its own matrix.
        AudioRig zabumba { kSampleRate, 512 };
        zabumba.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                       forrobox::ids::pan), 50.0f);
        zabumba.setStep (0, 0, 127);

        juce::AudioBuffer<float> mono (1, 512);
        juce::MidiBuffer midi;

        zabumba.processor.setPlaying (true);

        auto zabumbaLoudest = 0.0f;

        for (int block = 0; block < 8; ++block)
        {
            mono.clear();
            zabumba.processor.processBlock (mono, midi);
            zabumbaLoudest = juce::jmax (zabumbaLoudest, mono.getMagnitude (0, 0, 512));
        }

        check (zabumbaLoudest > 0.0005f,
               "a hard-right sampled zabumba survives a mono output too");
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
        auto longestSeconds = 0.0;

        for (int slot = 0; slot < forrobox::ZabumbaSampler::kMaxSlots; ++slot)
        {
            if (! sampler.isLoaded (slot))
                continue;

            longestSeconds = juce::jmax (longestSeconds,
                                         static_cast<double> (sampler.getLengthSamples (slot))
                                           / sampler.getFileSampleRate (slot));
        }

        const auto worstCaseSeconds = longestSeconds * 2.0;   // PITCH -12

        check (rig.processor.getTailLengthSeconds() >= worstCaseSeconds * 0.99,
               juce::String ("the reported tail covers the longest possible voice (")
                   + juce::String (worstCaseSeconds, 3) + " s)");

        // AND the humanisation's own reach on top of it. A trigger can land
        // 32 ms after the event that scheduled it — the step's jitter plus a
        // ghost's own offset — so a bounce that stopped at the voice length
        // alone would truncate the final decay by up to 30 ms. The 32 ms
        // lookahead itself needs no allowance: the host compensates that.
        const auto humanisedTail = worstCaseSeconds + forrobox::kMaxJitterSeconds
                                                    + forrobox::kGhostJitterSeconds;

        check (rig.processor.getTailLengthSeconds() >= humanisedTail * 0.99,
               juce::String ("and the humanisation's reach as well (needs ")
                   + juce::String (humanisedTail, 4) + " s, reports "
                   + juce::String (rig.processor.getTailLengthSeconds(), 4) + " s)");
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

        // Enough blocks for the hit to clear the 32 ms lookahead (three
        // 512-sample blocks) and be well into its decay before the stop.
        for (int i = 0; i < 6; ++i)
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

        // The profile's own CACHAÇA now that 03-02 has built it — 22 for
        // CAMPINA GRANDE, 32 for CARUARU, and so on. Pinned to 0 while
        // humanisation did not exist, because rendering with it set would have
        // suggested it did something.
        rig.setValue (forrobox::ids::cachaca, profile.cachaca);

        // And each channel's own ghost probability, from the channel defaults.
        for (const auto& info : forrobox::ids::channelInfos)
            rig.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost), info.ghost);

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
                  << "  cachaça " << juce::String (profile.cachaca, 0).toStdString()
                  << "  raw peak " << juce::String (rawPeak, 4).toStdString()
                  << (rawPeak > 1.0f ? " (CLIPS — awaiting 03-03's limiter)" : "")
                  << "  normalised by " << juce::String (juce::Decibels::gainToDecibels (normalisation), 1).toStdString()
                  << " dB  -> " << file.getFileName() << "\n";
    }
}

void runVoiceTests()
{
    // The instruments first: a broken one makes everything after it meaningless.
    testMeasurementInstruments();

    testSamplerClassification();
    testSamplerVelocityBlend();
    testLayersAreLevelMatched();
    testPitchTracksPerComponent();
    testZabumbaRendersAtBothRates();
    testZabumbaVelocityIsMonotonic();
    testVoiceSpectra();
    testCaixaHasBothLayers();
    testFrequencySweeps();
    testTrianguloIsBandLimited();
    testVoiceDurations();
    testTrianguloArticulation();
    testOnsetAccuracy();
    testBlockSizeIndependence();
    testDeterminism();
    testLatencyIsReported();
    testJitterIsOneDrawPerStep();
    testMutingDoesNotRetimeOtherChannels();
    testMutingDoesNotRelevelOtherChannels();
    testHumanisationKeying();
    testLookaheadCoversEveryRate();
    testPercentNormalisation();
    testJitterDistribution();
    testVelocityHumanisation();
    testChannelParameters();
    testVelocityMonotonicForSynthVoices();
    testGhostRate();
    testGhostProperties();
    testGhostLoudnessIsIndependentOfProbability();
    testGhostsOnlyWhereAllowed();
    testGhostsStayOutOfTheUiChannel();
    testMuteSoloTruthTable();
    testMuteSoloSilencesAudio();
    testNoAllocationsWhileRendering();
    testVoicePoolUnderPressure();
    testProfileHeadroom();
    testLaneToChannelMapping();
    testSampledLaneInvariant();
    testMonoOutputFoldsDown();
    testTailIsReportedToHost();
    testVoicesRingThroughTransportStop();
}

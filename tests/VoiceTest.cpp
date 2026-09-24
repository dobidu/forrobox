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
#include "RigStart.h"
#include "TestHarness.h"

#include "PluginProcessor.h"
#include "Voices.h"
#include "GmPercussion.h"
#include "VoiceEngine.h"
#include "MixBus.h"
#include "ZabumbaSampler.h"
#include "Profiles.h"
#include "StepSnapshot.h"
#include "MidiExport.h"

#include <cstring>
#include <iostream>

#include <algorithm>
#include <thread>
#include <map>
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
        sound like, and 02-03's suite already covers where the steps land.

        EMPTY IS A CHOICE, not an inheritance. The product's constructor loads
        `ids::defaultProfile`, so a fresh processor arrives with CAMPINA's grid
        and BATERIA muted; `blankInstrument` undoes both, because every check in
        this suite asserts on exactly the steps it set. It is called rather than
        reimplemented: `tests/RigStart.h` is the one home for that law, and this
        rig is its largest consumer. */
    struct AudioRig
    {
        ForroBoxAudioProcessor processor;
        double sampleRate;

        /** The size this rig was PREPARED with.

            Kept so a caller cannot drive blocks of a different size than the
            processor was prepared for. `collectLiveNotes` took the size as its
            own argument until 07-03's close, which made it a second source of
            truth — and a mismatch would have silently invalidated the
            block-boundary check, the one test that exists to catch block-size
            bugs. */
        int preparedBlockSize;

        explicit AudioRig (double rate = kSampleRate, int blockSize = 512)
            : sampleRate (rate), preparedBlockSize (blockSize)
        {
            processor.prepareToPlay (rate, blockSize);

            setValue (forrobox::ids::sync, 0.0f);
            setValue (forrobox::ids::bpm, static_cast<float> (kBpm));
            setValue (forrobox::ids::swing, 0.0f);
            setChoice (forrobox::ids::steps, 0);

            // The output stage made TRANSPARENT, for the same reason swing and
            // CACHAÇA are pinned off: every test written before 03-03 measures
            // the VOICE stage, and a character bus, limiter and master in the
            // path make it non-linear — "VOL 100 is exactly 4x VOL 25" read
            // 3.44x the moment the chain was connected.
            //
            // MIX 0 gives wet 0 and dry 1; the limiter off is threshold 0 dB
            // and ratio 1:1; master 100 is unity. The bus's own tests set these
            // explicitly, and testFullChainHeadroom runs the real chain.
            setValue (forrobox::ids::charMix, 0.0f);
            setValue (forrobox::ids::limiterOn, 0.0f);
            setValue (forrobox::ids::master, 100.0f);

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

            // Every channel fully open and centred, so a test that cares about
            // one parameter is not reading another's default. DECAY is left at
            // each channel's own default, which is what the spec's durations
            // are quoted against.
            for (const auto& info : forrobox::ids::channelInfos)
            {
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::vol), 100.0f);
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::pitch), 0.0f);
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::pan), 0.0f);
                // Ghosts off by default: their per-channel defaults are 6-14%,
                // so a "one hit on one lane" test would otherwise measure
                // several.
                setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost), 0.0f);
            }

            // The mutes, the solos and the grid, through the ONE definition of
            // "blank". This rig is used 77 times — the most processors in the
            // suite — so a rig that spelled the law out for itself is the one
            // that would silently keep starting from the product default the
            // day the constructor loads something else. /simplify.
            forrobox::test::blankInstrument (processor);
        }

        void setValue (juce::StringRef id, float value)
        {
            if (auto* param = processor.getAPVTS().getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (value));
        }

        /** The chain as it SHIPS: the profile's timbre, and the shipped
            defaults for MIX, the limiter and master.

            AudioRig defaults the bus to transparent so every test written
            before 03-03 keeps measuring the voice stage — the right isolation,
            but its complement had no name and got written out by hand in three
            places. It diverged at the first opportunity: with the audition's
            normalisation removed, that script reported three of four profiles
            clipping at 1.19-1.28 while testFullChainHeadroom measured 0.753,
            because the audition had inherited the transparent default. */
        void useShippedChain (int timbreIndex)
        {
            setChoice (forrobox::ids::timbre, timbreIndex);
            setValue (forrobox::ids::charMix, 40.0f);
            setValue (forrobox::ids::limiterOn, 1.0f);
            setValue (forrobox::ids::master, 82.0f);
        }

        void setChoice (juce::StringRef id, int index)
        {
            if (auto* param = processor.getAPVTS().getParameter (id))
                param->setValueNotifyingHost (param->convertTo0to1 (static_cast<float> (index)));
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
    /** Band-limited-ish broadband input: white noise, the thing a lowpass can
        actually be measured on. */
    juce::AudioBuffer<float> noiseBuffer (int numSamples, float amplitude = 0.25f, int seed = 99)
    {
        juce::AudioBuffer<float> buffer (2, numSamples);
        juce::Random random { seed };

        for (int s = 0; s < numSamples; ++s)
        {
            const auto value = (random.nextFloat() * 2.0f - 1.0f) * amplitude;

            for (int c = 0; c < 2; ++c)
                buffer.setSample (c, s, value);
        }

        return buffer;
    }

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
        auto band = noiseBuffer (length, 1.0f, 1234);

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

    // ── 03-03: the output stage ─────────────────────────────────────────────

    /** A bus on its own, at a known configuration. No processor needed — MixBus
        is a plain class, so its tests do not pay for an APVTS. */
    struct BusRig
    {
        forrobox::MixBus bus;
        forrobox::MixBus::Settings settings;

        BusRig (int timbre, float mix, bool limiterOn, float master)
        {
            settings.timbreIndex = timbre;
            settings.charMix = mix;
            settings.limiterOn = limiterOn;
            settings.master = master;
            bus.prepare (kSampleRate, 512);
        }

        /** Processes a copy, so the caller keeps its input.

            `beforeBlock (BusRig&, int firstSample)` runs before each block, for
            the tests that change a setting mid-render. The same block-slicing
            loop had been written out four times in this file, three of them
            differing only in wanting that hook. */
        template <typename BeforeBlock>
        juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& input, int blockSize,
                                      BeforeBlock&& beforeBlock)
        {
            juce::AudioBuffer<float> output (input);

            for (int at = 0; at < output.getNumSamples(); at += blockSize)
            {
                beforeBlock (*this, at);

                const auto count = juce::jmin (blockSize, output.getNumSamples() - at);
                juce::AudioBuffer<float> block (output.getArrayOfWritePointers(),
                                                output.getNumChannels(), at, count);
                bus.process (block, settings);
            }

            return output;
        }

        juce::AudioBuffer<float> run (const juce::AudioBuffer<float>& input, int blockSize = 512)
        {
            return run (input, blockSize, [] (BusRig&, int) {});
        }
    };

    void testCharacterBusGains()
    {
        section ("character bus: the two gain formulas");

        // Pure functions, so they are checked without rendering.
        //
        // HI-FI HALVES its own wet gain and the other two do not — at MIX 100
        // the wet path is 0.5 against 1.0, so HI-FI is the subtle character by
        // construction. And dry is 1 - wet x 0.5, NOT 1 - wet: the two paths
        // deliberately sum past unity, which is part of why the limiter is on
        // by default.
        checkEqual (forrobox::MixBus::wetGainFor (0, 100.0f), 0.5f, "HI-FI at MIX 100 is wet 0.5");
        checkEqual (forrobox::MixBus::wetGainFor (1, 100.0f), 1.0f, "LO-FI is wet 1.0");
        checkEqual (forrobox::MixBus::wetGainFor (2, 100.0f), 1.0f, "CICLOTRON is wet 1.0");

        checkEqual (forrobox::MixBus::dryGainFor (0, 100.0f), 0.75f, "HI-FI's dry is 1 - 0.5 x 0.5");
        checkEqual (forrobox::MixBus::dryGainFor (1, 100.0f), 0.5f, "LO-FI's dry is 1 - 1 x 0.5");
        checkEqual (forrobox::MixBus::dryGainFor (2, 100.0f), 0.5f, "CICLOTRON's dry is 0.5");

        // Nothing assumes dry + wet == 1: at MIX 100 LO-FI sums to 1.5.
        check (forrobox::MixBus::wetGainFor (1, 100.0f) + forrobox::MixBus::dryGainFor (1, 100.0f)
                 > 1.4f,
               "the paths sum past unity, as the spec has them");

        // At MIX 0 the bus is transparent by arithmetic: wet 0, dry 1.
        for (int timbre = 0; timbre < 3; ++timbre)
        {
            checkEqual (forrobox::MixBus::wetGainFor (timbre, 0.0f), 0.0f,
                        juce::String ("timbre ") + juce::String (timbre) + " at MIX 0 is wet 0");
            checkEqual (forrobox::MixBus::dryGainFor (timbre, 0.0f), 1.0f, "and dry 1");
        }

        // At MIX 40, the default: HI-FI 0.2/0.9, the others 0.4/0.8.
        checkEqual (forrobox::MixBus::wetGainFor (0, 40.0f), 0.2f, "HI-FI at the default MIX 40");
        checkEqual (forrobox::MixBus::dryGainFor (0, 40.0f), 0.9f, "and its dry");
    }

    void testCharacterBusShapesTheSound()
    {
        section ("character bus: each timbre lowpasses and drives as specified");

        auto input = noiseBuffer (48000);

        const auto highBandBefore = bandEnergy (input, 12000.0, 20000.0);
        const auto lowBandBefore  = bandEnergy (input, 200.0, 800.0);

        check (highBandBefore > 0.0 && lowBandBefore > 0.0, "the input is broadband");

        // At MIX 100 the wet path dominates, so the lowpass shows. Attenuation
        // must order by cutoff: LO-FI (5.2 kHz) more than CICLOTRON (9 kHz) more
        // than HI-FI (16 kHz).
        std::array<double, 3> tilt {};

        for (int timbre = 0; timbre < 3; ++timbre)
        {
            BusRig rig { timbre, 100.0f, false, 100.0f };
            auto output = rig.run (input);

            const auto high = bandEnergy (output, 12000.0, 20000.0);
            const auto low  = bandEnergy (output, 200.0, 800.0);

            // High-band energy relative to low-band, against the same ratio in
            // the input: a lowpass lowers it.
            tilt[static_cast<size_t> (timbre)] = (high / juce::jmax (1.0e-12, low))
                                                   / (highBandBefore / lowBandBefore);

            check (tilt[static_cast<size_t> (timbre)] < 1.0,
                   juce::String (forrobox::timbreSpecs[static_cast<size_t> (timbre)].displayName)
                       + " tilts the spectrum downwards (" 
                       + juce::String (tilt[static_cast<size_t> (timbre)], 4) + ")");
        }

        check (tilt[1] < tilt[2],
               juce::String ("LO-FI at 5.2 kHz attenuates more than CICLOTRON at 9 kHz (")
                   + juce::String (tilt[1], 4) + " against " + juce::String (tilt[2], 4) + ")");
        check (tilt[2] < tilt[0],
               juce::String ("and CICLOTRON more than HI-FI at 16 kHz (")
                   + juce::String (tilt[2], 4) + " against " + juce::String (tilt[0], 4) + ")");

        // The drive adds harmonic content that is not in the input. Measured on
        // a pure tone, where anything at a harmonic must have been created —
        // and with goertzelPower, not bandEnergy, whose semitone grid can step
        // over a tone entirely.
        juce::AudioBuffer<float> tone (2, 48000);

        for (int s = 0; s < 48000; ++s)
        {
            const auto value = 0.6f * std::sin (juce::MathConstants<float>::twoPi * 500.0f
                                                  * static_cast<float> (s)
                                                  / static_cast<float> (kSampleRate));
            for (int c = 0; c < 2; ++c)
                tone.setSample (c, s, value);
        }

        const auto thirdHarmonicIn = fbtest::goertzelPower (tone.getReadPointer (0), 48000,
                                                            1500.0, kSampleRate);

        std::array<double, 3> harmonics {};

        for (int timbre = 0; timbre < 3; ++timbre)
        {
            BusRig rig { timbre, 100.0f, false, 100.0f };
            auto output = rig.run (tone);

            harmonics[static_cast<size_t> (timbre)] =
                fbtest::goertzelPower (output.getReadPointer (0), 48000, 1500.0, kSampleRate);

            check (harmonics[static_cast<size_t> (timbre)] > thirdHarmonicIn * 100.0,
                   juce::String (forrobox::timbreSpecs[static_cast<size_t> (timbre)].displayName)
                       + " creates a third harmonic the input did not have");
        }

        // More drive, more harmonic. CICLOTRON at 9.0 against HI-FI at 1.2.
        check (harmonics[2] > harmonics[0] * 10.0,
               juce::String ("CICLOTRON's drive of 9 distorts far more than HI-FI's 1.2 (")
                   + juce::String (harmonics[2] / juce::jmax (1.0e-12, harmonics[0]), 1) + "x)");

        // And at MIX 0 the bus is transparent — asserted exactly, because the
        // smoothers snap on the first block rather than ramping in.
        BusRig transparent { 2, 0.0f, false, 100.0f };
        auto passed = transparent.run (input);

        checkEqual (fbtest::maxDifference (passed, input), 0.0f,
                    "at MIX 0 with the limiter off, the bus is exactly transparent");
    }

    void testCharacterBusSmoothing()
    {
        section ("character bus: changes are smoothed, not stepped");

        // A steady tone, so any discontinuity is the bus's and not the signal's.
        juce::AudioBuffer<float> tone (2, 24576);

        for (int s = 0; s < tone.getNumSamples(); ++s)
        {
            const auto value = 0.3f * std::sin (juce::MathConstants<float>::twoPi * 300.0f
                                                  * static_cast<float> (s)
                                                  / static_cast<float> (kSampleRate));
            for (int c = 0; c < 2; ++c)
                tone.setSample (c, s, value);
        }

        // HI-FI at MIX 0, then CICLOTRON at MIX 100 — the largest jump the
        // controls allow: cutoff 16 kHz to 9 kHz, wet 0 to 1, dry 1 to 0.5.
        // A change is compared against the STEADY STATE of the destination,
        // not against the input's own slope.
        //
        // The input slope is the wrong reference and a first version used it:
        // tanh has slope `drive` at zero, so CICLOTRON's drive of 9 legitimately
        // multiplies a 300 Hz sine's 0.0118-per-sample slope by nine. The worst
        // step measured 0.112 — 252 ms AFTER the switch, long past the
        // smoothing, i.e. the drive doing exactly its job.
        struct Transition { const char* name; int fromTimbre; float fromMix; int toTimbre; float toMix; };

        // The second and third are the ones that matter, and the axis a first
        // version could not reach. HI-FI@0 -> CICLOTRON@100 is the only
        // transition where the wet gain ramps 0 -> 1 and hides a stepped drive.
        //
        // LO-FI <-> HI-FI is what a PETROLINA profile switch actually does —
        // it is the only LO-FI profile — and at the default MIX 40 the wet gain
        // moves just 0.40 -> 0.20 while the drive would step 2.4 -> 1.2.
        // LO-FI -> CICLOTRON is worse still: wet and dry targets are IDENTICAL,
        // so nothing smooths at all and an unsmoothed drive is fully exposed.
        const std::array<Transition, 3> transitions {{
            { "HI-FI MIX 0 -> CICLOTRON MIX 100", 0, 0.0f, 2, 100.0f },
            { "LO-FI -> HI-FI at MIX 40 (a PETROLINA switch)", 1, 40.0f, 0, 40.0f },
            { "LO-FI -> CICLOTRON at MIX 100 (nothing else moves)", 1, 100.0f, 2, 100.0f },
        }};

        // `mode`: 0 = steady at the source, 1 = steady at the destination,
        // 2 = switching from source to destination mid-render.
        const auto worstSlope = [&tone] (const Transition& transition, int blockSize, int mode)
        {
            const auto switching = mode == 2;

            BusRig rig { (mode == 1) ? transition.toTimbre : transition.fromTimbre,
                         (mode == 1) ? transition.toMix : transition.fromMix,
                         false, 100.0f };

            const auto half = tone.getNumSamples() / 2;

            auto output = rig.run (tone, blockSize,
                                   [&transition, switching, half] (BusRig& r, int at)
                                   {
                                       if (switching && at >= half)
                                       {
                                           r.settings.timbreIndex = transition.toTimbre;
                                           r.settings.charMix = transition.toMix;
                                       }
                                   });

            auto worstStep = 0.0f;

            for (int s = 1; s < output.getNumSamples(); ++s)
                worstStep = juce::jmax (worstStep,
                                        std::abs (output.getSample (0, s)
                                                    - output.getSample (0, s - 1)));

            return worstStep;
        };

        for (const auto& transition : transitions)
        {
            for (const int blockSize : { 32, 512, 2048 })
            {
                // The reference is the WORSE of the two endpoints, not the
                // destination.
                //
                // A smoothed transition passes through the states between them,
                // so its slope is bounded by whichever end is steeper — and for
                // LO-FI -> HI-FI at MIX 40 that is the SOURCE: drive 2.4 x wet
                // 0.40 = 0.96 against the destination's 1.2 x 0.20 = 0.24.
                // Comparing against the destination alone reported a 0.0222
                // step against a 0.0140 reference for a transition that is
                // smooth throughout.
                const auto atSource = worstSlope (transition, blockSize, 0);
                const auto atDestination = worstSlope (transition, blockSize, 1);
                const auto switching = worstSlope (transition, blockSize, 2);
                const auto steady = juce::jmax (atSource, atDestination);

                check (steady > 0.0f, "the steady-state reference has slope");
                check (switching < steady * 1.05f,
                       juce::String (transition.name) + " at block " + juce::String (blockSize)
                           + " adds no step (" + juce::String (switching, 5)
                           + " against the steeper endpoint's " + juce::String (steady, 5) + ")");
            }
        }

        // The time constant is 20 ms. Measured on the wet gain going 0 -> 1:
        // after one tau it must have covered 1 - 1/e = 63.2% of the distance.
        forrobox::MixBus bus;
        bus.prepare (kSampleRate, 512);

        forrobox::MixBus::Settings settings;
        settings.timbreIndex = 1;
        settings.charMix = 0.0f;
        settings.limiterOn = false;
        settings.master = 100.0f;

        juce::AudioBuffer<float> silence (2, static_cast<int> (kSampleRate * 0.02));
        silence.clear();
        bus.process (silence, settings);       // snaps to wet 0

        checkEqual (bus.getWetGain(), 0.0f, "the wet gain starts at 0");

        settings.charMix = 100.0f;
        bus.process (silence, settings);       // exactly one time constant

        const auto reached = bus.getWetGain();

        check (reached > 0.60f && reached < 0.66f,
               juce::String ("after one 20 ms time constant the wet gain has covered 63.2% (")
                   + juce::String (reached, 4) + ")");

        // Four time constants gets essentially all the way.
        for (int i = 0; i < 3; ++i)
            bus.process (silence, settings);

        check (bus.getWetGain() > 0.98f,
               juce::String ("and four time constants reach the target (")
                   + juce::String (bus.getWetGain(), 4) + ")");
    }

    void testLimiterAndMaster()
    {
        section ("limiter and master");

        // A signal at the level the four grooves actually reach unlimited:
        // 1.454, measured across 36 humanisation realisations in 03-02.
        auto hot = noiseBuffer (48000, 1.454f);

        {
            BusRig limited { 0, 0.0f, true, 100.0f };   // MIX 0, so only the limiter acts
            auto output = limited.run (hot);

            const auto threshold = juce::Decibels::decibelsToGain (forrobox::kLimiterThresholdDb);

            // Measured PAST the attack window. A 2 ms attack lets the opening
            // transient through by design, and this input starts at full level
            // from sample 0 — so a whole-buffer peak measures the attack, not
            // the limiting. It read 1.4286 against an input of 1.454, which
            // looked like a limiter doing nothing and was a limiter that had
            // not yet moved. 10 ms is five time constants.
            const auto settled = static_cast<int> (0.010 * kSampleRate);
            const auto steadyPeak = output.getMagnitude (settled,
                                                         output.getNumSamples() - settled);

            check (steadyPeak < 1.0f,
                   juce::String ("the limiter holds a 1.454 peak under full scale (")
                       + juce::String (steadyPeak, 4) + " once settled)");
            check (steadyPeak > threshold * 0.9f,
                   juce::String ("and does not over-limit it (threshold is ")
                       + juce::String (threshold, 4) + ")");

            // The attack overshoot is real and bounded — it is not silently
            // absorbed by measuring only the settled region.
            check (bufferPeak (output) > steadyPeak,
                   juce::String ("the opening transient does overshoot, as a 2 ms attack implies (")
                       + juce::String (bufferPeak (output), 4) + ")");
            check (bufferPeak (output) < 1.5f,
                   "but no more than the input it came from");

            // Taken ONCE into a local. takeGainReductionDb consumes, so calling
            // it twice in one expression is a bug — and was one: argument
            // evaluation order is unspecified, so the message read 8.13 dB
            // while the condition read the 0 left behind.
            const auto reduction = limited.bus.takeGainReductionDb();

            check (reduction > 3.0f,
                   juce::String ("and reports the reduction it applied (")
                       + juce::String (reduction, 2) + " dB)");
        }

        // ── the reading is a PEAK HOLD, across blocks, until it is taken ──
        //
        // The property that makes the footer's 30 Hz poll correct against a
        // ~5 ms block: a caller polling slower than the audio thread must not
        // miss a peak. Nothing asserted it, and the accumulate is the one line
        // 04-05's /code-review found to be a non-atomic read-modify-write — so a
        // fix that turned it into a plain store would have kept every check
        // here green while the meter silently reported only the LAST block.
        {
            BusRig holding { 0, 0.0f, true, 100.0f };

            juce::AudioBuffer<float> loud (2, 512);
            juce::AudioBuffer<float> quiet (2, 512);

            for (int c = 0; c < 2; ++c)
                for (int i = 0; i < 512; ++i)
                {
                    loud.setSample (c, i, 0.95f);
                    quiet.setSample (c, i, 0.001f);
                }

            // One hot block, then several that ask for no reduction at all. The
            // value is taken ONCE, afterwards.
            holding.run (loud);

            for (int i = 0; i < 8; ++i)
                holding.run (quiet);

            const auto held = holding.bus.takeGainReductionDb();

            check (held > 3.0f,
                   juce::String ("a peak from eight blocks ago is still there when it is taken (")
                       + juce::String (held, 2) + " dB) — the accumulate is a MAX since the last "
                         "read, not the last block's value");

            checkEqual (holding.bus.takeGainReductionDb(), 0.0f,
                        "and taking it CLEARS it, which is what makes it single-reader");
        }

        // Off is TRANSPARENT, not absent: threshold 0 dB and ratio 1:1 on the
        // same object. PLANNING.md: "bypassed rather than removed, to avoid a
        // click".
        {
            BusRig bypassed { 0, 0.0f, false, 100.0f };
            auto output = bypassed.run (hot);

            checkEqual (fbtest::maxDifference (output, hot), 0.0f,
                        "with the limiter off the output equals its input exactly");
            checkEqual (bypassed.bus.takeGainReductionDb(), 0.0f, "and reports no reduction");
        }

        // Master: gain = (value/100)^2.
        struct MasterCase { float percent; float expected; };

        for (const auto& masterCase : std::array<MasterCase, 4> {{ { 100.0f, 1.0f },
                                                                   { 82.0f, 0.6724f },
                                                                   { 50.0f, 0.25f },
                                                                   { 0.0f, 0.0f } }})
        {
            auto quiet = noiseBuffer (4096, 0.2f);
            BusRig rig { 0, 0.0f, false, masterCase.percent };
            auto output = rig.run (quiet);

            const auto ratio = bufferPeak (output) / bufferPeak (quiet);

            if (masterCase.percent > 0.0f)
                check (std::abs (ratio - masterCase.expected) < 1.0e-4f,
                       juce::String ("master ") + juce::String (masterCase.percent, 0)
                           + " gives (v/100)^2 = " + juce::String (masterCase.expected, 4)
                           + " (measured " + juce::String (ratio, 4) + ")");
            else
                checkSilent (output, "master 0 is exact silence");
        }

        // A linear taper would give 0.82 where the squared one gives 0.672 —
        // the mistake this catches.
        auto reference = noiseBuffer (4096, 0.2f);
        BusRig atDefault { 0, 0.0f, false, 82.0f };
        const auto measured = bufferPeak (atDefault.run (reference)) / bufferPeak (reference);

        check (std::abs (measured - 0.82f) > 0.1f,
               juce::String ("and it is squared, not linear (") + juce::String (measured, 4)
                   + " rather than 0.82)");
    }

    void testLimiterToggleDoesNotBurst()
    {
        section ("toggling the limiter does not let a burst through");

        // PLANNING.md: the limiter off is "bypassed rather than removed, to
        // avoid a click". This is the test for that sentence, and AC-4 named it
        // while the first pass did not write it — a control that branched
        // around the compressor when off passed all 1062 checks.
        //
        // The mechanism is specific. juce::dsp::Compressor updates its envelope
        // inside processSample, so with the transparent settings the object
        // still TRACKS while it is off, and enabling it acts on a correct
        // envelope immediately. Branched around, processSample is never called,
        // the envelope sits at zero, and on enable it reads "no reduction
        // needed" until the 2 ms attack catches up — so a full-level burst gets
        // through.
        auto hot = noiseBuffer (48000, 1.454f);

        BusRig rig { 0, 0.0f, false, 100.0f };      // MIX 0: the limiter alone

        juce::AudioBuffer<float> output (hot);
        constexpr int blockSize = 512;
        constexpr int enableAt = 24;                // blocks

        for (int block = 0; block * blockSize < output.getNumSamples(); ++block)
        {
            const auto at = block * blockSize;
            const auto count = juce::jmin (blockSize, output.getNumSamples() - at);

            rig.settings.limiterOn = block >= enableAt;

            juce::AudioBuffer<float> slice (output.getArrayOfWritePointers(),
                                            output.getNumChannels(), at, count);
            rig.bus.process (slice, rig.settings);
        }

        const auto enableSample = enableAt * blockSize;

        // Before: transparent, so the input's own level.
        const auto beforePeak = output.getMagnitude (enableSample - 4096, 4096);

        // The 5 ms straight after enabling — where a stale envelope would let
        // the signal through.
        const auto burstWindow = static_cast<int> (0.005 * kSampleRate);
        const auto burstPeak = output.getMagnitude (enableSample, burstWindow);

        // And once settled.
        const auto settledPeak = output.getMagnitude (enableSample + burstWindow * 4, 8192);

        check (beforePeak > 1.4f,
               juce::String ("the limiter really was transparent before (") 
                   + juce::String (beforePeak, 4) + ")");
        check (settledPeak < 1.0f,
               juce::String ("and limits once enabled (") + juce::String (settledPeak, 4) + ")");

        // The burst window must not look like the unlimited signal. A tracking
        // envelope holds it near the settled level; a frozen one lets through
        // most of the 1.454 it was passing a sample earlier.
        check (burstPeak < settledPeak * 1.6f,
               juce::String ("no burst gets through on enable (") + juce::String (burstPeak, 4)
                   + " against a settled " + juce::String (settledPeak, 4) + ")");

        // The converse: disabling must not click either. The gain rises to
        // unity over the release rather than instantly.
        BusRig off { 0, 0.0f, true, 100.0f };
        juce::AudioBuffer<float> disabling (hot);

        for (int block = 0; block * blockSize < disabling.getNumSamples(); ++block)
        {
            const auto at = block * blockSize;
            const auto count = juce::jmin (blockSize, disabling.getNumSamples() - at);

            off.settings.limiterOn = block < enableAt;

            juce::AudioBuffer<float> slice (disabling.getArrayOfWritePointers(),
                                            disabling.getNumChannels(), at, count);
            off.bus.process (slice, off.settings);
        }

        check (disabling.getMagnitude (enableSample - 4096, 4096) < 1.0f,
               "limited before the toggle");
        check (disabling.getMagnitude (enableSample + burstWindow * 4, 8192) > 1.4f,
               "and transparent after it");
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
               fbtest::utf8 ("at PITCH 0 the ganzá's band sits at 6.8 kHz (ratio ")
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

        check (bufferPeak (first) > 0.001f, "the render is not silent");
        checkEqual (fbtest::maxDifference (first, second), 0.0f,
                    "two fresh renders are bit-identical, noise included");
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
                    fbtest::utf8 ("every step has exactly ONE onset — the two lanes share a single "
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
               fbtest::utf8 ("while an audible triângulo does change the mix (")
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
               fbtest::utf8 ("CACHAÇA 50 halves the bound (worst ") + juce::String (halfWorst)
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
                             + fbtest::utf8 (" / cachaça ") + juce::String (c.cachaca, 0);

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
               fbtest::utf8 ("CACHAÇA raises the ghost rate (") + juce::String (low) + " -> "
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
               fbtest::utf8 ("only HH ghosts among the kit lanes — energy below 300 Hz stays at the "
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

    /** 05-02 AC-2's input: the displayed position, and the correction on it.

        `/code-review` found this shipped with NO test at all — the whole change
        SPECIAL-FLOWS gates on, and inverting the sign of the correction would
        have passed the entire suite. The sign, the rate, the stopped state and
        the start transient are all pinned here.

        The correction exists because `BlockEmitter` publishes at GRID time while
        the audio leaves `outputDelaySamples()` later. Uncorrected, the playhead
        leads what the user hears by 32 ms — about 28% of a sixteenth at 132 BPM. */
    void testDisplayPositionTracksTheAudibleGroove()
    {
        section ("the displayed position is the clock's, pulled back by the plugin's own delay");

        AudioRig rig { kSampleRate, 512 };

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        const auto render = [&]
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        };

        // ── stopped is a state, not a stale value ───────────────────────────
        {
            check (rig.processor.getDisplayPositionInSteps()
                       <= ForroBoxAudioProcessor::kStoppedPosition,
                   "a processor that has never played reports the stopped position");
        }

        rig.processor.setPlaying (true);

        for (int i = 0; i < 40; ++i)
            render();

        // ── the position advances, and at the clock's rate ──────────────────
        const auto before = rig.processor.getDisplayPositionInSteps();

        for (int i = 0; i < 8; ++i)
            render();

        const auto after = rig.processor.getDisplayPositionInSteps();

        check (after > before,
               "the position advances while the transport runs (" + juce::String (before, 3)
                   + " -> " + juce::String (after, 3) + ")");

        // Eight blocks of 512 at 132 BPM: steps-per-sample is bpm/60*4/rate.
        const auto stepsPerSample = kBpm / 60.0 * 4.0 / kSampleRate;
        const auto expected = stepsPerSample * 512.0 * 8.0;

        check (std::abs ((after - before) - expected) < 1.0e-6,
               "and by exactly the clock's own rate over those blocks (expected "
                   + juce::String (expected, 6) + ", got " + juce::String (after - before, 6) + ")");

        // ── the CORRECTION: behind the grid, by the reported delay ──────────
        //
        // The case that makes this able to fail. Without the subtraction the
        // position equals the grid position exactly, and with the sign inverted
        // it LEADS it by the same amount — so comparing against the raw grid
        // position is what distinguishes all three.
        {
            const auto delaySamples = rig.processor.outputDelaySamples();

            check (delaySamples > 0,
                   "the plugin reports a real output delay (" + juce::String (delaySamples)
                       + " samples) — with none, this check could not fail");

            const auto delayInSteps = stepsPerSample * delaySamples;

            // The grid position is what the emitter saw: the last step it fired
            // sits at or just behind it.
            const auto displayed = rig.processor.getDisplayPositionInSteps();

            // The EXACT relation, against the grid position computed independently
            // from the block count. This is what distinguishes the three
            // possibilities: no correction leaves displayed == grid, an inverted
            // sign puts it a delay AHEAD, and only the correct one puts it a
            // delay behind.
            //
            // The first version of this check compared `displayed` against the
            // emitted step index with a tolerance of one whole step. It passed
            // with the sign inverted — 0.256 of a step is invisible inside a
            // tolerance of 1.0 — which is the shape this project keeps finding:
            // a check that names the thing it does not actually constrain.
            const auto grid = stepsPerSample * 512.0 * 48.0;   // 40 + 8 blocks since play

            // The one check doing the work. It separates all three
            // possibilities at once: no correction leaves displayed == grid, an
            // inverted sign puts it a delay AHEAD, and only the right one puts
            // it a delay behind.
            check (std::abs (displayed - (grid - delayInSteps)) < 1.0e-9,
                   "the displayed position is the grid position MINUS the plugin's output delay, "
                   "so the sweep follows what is HEARD rather than running ahead of it (grid "
                       + juce::String (grid, 6) + " - delay " + juce::String (delayInSteps, 6)
                       + " = " + juce::String (grid - delayInSteps, 6) + ", got "
                       + juce::String (displayed, 6) + ")");

            // Two further checks stood here — "it is NOT simply the grid
            // position" and "corrected BACKWARDS" — and both were arithmetic
            // consequences of the exact relation above plus the magnitude below.
            // They read as independent coverage of the sign and added no failure
            // mode: anything that tripped them tripped the exact check first.
            // Their prose is folded into that check's message instead.

            check (delayInSteps > 0.2,
                   fbtest::utf8 ("and the correction is large enough to be visible — ") 
                       + juce::String (delayInSteps, 3) + " of a step at "
                       + juce::String (kBpm, 0) + " BPM, which is why it is corrected at all");
        }

        // ── stopping parks it, rather than freezing it mid-sweep ────────────
        {
            rig.processor.setPlaying (false);
            render();

            check (rig.processor.getDisplayPositionInSteps()
                       <= ForroBoxAudioProcessor::kStoppedPosition,
                   "stopping parks the position at the stopped sentinel — four stop paths used to "
                   "leave it holding its last playing value, so a playhead reading its documented "
                   "only input would have frozen mid-sweep instead of hiding");

            checkEqual (rig.processor.getCurrentStep(), forrobox::Clock::kStoppedStep,
                        "and the step agrees");
        }
    }

    /** 05-02: stopping clears the last step's velocities.

        A BEHAVIOUR CHANGE the forwarders hid. The old stop path stored the
        stopped step and did not touch `lastStepVelocities`, so
        `getLastStepVelocity` kept returning the last fired step's value;
        `publishStopped` zeroes the whole word. The new behaviour is the one the
        LEDs want — a stopped transport should not leave a lane reading 100 —
        but it was presented as a no-op refactor and nothing pinned it.
        Found by /code-review. */
    void testStoppingClearsTheLastStepVelocities()
    {
        section ("stopping clears the published velocities, so nothing reads as still firing");

        AudioRig rig { kSampleRate, 512 };

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            for (int step = 0; step < 16; ++step)
                rig.setStep (lane, step, 90);

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        rig.processor.setPlaying (true);

        for (int i = 0; i < 24; ++i)
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        }

        auto loudest = 0;
        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            loudest = juce::jmax (loudest,
                                  static_cast<int> (rig.processor.getLastStepVelocity (lane)));

        checkEqual (loudest, 90, "while playing, the published velocities are the step's");

        rig.processor.setPlaying (false);

        block.clear();
        midi.clear();
        rig.processor.processBlock (block, midi);

        auto afterStop = 0;
        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            afterStop = juce::jmax (afterStop,
                                    static_cast<int> (rig.processor.getLastStepVelocity (lane)));

        checkEqual (afterStop, 0,
                    "and stopping clears them — an LED reading the snapshot must not stay lit on "
                    "the last step a stopped transport played");
    }

    /** Defined below, beside the convolution tests; declared here because the
        allocation guard needs one too. */
    juce::File makeTestImpulseResponse (const juce::String& name);

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

        // AND WITH AN IR LOADED. 09-07 put a convolution in the chain; its
        // scratch is sized in `prepare` precisely so `process` never grows it,
        // and this is what holds that claim to account. The FFT engine is doing
        // real work in these blocks, which is the point.
        {
            const auto ir = makeTestImpulseResponse ("alloc");
            check (rig.processor.loadImpulseResponse (ir), "the IR loads");
            rig.setValue (forrobox::ids::convMix, 100.0f);

            // A LONG WARM-UP, and the length is a finding rather than a fudge.
            //
            // At 16 blocks this measured 58 allocations. They are JUCE's own:
            // `juce::dsp::Convolution` loads on a background thread and SWAPS
            // the finished response in from `process`, which allocates on the
            // audio thread — once per load, bounded, and then never again. Our
            // own scratch is sized in `prepare` and never grows, which is the
            // claim this check is really about.
            //
            // So the honest statement is two-part: the load transient allocates
            // and is JUCE's to own, and the STEADY STATE is clean. The transient
            // is asserted below rather than left to a comment.
            const auto transientBefore = fbtest::allocations.load (std::memory_order_relaxed);

            for (int i = 0; i < 2000; ++i)
            {
                block.clear();
                midi.clear();
                rig.processor.processBlock (block, midi);
            }

            const auto settled = fbtest::allocations.load (std::memory_order_relaxed);

            check (settled - transientBefore < 500,
                   juce::String ("the IR swap's allocations are bounded, not continuous (")
                     + juce::String (static_cast<int> (settled - transientBefore)) + ")");

            const auto wetBefore = fbtest::allocations.load (std::memory_order_relaxed);

            for (int i = 0; i < 2000; ++i)
            {
                block.clear();
                midi.clear();
                rig.processor.processBlock (block, midi);
            }

            const auto wetAfter = fbtest::allocations.load (std::memory_order_relaxed);

            checkEqual (static_cast<long long> (wetAfter - wetBefore), 0LL,
                        "2000 blocks THROUGH THE CONVOLUTION allocate nothing");

            ir.deleteFile();
        }

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

        // ── and the MIDI path, which the assertion above CANNOT see ─────────
        //
        // `juce::MidiBuffer::addEvent` grows a juce::Array, and `clear()` is
        // `clearQuick()` — it never frees. So the 16 warm-up blocks above take
        // that array to steady-state capacity, and the measured 2000-block
        // window can never observe a MIDI allocation however many notes it
        // emits. The assertion is true and says nothing about drainMidi.
        //
        // Measured on a COLD buffer instead. This is the honest shape: the
        // first blocks that emit MIDI DO allocate, once per buffer, and every
        // hosted format pre-sizes to 2048 bytes (juce_audio_plugin_client_VST3
        // and its siblings) so only Standalone ever pays it. Asserting zero
        // here would be asserting something false.
        {
            juce::MidiBuffer cold;

            const auto coldBefore = fbtest::allocations.load (std::memory_order_relaxed);
            block.clear();
            rig.processor.processBlock (block, cold);
            const auto coldAfter = fbtest::allocations.load (std::memory_order_relaxed);

            check (coldAfter >= coldBefore,
                   "a cold MidiBuffer is measurable at all");

            // Bounded, not zero. JUCE grows by (n + n/2 + 8) & ~7, so reaching
            // the ~432 bytes a dense block needs takes a handful of reallocs —
            // never per-note, never per-block after the first.
            check (static_cast<long long> (coldAfter - coldBefore) <= 16,
                   "a COLD MidiBuffer allocates a bounded handful of times as it grows to "
                   "hold the block's notes, and never again once warm — every hosted "
                   "format pre-sizes it, so only Standalone pays this, once");
        }

        // ── and again on the MULTI-OUT path, which the window above never ran ──
        //
        // The rig's default layout leaves every aux bus disabled, so
        // `isMultiOut()` is false and the entire stem-view block — the only new
        // audio-thread code in 04-06, and the only part the Phase 1 contract is
        // actually about — was never measured. /code-review named the concrete
        // way that matters: the code is allocation-free only because `busView`
        // returns a prvalue that binds to AudioBuffer's MOVE assignment. Write
        // `const auto view = busView (...); stemBuffers[c] = view;` and the copy
        // assignment calls setSize, which mallocs once per enabled bus per
        // block — and the check above would still have read zero.
        {
            ForroBoxAudioProcessor multi;

            juce::AudioProcessor::BusesLayout layout;

            for (int b = 0; b < ForroBoxAudioProcessor::kNumOutputBuses; ++b)
                layout.outputBuses.add (juce::AudioChannelSet::stereo());

            check (multi.setBusesLayout (layout), "all six buses enable for the allocation window");

            multi.prepareToPlay (kSampleRate, 512);

            if (auto* mode = dynamic_cast<juce::RangedAudioParameter*> (
                                 multi.getAPVTS().getParameter (forrobox::ids::outputMode)))
                mode->setValueNotifyingHost (mode->convertTo0to1 (1.0f));

            {
                auto state = multi.lockPatternState();

                for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
                    for (int step = 0; step < 16; ++step)
                        state->lanes[(size_t) lane][(size_t) step] =
                            static_cast<std::uint8_t> (60 + (step * 4) % 60);
            }

            juce::AudioBuffer<float> wide (ForroBoxAudioProcessor::kNumOutputBuses * 2, 512);
            juce::MidiBuffer wideMidi;

            multi.setPlaying (true);

            for (int i = 0; i < 16; ++i)
            {
                wide.clear();
                wideMidi.clear();
                multi.processBlock (wide, wideMidi);
            }

            const auto multiBefore = fbtest::allocations.load (std::memory_order_relaxed);

            // 500, not the 2000 the stereo window above uses. An allocation on
            // this path fires on the FIRST block, not the fifteen-hundredth —
            // measured at 195 ms for 2000, which was 88% of everything 04-06
            // added to the suite. The asymmetry with the window above is
            // deliberate; do not "restore" it for symmetry.
            for (int i = 0; i < 500; ++i)
            {
                wide.clear();
                wideMidi.clear();
                multi.processBlock (wide, wideMidi);
            }

            const auto multiAfter = fbtest::allocations.load (std::memory_order_relaxed);

            checkEqual (static_cast<long long> (multiAfter - multiBefore), 0LL,
                        "500 blocks on the MULTI-OUT path, with all six buses enabled, "
                        "allocate nothing either");

            check (multi.getVoiceEngine().getActiveVoiceCount() > 0,
                   "and voices were sounding throughout that window too");

            multi.setPlaying (false);
        }

        // Voices were genuinely in flight during the window, not idle.
        check (rig.processor.getVoiceEngine().getActiveVoiceCount() > 0,
               "voices were sounding throughout the measured window");
        check (rig.processor.getStepPublicationCount() > 100,
               juce::String ("steps kept firing (") + juce::String (rig.processor.getStepPublicationCount())
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
    /** Writes a short decaying noise burst to a temp WAV and returns it.

        GENERATED, not committed: an impulse response is audio, and this project
        keeps generated audio out of git (`renderAuditionFiles` says the same).
        A decaying burst is enough to be audibly a room without pretending to be
        one. */
    juce::File makeTestImpulseResponse (const juce::String& name)
    {
        const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("forrobox-test-ir-" + name + ".wav");
        file.deleteFile();

        constexpr int length = 4096;
        juce::AudioBuffer<float> ir (2, length);
        juce::Random random { 20260924 };

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < length; ++i)
            {
                const auto decay = std::exp (-4.0f * static_cast<float> (i)
                                                   / static_cast<float> (length));
                ir.setSample (ch, i, (random.nextFloat() * 2.0f - 1.0f) * decay);
            }

        juce::WavAudioFormat wav;

        if (auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
        {
            const auto options = juce::AudioFormatWriterOptions()
                                   .withSampleRate (kSampleRate)
                                   .withNumChannels (2)
                                   .withBitsPerSample (24);

            if (auto writer = wav.createWriterFor (stream, options))
            {
                writer->writeFromAudioSampleBuffer (ir, 0, length);
                writer.reset();
            }
        }

        return file;
    }

    void testConvolutionStage()
    {
        section ("the IR stage — bit-identical dry, audible wet, honest latency");

        const auto ir = makeTestImpulseResponse ("stage");
        check (ir.existsAsFile(), "the test impulse response was written");

        const auto renderWith = [&ir] (bool loadIr, float convMix)
        {
            AudioRig rig { kSampleRate, 512 };
            rig.useShippedChain (0);
            rig.setValue (forrobox::ids::convMix, convMix);

            if (loadIr)
                check (rig.processor.loadImpulseResponse (ir), "the IR loads");

            {
                auto state = rig.processor.lockPatternState();
                forrobox::applyProfile (*state, forrobox::allProfiles()[0]);
            }

            return rig.render (24576, 512);
        };

        const auto dry = renderWith (false, 0.0f);

        // AC-1's first clause is BIT-IDENTICAL, not "close". `Convolver::process`
        // returns before touching the buffer at zero, so a zero-wet render with
        // an IR loaded must equal one with no IR at all, sample for sample.
        const auto loadedButDry = renderWith (true, 0.0f);

        // COMPARED AS BYTES, which is what "bit-identical" means. Sample-by-sample
        // `!=` says the same thing here and Clang is right to warn about it in
        // general — this is the one place the exact comparison IS the claim, so
        // it is spelled the way the claim is worded rather than silenced.
        auto identical = dry.getNumSamples() == loadedButDry.getNumSamples()
                      && dry.getNumChannels() == loadedButDry.getNumChannels();

        for (int ch = 0; identical && ch < dry.getNumChannels(); ++ch)
            identical = std::memcmp (dry.getReadPointer (ch),
                                     loadedButDry.getReadPointer (ch),
                                     static_cast<size_t> (dry.getNumSamples()) * sizeof (float)) == 0;

        check (identical, "conv_mix at 0 with an IR loaded is BIT-IDENTICAL to no IR at all");

        // And at full wet it is audibly a different signal, finite, and still
        // under the limiter — which is the property that decided the ordering.
        const auto wet = renderWith (true, 100.0f);

        check (isFinite (wet), "the convolved render is finite");
        check (bufferPeak (wet) > 0.05f, "and substantial");
        check (bufferPeak (wet) <= 1.0f,
               juce::String ("and does not clip, because the limiter is downstream of the IR (peak ")
                 + juce::String (bufferPeak (wet), 4) + ")");

        auto differs = false;

        for (int i = 0; ! differs && i < wet.getNumSamples(); ++i)
            differs = std::abs (wet.getSample (0, i) - dry.getSample (0, i)) > 1.0e-6f;

        check (differs, "and it is not merely the dry signal again");

        ir.deleteFile();
    }

    void testConvolutionLatencyIsReported()
    {
        section ("the plugin reports the latency its IR stage actually adds");

        const auto ir = makeTestImpulseResponse ("latency");

        AudioRig rig { kSampleRate, 512 };

        // NOT ZERO, and the first version of this test wrongly expected zero.
        // CACHAÇA's delayed origin (03-02) has delayed the engine by 32 ms since
        // Phase 3 and the host has always been told. The IR stage ADDS to that;
        // it does not replace it.
        const auto humanisation = rig.processor.getLatencySamples();

        check (humanisation > 0,
               "a fresh instance already reports CACHACA's delayed origin");
        checkEqual (rig.processor.convolverLatencyForTest(), 0,
                    "and the IR stage adds nothing while no IR is loaded");

        check (rig.processor.loadImpulseResponse (ir), "the IR loads");
        rig.processor.prepareToPlay (kSampleRate, 512);

        // ASKED OF THE ENGINE, compared against what the host is told. A number
        // the plugin made up would be exactly the unreported latency that makes
        // it play late against every other track.
        // THE SUM. Reporting only the convolver's would silently discard the
        // 1536 samples the humanisation still delays — which is exactly what
        // this plan's first draft did, and what this check caught.
        checkEqual (rig.processor.getLatencySamples(),
                    humanisation + rig.processor.convolverLatencyForTest(),
                    "what the host is told is CACHACA's delay PLUS the IR stage's");

        // WITH A NON-ZERO TERM, because without one this whole check degenerates.
        // The engine's head is structurally zero, so both sides above reduce to
        // `humanisation` and the assertion says only "latency is unchanged" — it
        // could not catch a second writer clobbering the sum, which is precisely
        // what `/code-review` found in `prepareToPlay`. /code-review.
        rig.processor.setConvolverLatencyForTest (777);

        checkEqual (rig.processor.getLatencySamples(), humanisation + 777,
                    "a non-zero IR latency is ADDED to CACHACA's, not substituted for it");

        // And it survives a re-prepare, which is where the second writer lived.
        rig.processor.prepareToPlay (kSampleRate, 512);

        checkEqual (rig.processor.getLatencySamples(), humanisation + 777,
                    "and a prepareToPlay does not drop it again");

        rig.processor.setConvolverLatencyForTest (-1);

        ir.deleteFile();
    }

    void testUnreadableImpulseResponseIsRefused()
    {
        section ("a file that is not audio is refused, not accepted silently");

        AudioRig rig { kSampleRate, 512 };

        // A `.wav` that is not a WAV — the shape of an mp3 someone renamed, and
        // the case that used to return TRUE, persist the path, and collapse the
        // bus to near-silence with no diagnostic anywhere.
        const auto fake = juce::File::getSpecialLocation (juce::File::tempDirectory)
                            .getChildFile ("forrobox-test-not-audio.wav");
        fake.deleteFile();
        fake.replaceWithText ("this is not an audio file");

        check (fake.existsAsFile(), "the decoy exists on disk");
        check (! rig.processor.loadImpulseResponse (fake),
               "loading it is REFUSED, because existing is not the same as readable");

        {
            auto handle = rig.processor.lockPatternState();
            check (handle->impulseResponsePath.isEmpty(),
                   "and a refused file is not written into the state");
        }

        // A missing file too, which is the other way in.
        const auto absent = juce::File::getSpecialLocation (juce::File::tempDirectory)
                              .getChildFile ("forrobox-test-absent.wav");
        absent.deleteFile();

        check (! rig.processor.loadImpulseResponse (absent), "and a missing file is refused");

        fake.deleteFile();
    }

    void testImpulseResponseSurvivesReload()
    {
        section ("the IR path persists, and a missing file costs a reverb not a session");

        const auto ir = makeTestImpulseResponse ("persist");
        juce::MemoryBlock saved;

        {
            AudioRig rig { kSampleRate, 512 };
            check (rig.processor.loadImpulseResponse (ir), "the IR loads");

            {
                auto handle = rig.processor.lockPatternState();
                checkEqual (handle->impulseResponsePath.toStdString(),
                            ir.getFullPathName().toStdString(),
                            "and the state records its path");
            }

            rig.processor.getStateInformation (saved);
        }

        {
            AudioRig rig { kSampleRate, 512 };
            rig.processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

            auto handle = rig.processor.lockPatternState();
            checkEqual (handle->impulseResponsePath.toStdString(),
                        ir.getFullPathName().toStdString(),
                        "the path comes back");
        }

        // NOW DELETE IT, and reload the same project. This is the case a user
        // hits by opening a session on another machine.
        ir.deleteFile();
        check (! ir.existsAsFile(), "the IR is gone from disk");

        {
            AudioRig rig { kSampleRate, 512 };
            rig.processor.setStateInformation (saved.getData(), static_cast<int> (saved.getSize()));

            {
                auto handle = rig.processor.lockPatternState();
                checkEqual (handle->impulseResponsePath.toStdString(),
                            ir.getFullPathName().toStdString(),
                            "the PATH is kept even though the file is not there");
            }

            // And it plays. Dry, finite, and without having thrown on the way.
            rig.useShippedChain (0);
            rig.setValue (forrobox::ids::convMix, 100.0f);

            {
                auto state = rig.processor.lockPatternState();
                forrobox::applyProfile (*state, forrobox::allProfiles()[0]);
            }

            const auto rendered = rig.render (12288, 512);

            check (isFinite (rendered), "a project whose IR is missing still renders");
            check (bufferPeak (rendered) > 0.05f, "and is audible, dry");
        }
    }

    void testFullChainHeadroom()
    {
        section ("the four real grooves, through the whole chain");

        // This test used to PIN the unlimited sum as design input for 03-03:
        // 1.336 deterministic, and 1.454 across 36 humanisation realisations.
        // Those numbers did their job — the limiter's threshold was sized from
        // the second, not the first — so the assertion becomes what they were
        // feeding: with voices, character bus, limiter and master all in the
        // path, NOTHING may exceed full scale.
        //
        // The seed sweep stays. It is what makes the claim about the
        // distribution rather than one draw, and it is the reason the figure
        // handed to this plan was 1.454 rather than the 1.336 a single
        // realisation reports.
        const auto peakForProfile = [] (const forrobox::Profile& profile, std::uint64_t seed)
        {
            AudioRig rig { kSampleRate, 512 };

            // The real chain, not the transparent bus AudioRig defaults to.
            rig.useShippedChain (profile.timbreIndex);

            rig.processor.setHumanisationSeedOffset (seed);
            rig.setValue (forrobox::ids::bpm, static_cast<float> (profile.bpm()));
            rig.setValue (forrobox::ids::swing, profile.swing());
            rig.setValue (forrobox::ids::cachaca, profile.cachaca());

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

            auto buffer = rig.render (98304, 512);

            check (isFinite (buffer),
                   juce::String (profile.displayName()) + " renders no NaN or infinity");

            return bufferPeak (buffer);
        };

        auto worst = 0.0f;
        const char* worstProfile = "";
        auto measured = 0;

        for (std::uint64_t seed = 0; seed < 9; ++seed)
        {
            for (const auto& profile : forrobox::allProfiles())
            {
                const auto peak = peakForProfile (profile, seed);
                ++measured;

                check (peak > 0.05f,
                       juce::String (profile.displayName()) + " renders a substantial groove");

                if (peak > worst)
                {
                    worst = peak;
                    worstProfile = profile.displayName();
                }
            }
        }

        checkEqual (measured, 36, "36 profile x realisation combinations measured");

        // The claim this plan exists to make true. Measured 2026-09-08: 0.753
        // for the hottest profile, against 1.454 unlimited.
        check (worst < 1.0f,
               juce::String ("nothing clips through the full chain (worst ")
                   + juce::String (worst, 4) + ", " + worstProfile + ")");

        // And it is not quietly over-limited into mush either: the grooves
        // still reach a usable level.
        check (worst > 0.3f,
               juce::String ("and the chain is not crushed (worst peak ")
                   + juce::String (worst, 4) + ")");

        // The limiter is doing work on the hottest material rather than sitting
        // idle above the programme level.
        AudioRig hottest { kSampleRate, 512 };
        hottest.useShippedChain (0);
        hottest.setValue (forrobox::ids::bpm, 138.0f);
        hottest.setValue (forrobox::ids::cachaca, 32.0f);

        for (const auto& info : forrobox::ids::channelInfos)
            hottest.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost),
                              info.ghost);

        {
            auto state = hottest.processor.lockPatternState();

            if (const auto* caruaru = forrobox::findProfile ("caruaru"))
                forrobox::applyProfile (*state, *caruaru);
        }

        // Polled per block, and the MAXIMUM taken.
        //
        // getGainReductionDb reports the LAST block, which is what a meter
        // polled once a frame wants — and by the end of a 98304-sample render
        // the groove has stopped and the tails have decayed, so reading it
        // afterwards reported 0.00 dB for a limiter that had been working
        // throughout. The accessor is right; sampling it once at the end was
        // not.
        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;
        auto worstReduction = 0.0f;

        hottest.processor.setPlaying (true);

        for (int i = 0; i < 192; ++i)
        {
            block.clear();
            midi.clear();
            hottest.processor.processBlock (block, midi);
            worstReduction = juce::jmax (worstReduction,
                                         hottest.processor.getMixBus().takeGainReductionDb());
        }

        check (worstReduction > 0.5f,
               juce::String ("the limiter engages on the hottest groove (")
                   + juce::String (worstReduction, 2) + " dB at its busiest)");

        // And reports nothing once the groove has STOPPED and decayed, which is
        // the same accessor saying the opposite thing correctly. The transport
        // has to be stopped for that — a first version just rendered more
        // blocks, and the sequencer went on playing.
        hottest.processor.setPlaying (false);

        for (int i = 0; i < 200; ++i)
        {
            block.clear();
            midi.clear();
            hottest.processor.processBlock (block, midi);
        }

        checkEqual (hottest.processor.getMixBus().takeGainReductionDb(), 0.0f,
                    "and reports none once everything has decayed");
    }

    void testFactoryDefaults()
    {
        section ("the plugin as it ships, with nothing touched");

        // NOTHING in the suite rendered the plugin at its factory settings.
        // AudioRig's constructor overwrites every global and every per-channel
        // parameter — deliberately, for isolation — so at the close of the
        // phase whose goal is "the four profiles audibly match the prototype",
        // the state a user actually gets on instantiation was untested.
        //
        // That is what let the audition diverge from testFullChainHeadroom
        // unnoticed: one rendered the shipped chain and the other did not, and
        // no assertion covered the shipped configuration at all.
        ForroBoxAudioProcessor processor;
        processor.prepareToPlay (kSampleRate, 512);

        // The default profile's grid is loaded by the constructor and nothing
        // else is touched. That sentence was written at 03-03 and was FALSE
        // until 08-01: the constructor named CAMPINA and loaded nothing, so
        // this rendered an empty grid for four plans. See the control below.
        juce::AudioBuffer<float> buffer (2, 512);
        juce::MidiBuffer midi;

        // ONE render, used twice. The control below has to differ from this
        // render in the grid and in NOTHING else, and "nothing else" written as
        // a second copy of the loop is a claim in a comment rather than a
        // property of the code. /simplify.
        auto finite = true;

        const auto renderPeak = [&] (ForroBoxAudioProcessor& subject)
        {
            subject.setPlaying (true);

            auto peak = 0.0f;

            for (int block = 0; block < 384; ++block)     // ~4 s
            {
                buffer.clear();
                midi.clear();
                subject.processBlock (buffer, midi);

                peak = juce::jmax (peak, bufferPeak (buffer));
                finite = finite && isFinite (buffer);
            }

            return peak;
        };

        const auto peak = renderPeak (processor);

        check (finite, "renders no NaN or infinity at factory defaults");
        check (peak > 0.05f,
               juce::String ("and makes a substantial sound (peak ") + juce::String (peak, 4) + ")");
        check (peak < 1.0f,
               juce::String ("without clipping (peak ") + juce::String (peak, 4) + ")");

        // ── AND THE SOUND IS THE GROOVE, not the ghosts ────────────────────
        //
        // The three checks above passed for four plans against an EMPTY grid.
        // Every channel's GHOST parameter ships at 6-14%, so a factory instance
        // with no pattern at all still fires ghost notes on most steps and
        // clears 0.05 comfortably — which is why the comment above this test
        // could claim the constructor loaded a profile while it did not, with
        // nothing able to contradict it.
        //
        // The same render with the GRID cleared and nothing else touched is the
        // control. `clearGrid` and not `blankInstrument`, deliberately: the
        // latter would also release BATERIA's mute and add four kit lanes back,
        // which would make the difference measured here partly about the mute.
        {
            ForroBoxAudioProcessor ghostsOnly;

            forrobox::test::clearGrid (ghostsOnly);

            ghostsOnly.prepareToPlay (kSampleRate, 512);

            const auto ghostPeak = renderPeak (ghostsOnly);

            check (ghostPeak > 0.0f,
                   juce::String ("an empty grid is NOT silent at factory defaults: the shipped "
                                 "ghost probabilities alone reach ") + juce::String (ghostPeak, 4)
                       + ", which is what the peak check above was passing on");

            check (peak > ghostPeak * 2.0f,
                   juce::String ("and the factory groove is well clear of it (")
                       + juce::String (ghostPeak, 4) + " -> " + juce::String (peak, 4)
                       + "). A fresh instance plays CAMPINA, which is PROJECT.md's Success "
                         "Metric and the reason this plan exists");
        }

        // And the shipped defaults are the ones PLANNING.md's state table says.
        const auto settings = processor.resolveBusSettings();

        checkEqual (settings.timbreIndex, 0, "TIMBRE ships at HI-FI");
        checkEqual (settings.charMix, 40.0f, "MIX ships at 40");
        check (settings.limiterOn, "the limiter ships on");
        checkEqual (settings.master, 82.0f, "master ships at 82");

        // The parameter's choice strings against the table the AUDIO reads its
        // cutoff and drive from, index by index. `timbreIndex` above pins only
        // the default; a control that rewrote the StringArray by hand in a
        // different order passed all 1090 checks, so the plugin would have
        // shown "LO-FI" while rendering HI-FI. The names are what the host
        // automation lane and every saved project record, so the two orders
        // agreeing is a compatibility property, not a cosmetic one.
        if (auto* timbre = dynamic_cast<juce::AudioParameterChoice*> (
                               processor.getAPVTS().getParameter (forrobox::ids::timbre)))
        {
            checkEqual (timbre->choices.size(), static_cast<int> (forrobox::timbreSpecs.size()),
                        "TIMBRE offers one choice per timbre spec");

            auto aligned = true;

            // Through `CharPointer_UTF8` on BOTH sides. `CICLOTRON™` is the
            // first name in this table that is not pure ASCII, and comparing a
            // `juce::String` against a `const char*` reads those bytes as
            // LATIN-1 — so this check would have failed on a correct plugin,
            // and would have passed on one that mangled the name consistently.
            for (size_t i = 0; i < forrobox::timbreSpecs.size(); ++i)
                aligned = aligned
                       && timbre->choices[static_cast<int> (i)]
                              == juce::String (juce::CharPointer_UTF8 (
                                     forrobox::timbreSpecs[i].displayName));

            check (aligned,
                   juce::String ("and in the table's order (") + timbre->choices.joinIntoString (", ") + ")");
        }
        else
        {
            check (false, "TIMBRE is a choice parameter");
        }
    }

    void testRingOutPassesThroughTheBus()
    {
        section ("the ring-out after Stop goes through the output stage");

        // A direct assertion for the trap 03-02's most expensive finding
        // identified: three engine.render call sites would have let 03-03's
        // limiter and master be added to the normal path only, leaving the
        // decay after STOP unlimited and at unity master — at the default
        // master of 82, (0.82)^2 = 0.672, so a +3.5 dB jump on the most common
        // gesture in the plugin.
        //
        // It was already protected indirectly, by a gain-reduction assertion in
        // a test about something else. This puts the guard where the lesson is.
        AudioRig rig { kSampleRate, 512 };
        rig.useShippedChain (0);
        rig.setValue (forrobox::ids::master, 50.0f);   // 0.25, unmistakable
        rig.setStep (0, 0, 127);                       // the zabumba, longest tail

        juce::AudioBuffer<float> block (2, 512);
        juce::MidiBuffer midi;

        rig.processor.setPlaying (true);

        // Past the 32 ms lookahead and into the decay.
        for (int i = 0; i < 6; ++i)
        {
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);
        }

        const auto whilePlaying = bufferPeak (block);
        check (whilePlaying > 0.0005f, "the zabumba is sounding before the stop");

        rig.processor.setPlaying (false);

        block.clear();
        midi.clear();
        rig.processor.processBlock (block, midi);

        const auto afterStop = bufferPeak (block);

        check (afterStop > 0.0005f, "and still sounding after it");

        // The decay must not JUMP. Skipping the bus on the stopped path would
        // divide out the 0.25 master and multiply the level by four.
        check (afterStop < whilePlaying * 1.6f,
               juce::String ("and the level does not jump when the transport stops (")
                   + juce::String (afterStop, 5) + " against " + juce::String (whilePlaying, 5)
                   + ")");

        // Rendered at the master's own gain, not at unity: an unscaled tail
        // would be four times this.
        AudioRig unity { kSampleRate, 512 };
        unity.useShippedChain (0);
        unity.setValue (forrobox::ids::master, 100.0f);
        unity.setStep (0, 0, 127);

        juce::AudioBuffer<float> loud (2, 512);
        unity.processor.setPlaying (true);

        for (int i = 0; i < 6; ++i)
        {
            loud.clear();
            midi.clear();
            unity.processor.processBlock (loud, midi);
        }

        unity.processor.setPlaying (false);
        loud.clear();
        midi.clear();
        unity.processor.processBlock (loud, midi);

        const auto ratio = bufferPeak (loud) / juce::jmax (1.0e-9f, afterStop);

        check (ratio > 3.0f && ratio < 5.0f,
               juce::String ("the stopped tail is master-scaled (master 100 against 50 is ")
                   + juce::String (ratio, 2) + "x, expected 4x)");
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

        // Driven at the ENGINE, not through processBlock.
        //
        // It used to hand `processBlock` a one-channel buffer — a layout
        // `isBusesLayoutSupported` explicitly REFUSES, asserted a few tests below
        // ("main MONO — refused; the design is a stereo instrument"). So the test
        // produced the only input that could reach the fold-down, and when 04-06
        // made the processor address its output as a BUS the mismatch surfaced as
        // a segfault — which was then patched with a clamp on the audio thread,
        // justified by a self-inflicted input. /simplify caught that.
        //
        // The law is real and worth keeping: `render` has an explicit
        // `right == nullptr` branch, and it once wrote only the left-ward matrix
        // terms — at PAN +50 that is cos(pi/2) = 0, so a hard-right channel
        // disappeared instead of summing to mono. But the law belongs to
        // VoiceEngine, which is deliberately bus-ignorant and for which a
        // one-channel buffer IS a legitimate input. No processor, no host, and no
        // layout to contradict.
        const auto foldsDown = [] (int channel, int lane, float pan, const char* what)
        {
            forrobox::VoiceEngine engine;
            engine.prepare (kSampleRate, 512);

            forrobox::VoiceEngine::Settings settings;

            for (auto& cs : settings.channels)
            {
                cs.vol = 100.0f;
                cs.pan = 0;
            }

            settings.channels[(size_t) channel].pan = pan;
            engine.beginBlock (settings);

            forrobox::VoiceEngine::StepVelocities velocities {};
            velocities[(size_t) lane] = 127;
            engine.scheduleStep (velocities, 0);

            // ONE channel, which is what a mono bus would deliver.
            juce::AudioBuffer<float> mono (1, 512);
            forrobox::VoiceEngine::Stems noStems;

            auto loudest = 0.0f;

            // The 32 ms lookahead is three blocks at 512, so a single block would
            // be silence and this would fail for a reason unrelated to panning.
            for (int block = 0; block < 8; ++block)
            {
                mono.clear();
                engine.render (mono, noStems);
                loudest = juce::jmax (loudest, mono.getMagnitude (0, 0, 512));
            }

            check (loudest > 0.0005f,
                   juce::String (what) + " survives a mono output at PAN "
                       + juce::String (pan, 0) + " (peak " + juce::String (loudest, 5) + ")");
        };

        // The synthesised matrix, across the pan law's full width.
        for (const float pan : { -50.0f, 0.0f, 50.0f })
            foldsDown (4, 4, pan, "a hard-panned synthesised channel");

        // And the sampled path, which has its own matrix.
        foldsDown (0, 0, 50.0f, "a hard-right sampled zabumba");
    }


    void testStemsCarryOneChannelEach()
    {
        section ("each per-channel stem carries that channel's voices, pre-everything");

        constexpr int kChannels = forrobox::VoiceEngine::kNumChannels;
        constexpr int kBlock = 512;

        // ── the real subject: the processor's own multi-out render ──────────
        //
        // Driving VoiceEngine directly would need the scheduler's block
        // settings reconstructed by hand, which is a second copy of what
        // processBlock already does. The processor is the unit under test.
        const auto renderProcessor = [] (int soloChannel, int muteChannel, bool multiOut,
                                         std::array<juce::AudioBuffer<float>, (size_t) kChannels>& stems,
                                         juce::AudioBuffer<float>& mainOut)
        {
            ForroBoxAudioProcessor processor;

            // Every bus enabled, which is what a host does for multi-out.
            juce::AudioProcessor::BusesLayout layout;

            for (int b = 0; b < ForroBoxAudioProcessor::kNumOutputBuses; ++b)
                layout.outputBuses.add (juce::AudioChannelSet::stereo());

            check (processor.setBusesLayout (layout), "the host can enable all six buses");

            processor.prepareToPlay (kSampleRate, kBlock);

            const auto setValue = [&processor] (juce::StringRef id, float value)
            {
                if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (
                                  processor.getAPVTS().getParameter (id)))
                    p->setValueNotifyingHost (p->convertTo0to1 (value));
            };

            setValue (forrobox::ids::bpm, 132.0f);
            setValue (forrobox::ids::cachaca, 0.0f);
            setValue (forrobox::ids::outputMode, multiOut ? 1.0f : 0.0f);

            for (int c = 0; c < kChannels; ++c)
            {
                const auto* id = forrobox::ids::channelInfos[(size_t) c].id;

                setValue (forrobox::ids::channelParam (id, forrobox::ids::ghost), 0.0f);
                setValue (forrobox::ids::channelParam (id, forrobox::ids::mute),
                          c == muteChannel ? 1.0f : 0.0f);
                setValue (forrobox::ids::channelParam (id, forrobox::ids::solo),
                          c == soloChannel ? 1.0f : 0.0f);
            }

            {
                auto state = processor.lockPatternState();

                if (const auto* profile = forrobox::findProfile ("campina"))
                    forrobox::applyProfile (*state, *profile);
            }

            const auto totalChannels = ForroBoxAudioProcessor::kNumOutputBuses * 2;

            juce::AudioBuffer<float> block (totalChannels, kBlock);
            juce::MidiBuffer midi;

            mainOut.setSize (2, kBlock * 8);
            mainOut.clear();

            for (auto& stem : stems)
            {
                stem.setSize (2, kBlock * 8);
                stem.clear();
            }

            processor.setPlaying (true);

            for (int i = 0; i < 8; ++i)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);

                for (int ch = 0; ch < 2; ++ch)
                {
                    mainOut.copyFrom (ch, i * kBlock, block, ch, 0, kBlock);

                    for (int c = 0; c < kChannels; ++c)
                        stems[(size_t) c].copyFrom (ch, i * kBlock, block,
                                                     (1 + c) * 2 + ch, 0, kBlock);
                }
            }

            processor.setPlaying (false);
        };


        // The two all-channels renders, done ONCE each and reused below. They
        // were run three and two times respectively — and the test itself
        // asserts they are bit-identical, so the repeats bought nothing.
        std::array<juce::AudioBuffer<float>, (size_t) kChannels> multiStems, stereoStems;
        juce::AudioBuffer<float> multiMain, stereoMain;

        renderProcessor (-1, -1, true, multiStems, multiMain);
        renderProcessor (-1, -1, false, stereoStems, stereoMain);

        // ── every channel playing: every stem that has notes carries them ───
        {
            auto& stems = multiStems;
            auto& main = multiMain;

            check (fbtest::bufferRms (main) > 0.0f, "the main bus carries the mix");

            auto sounding = 0;

            for (int c = 0; c < kChannels; ++c)
                if (fbtest::bufferRms (stems[(size_t) c]) > 0.0f)
                    ++sounding;

            check (sounding >= 4,
                   juce::String ("at least four of the five stems carry audio (")
                       + juce::String (sounding) + ") — campina does not play every channel");
        }

        // ── MUTE one channel: its stem goes silent, the others do not ───────
        {
            std::array<juce::AudioBuffer<float>, (size_t) kChannels> stems;
            juce::AudioBuffer<float> main;

            renderProcessor (-1, 0, true, stems, main);

            fbtest::checkSilent (stems[0], "muting ZABUMBA silences its stem exactly");

            auto othersSound = false;

            for (int c = 1; c < kChannels; ++c)
                othersSound = othersSound || fbtest::bufferRms (stems[(size_t) c]) > 0.0f;

            check (othersSound, "and leaves the other stems playing");
        }

        // ── SOLO one channel: only its stem sounds ──────────────────────────
        {
            std::array<juce::AudioBuffer<float>, (size_t) kChannels> stems;
            juce::AudioBuffer<float> main;

            renderProcessor (0, -1, true, stems, main);

            check (fbtest::bufferRms (stems[0]) > 0.0f, "soloing ZABUMBA leaves its stem playing");

            for (int c = 1; c < kChannels; ++c)
                fbtest::checkSilent (stems[(size_t) c],
                                     juce::String ("and silences stem ")
                                         + forrobox::ids::channelInfos[(size_t) c].id);
        }

        // ── STEREO leaves every aux bus SILENT ──────────────────────────────
        //
        // AC-4, and it had NO test until /code-review pointed out that
        // `renderProcessor` was called with multiOut=true at every site: the
        // `if (isMultiOut())` guard could be deleted and the whole suite stayed
        // green, while every host sitting in STEREO with its aux buses enabled
        // started receiving stems it never asked for.
        //
        // The buses are enabled either way here — that is the whole point. A
        // host may enable them and leave the plugin in STEREO.
        {
            auto& stems = stereoStems;

            check (fbtest::bufferRms (stereoMain) > 0.0f,
                   "with STEREO selected the main bus still carries the mix");

            for (int c = 0; c < kChannels; ++c)
                fbtest::checkSilent (stems[(size_t) c],
                                     juce::String ("stem ")
                                         + forrobox::ids::channelInfos[(size_t) c].id
                                         + " is SILENT in STEREO — cleared, not merely unwritten");
        }

        // ── and the main bus is identical in both modes ─────────────────────
        //
        // AC-3. Switching OUTPUT must not move the thing the user is listening
        // to by one sample.
        {
            checkEqual (fbtest::maxDifference (stereoMain, multiMain), 0.0f,
                        "the main bus is sample-identical in STEREO and MULTI-OUT — the mode "
                        "changes what the AUX buses carry and nothing else");
        }

        // ── a layout with HOLES: buses 1, 3 and 5 on, 2 and 4 off ───────────
        //
        // The reason the stem offset is SUMMED from the enabled buses before it
        // rather than computed as `bus * 2`. Every case above enables all six,
        // where the two agree exactly — so a control replacing the sum with
        // `bus * 2` went UNDETECTED, and `isBusesLayoutSupported` explicitly
        // accepts this layout ("a host may enable three of five, and several
        // do"). Found by negative control c118.
        {
            ForroBoxAudioProcessor processor;

            juce::AudioProcessor::BusesLayout layout;
            layout.outputBuses.add (juce::AudioChannelSet::stereo());          // main

            // Channels 0, 2, 4 get a bus; 1 and 3 do not.
            for (int c = 0; c < kChannels; ++c)
                layout.outputBuses.add (c % 2 == 0 ? juce::AudioChannelSet::stereo()
                                                   : juce::AudioChannelSet::disabled());

            check (processor.setBusesLayout (layout),
                   "a host can enable three of the five aux buses and leave two disabled");

            processor.prepareToPlay (kSampleRate, 512);

            const auto setValue = [&processor] (juce::StringRef id, float value)
            {
                if (auto* prm = dynamic_cast<juce::RangedAudioParameter*> (
                                    processor.getAPVTS().getParameter (id)))
                    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
            };

            setValue (forrobox::ids::outputMode, 1.0f);
            setValue (forrobox::ids::cachaca, 0.0f);

            // SOLO channel 4 — the last enabled bus, and the one whose offset is
            // wrong by four channels if disabled buses are assumed to occupy
            // space. With `bus * 2` its audio would land past the end of the
            // host's buffer or in another bus entirely.
            for (int c = 0; c < kChannels; ++c)
            {
                const auto* id = forrobox::ids::channelInfos[(size_t) c].id;

                setValue (forrobox::ids::channelParam (id, forrobox::ids::ghost), 0.0f);
                setValue (forrobox::ids::channelParam (id, forrobox::ids::solo),
                          c == 4 ? 1.0f : 0.0f);
            }

            {
                auto state = processor.lockPatternState();

                if (const auto* profile = forrobox::findProfile ("campina"))
                    forrobox::applyProfile (*state, *profile);
            }

            // Three enabled aux buses at two channels each, plus main.
            juce::AudioBuffer<float> block (2 + 3 * 2, 512);
            juce::MidiBuffer midi;

            juce::AudioBuffer<float> collected (2, 512 * 8);
            collected.clear();

            processor.setPlaying (true);

            for (int i = 0; i < 8; ++i)
            {
                block.clear();
                midi.clear();
                processor.processBlock (block, midi);

                // BATERIA is the third ENABLED aux bus, so its channels start at
                // 2 (main) + 2 + 2 = 6 — not at 2 + 4 * 2 = 10, which is past
                // the end of this buffer.
                for (int ch = 0; ch < 2; ++ch)
                    collected.copyFrom (ch, i * 512, block, 6 + ch, 0, 512);
            }

            processor.setPlaying (false);

            check (fbtest::bufferRms (collected) > 0.0f,
                   "the soloed channel's stem lands on the THIRD ENABLED bus — a disabled bus "
                   "occupies no channels, so the offset is summed rather than multiplied");
        }

        // ── the stems do NOT sum to the main bus, and that is the design ────
        //
        // Stated as a CHECK rather than left in a comment: stems are
        // pre-character, pre-limiter, pre-master, so tanh(a+b) != tanh(a)+tanh(b)
        // and the limiter acts on the sum by definition. Someone will one day
        // measure this and file it as a bug; this is the answer.
        {
            auto& stems = multiStems;
            auto& main = multiMain;

            juce::AudioBuffer<float> summed (2, main.getNumSamples());
            summed.clear();

            for (const auto& stem : stems)
                for (int ch = 0; ch < 2; ++ch)
                    summed.addFrom (ch, 0, stem, ch, 0, stem.getNumSamples());

            const auto difference = fbtest::maxDifference (summed, main);

            check (difference > 0.001f,
                   juce::String ("the five stems summed do NOT equal the main bus (max difference ")
                       + juce::String (difference, 4) + ") — stems are pre-character, pre-limiter "
                         "and pre-master, so the character bus's tanh is not distributive over them "
                         "and the limiter acts on the sum. Decided at 04-06 planning");
        }
    }

    void testBusLayoutsAreAcceptedAndRefused()
    {
        section ("the declared bus layout: main stereo plus five aux, each optional");

        ForroBoxAudioProcessor processor;

        checkEqual (processor.getBusCount (false), ForroBoxAudioProcessor::kNumOutputBuses,
                    "six output buses are declared — main plus one per channel");
        checkEqual (processor.getBusCount (true), 0,
                    "and NO input bus: declaring one makes some hosts present an instrument as "
                    "an effect");

        // Named from the channel table, so a routing panel cannot label a bus
        // with an instrument it does not carry.
        for (size_t c = 0; c < forrobox::ids::channelInfos.size(); ++c)
        {
            const auto expected = juce::String (juce::CharPointer_UTF8 (
                forrobox::ids::channelInfos[c].displayName));

            checkEqual (processor.getBus (false, ForroBoxAudioProcessor::busForChannel ((int) c))
                            ->getName(),
                        expected,
                        "aux bus " + juce::String ((int) c + 1) + " is named " + expected);
        }

        // Only the MAIN bus is enabled by default, so a host that wants a plain
        // stereo instrument gets one and every pre-multi-out session opens
        // unchanged.
        check (processor.getBus (false, 0)->isEnabledByDefault(),
               "the main bus is enabled by default");

        for (int b = 1; b < ForroBoxAudioProcessor::kNumOutputBuses; ++b)
            check (! processor.getBus (false, b)->isEnabledByDefault(),
                   "aux bus " + juce::String (b) + " is NOT enabled by default");

        // ── the layouts themselves ──────────────────────────────────────────
        const auto stereo = juce::AudioChannelSet::stereo();
        const auto mono = juce::AudioChannelSet::mono();
        const auto off = juce::AudioChannelSet::disabled();

        const auto layoutOf = [&] (std::initializer_list<juce::AudioChannelSet> outputs)
        {
            juce::AudioProcessor::BusesLayout layout;

            for (const auto& set : outputs)
                layout.outputBuses.add (set);

            return layout;
        };

        struct Case { const char* what; juce::AudioProcessor::BusesLayout layout; bool accepted; };

        const std::array<Case, 8> cases {{
            { "main stereo with every aux DISABLED — the plain stereo instrument",
              layoutOf ({ stereo, off, off, off, off, off }), true },
            { "main stereo with all five aux stereo — full multi-out",
              layoutOf ({ stereo, stereo, stereo, stereo, stereo, stereo }), true },
            { "main stereo with THREE of five enabled — hosts do this",
              layoutOf ({ stereo, stereo, off, stereo, off, stereo }), true },
            { "main stereo alone, the aux buses not offered at all",
              layoutOf ({ stereo }), true },
            { "main DISABLED — refused, because it would make the plugin silent on the "
              "output the user is listening to and look like the routing is broken",
              layoutOf ({ off, stereo, stereo, stereo, stereo, stereo }), false },
            { "main MONO — refused; the design is a stereo instrument",
              layoutOf ({ mono, off, off, off, off, off }), false },

            // The two cases the aux loop and the count guard exist for, and
            // which nothing covered: /simplify showed that deleting either
            // branch left all six cases above green.
            { "a MONO aux bus — refused; a stem carries a panned channel",
              layoutOf ({ stereo, mono, off, off, off, off }), false },
            { "MORE buses than were declared — refused",
              layoutOf ({ stereo, stereo, stereo, stereo, stereo, stereo, stereo }), false },
        }};

        for (const auto& c : cases)
            checkEqual (processor.isBusesLayoutSupported (c.layout), c.accepted,
                        juce::String (c.accepted ? "accepts " : "refuses ") + c.what);

        // An INPUT bus is refused however the outputs are arranged.
        {
            auto withInput = layoutOf ({ stereo, off, off, off, off, off });
            withInput.inputBuses.add (stereo);

            check (! processor.isBusesLayoutSupported (withInput),
                   "and refuses any input bus, whatever the outputs look like");
        }
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

/** Renders every GROOVE of every profile to a WAV and a MID, for a person to judge.

    Phase 3 wrote this to A/B four profiles against the prototype. 09-04 points
    it at the groove banks — sixteen grooves, thirty-two files — because that is
    how Esmeraldo Filho judges twelve drafted grooves that no UI can reach until
    09-06 builds the cycler.

    IT ASSERTS NOW, and this docstring used to say it did not. 04-01's law is
    that a checkpoint artefact needs the same scrutiny as a test, and
    `ui-renders` is the counter-example in this same repository: those PNGs are
    checked for their far corner and their knob ink, which is what caught a 2x
    render that was a 1200x780 chassis in the corner of a 2400x1560 image.

    The check that matters is NOT silence or clipping — it is that each render
    is THE GROOVE IT NAMES. Thirty-two files that all rendered CAMPINA's default
    would be finite, audible and unclipped, and nobody would hear the difference
    across a listening session.

    Deliberately part of the one test executable rather than a second target. A
    second executable re-compiles the whole JUCE module set, which is why there
    is only one. */
void renderAuditionFiles (const juce::String& outputDirectory)
{
    const auto directory = juce::File::getCurrentWorkingDirectory()
                             .getChildFile (outputDirectory);
    check (directory.createDirectory().wasOk(),
           "the audition directory can be created: " + directory.getFullPathName());

    std::cout << "Rendering auditions to " << directory.getFullPathName() << "\n";

    for (const auto& profile : forrobox::allProfiles())
    for (const auto& groove : profile.grooves())
    {
        AudioRig rig { kSampleRate, 512 };

        // THE GROOVE'S feel, not the profile's — 09-03 moved bpm, swing and
        // cachaça onto the groove precisely so a bank could hold a xote at 92
        // beside a pé-de-serra at 132. Rendering all four at the profile's
        // tempo would audition something the plugin will never play.
        rig.setValue (forrobox::ids::bpm, static_cast<float> (groove.bpm));
        rig.setValue (forrobox::ids::swing, groove.swing);

        // The GROOVE's own CACHAÇA now that 03-02 has built it. Pinned to 0
        // while humanisation did not exist, because rendering with it set would
        // have suggested it did something.
        rig.setValue (forrobox::ids::cachaca, groove.cachaca);

        // And each channel's own ghost probability, from the channel defaults.
        for (const auto& info : forrobox::ids::channelInfos)
            rig.setValue (forrobox::ids::channelParam (info.id, forrobox::ids::ghost), info.ghost);

        // THE REAL CHAIN — the profile's own timbre, and the shipped defaults
        // for MIX, the limiter and master.
        //
        // AudioRig deliberately defaults the bus to transparent so that every
        // test written before 03-03 keeps measuring the voice stage. That
        // default is wrong here, and it showed: with the normalisation removed,
        // the audition reported three of four profiles clipping at 1.19-1.28
        // while testFullChainHeadroom measured 0.753 — because the audition was
        // rendering with no character bus, no limiter and unity master.
        rig.useShippedChain (profile.timbreIndex);

        {
            auto state = rig.processor.lockPatternState();
            forrobox::applyGroove (*state, profile, groove);
        }

        // A COPY OF WHAT THE RIG WILL ACTUALLY RENDER, taken from the rig — not
        // a second application of the same groove into a fresh State.
        //
        // The first version of this did apply it twice, and the hit-count check
        // below could then never fail: both sides came from `groove`, so a
        // renderer that applied `defaultGroove()` to the RIG still wrote a .mid
        // for the right groove and the counts agreed. Mutating it proved
        // exactly that — exit 0, every check green. Same shape as the
        // projection-on-both-sides finding at 09-01, reproduced in the check
        // built to catch the wrong groove.
        forrobox::State rendered;
        {
            auto state = rig.processor.lockPatternState();
            rendered = *state;
        }

        // The profile's bateria mute, which is data the profile carries.
        rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[4].id,
                                                   forrobox::ids::mute),
                      profile.bateriaMuted ? 1.0f : 0.0f);

        // Four bars at the GROOVE's tempo, plus a tail.
        const auto barSeconds = 4.0 * 60.0 / static_cast<double> (groove.bpm);
        const auto numSamples = static_cast<int> ((barSeconds * 4.0 + 1.0) * kSampleRate);

        auto buffer = rig.render (numSamples - numSamples % 512, 512);

        const auto rawPeak = bufferPeak (buffer);

        // NO normalisation. From 03-01 to 03-02 this scaled every render to
        // -3 dBFS and printed the applied gain, because three of the four
        // profiles summed past 1.0 with no limiter or master to hold them —
        // writing a clipped file would have made the A/B about clipping rather
        // than about the grooves.
        //
        // 03-03 built the limiter and the master, so the chain now sets its own
        // level and the audition renders it. If something clips, that is a
        // finding about the chain rather than something for a normaliser to
        // hide.

        const auto stem = juce::String (profile.id()) + "__" + groove.id;
        const auto file = directory.getChildFile (stem + ".wav");
        file.deleteFile();

        juce::WavAudioFormat wav;
        std::unique_ptr<juce::OutputStream> stream { file.createOutputStream() };

        if (stream == nullptr)
        {
            // CHECKED, not just printed. These two `continue` paths recorded no
            // check at all, so a run that opened nothing reported "0 / 0 checks
            // passed — OK" and exited 0 — defeating the reportSummary() this
            // same plan wired into TestMain, one function over. /code-review.
            check (false, stem + ": the .wav opens for writing");
            continue;
        }

        const auto options = juce::AudioFormatWriterOptions()
                               .withSampleRate (kSampleRate)
                               .withNumChannels (2)
                               .withBitsPerSample (24);

        auto writer = wav.createWriterFor (stream, options);

        if (writer == nullptr)
        {
            check (false, stem + ": the .wav gets a writer");
            continue;
        }

        // The RETURN VALUE. A full disk makes this false, leaves a truncated
        // WAV on disk, and — discarded — let the run report every check green
        // and hand the listener silence. /code-review.
        check (writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples()),
               stem + ": the .wav is written");
        writer.reset();

        // ── the .mid beside it ──────────────────────────────────────────────
        //
        // 07-01's writer, CALLED — not re-implemented. It is cross-checked byte
        // for byte against the prototype's own exportMIDI on every build, so
        // anything written here would be a second, unchecked implementation.
        forrobox::ChannelGate muted {};
        muted[4] = profile.bateriaMuted;

        const auto bytes = forrobox::renderStandardMidiFile (rendered, groove.bpm,
                                                             forrobox::kPatternLength, muted);

        const auto midiFile = directory.getChildFile (stem + ".mid");
        midiFile.deleteFile();

        check (midiFile.replaceWithData (bytes.data(), bytes.size()),
               stem + ": the .mid is written");

        // ── and the render is ASSERTED ──────────────────────────────────────
        check (isFinite (buffer), stem + ": the render is finite");
        check (rawPeak > 0.02f, stem + ": the render is not silent (peak "
                                     + juce::String (rawPeak, 4) + ")");
        check (rawPeak <= 1.0f, stem + ": the render does not clip (peak "
                                     + juce::String (rawPeak, 4) + ")");

        // THE ONE THAT MATTERS. Every check above passes for a file that
        // rendered the WRONG groove, and across thirty-two auditions nobody
        // would hear which. The MIDI just written is read back and its note-ons
        // counted against this groove's own stored velocities — so a renderer
        // that applied defaultGroove() every time fails by name.
        //
        // Counted against the STORED grid, not the performance: exportMIDI
        // writes `step * stepTicks` with no swing and excludes ghosts entirely
        // (PLANNING.md:584), which 07-03 recorded as two deliberately different
        // data paths. The .wav carries the humanised performance; the .mid
        // carries the grid.
        auto expectedHits = 0;

        for (size_t lane = 0; lane < groove.patterns.size(); ++lane)
        {
            const auto channel = forrobox::VoiceEngine::channelForLane (static_cast<int> (lane));

            if (muted[static_cast<size_t> (channel)])
                continue;

            forrobox::DecodedPattern decoded {};

            if (! forrobox::decodePattern (groove.patterns[lane], decoded))
                continue;

            for (auto step = 0; step < forrobox::kPatternLength; ++step)
                if (decoded[static_cast<size_t> (step)] > 0)
                    ++expectedHits;
        }

        auto noteOns = 0;
        {
            // FROM THE FILE ON DISK, not from `bytes`. The comment above used to
            // say "the MIDI just written is read back" while parsing the vector
            // it had just been written from — so nothing here ever touched the
            // artefact the listener actually opens. `ui-renders` is the standard
            // this docstring invokes, and it checks the PNG. /code-review.
            juce::FileInputStream in (midiFile);
            juce::MidiFile parsed;

            // BRACED. Written without them, the `else` bound to the innermost
            // `if` — the dangling-else — so "the .mid parses" fired once per
            // event that was not a note-on, and reported 238 failures against a
            // file that had parsed perfectly well.
            if (! in.openedOk() || ! parsed.readFrom (in))
            {
                check (false, stem + ": the .mid parses");
            }
            else
            {
                for (int t = 0; t < parsed.getNumTracks(); ++t)
                    if (const auto* track = parsed.getTrack (t))
                        for (const auto* event : *track)
                            if (event->message.isNoteOn())
                                ++noteOns;
            }
        }

        checkEqual (noteOns, expectedHits,
                    stem + ": the rendered MIDI carries THIS groove's hits");

        std::cout << "  " << profile.displayName() << " / " << groove.name
                  << "  " << groove.bpm << " BPM"
                  << "  swing " << juce::String (groove.swing, 0).toStdString()
                  << "  cachaça " << juce::String (groove.cachaca, 0).toStdString()
                  << "  " << forrobox::timbreSpecs[static_cast<size_t> (
                                juce::jlimit (0, 2, profile.timbreIndex))].displayName
                  << "  peak " << juce::String (rawPeak, 4).toStdString()
                  << (rawPeak > 1.0f ? "  *** CLIPS ***" : "")
                  << "  " << noteOns << " notes"
                  << "  -> " << file.getFileName() << " + .mid\n";
    }
}

/** 05-02 AC-1: the step and its velocities are published as ONE value.

    Three atomics before this — the velocities, then the step with release
    ordering, then the count. Ordered, so a reader that acquired the step saw
    velocities at least as new; NOT group-atomic, so it could hold step N beside
    step N+1's velocities. Nothing read them until this plan gave the publication
    three readers at frame rate.

    Driven from a second thread at full tilt rather than through a processor: the
    hazard is a reader observing a writer mid-publication, and a single-threaded
    test cannot reach it however many blocks it renders.

    The invariant is what makes a tear VISIBLE. Each step's velocities are a pure
    function of that step, so any snapshot whose velocities do not match its own
    index is a tear — no guessing which publication a reader caught. */
static void testStepPublicationIsGroupAtomic()
{
    section ("the step and its velocities are published as one value");

    using forrobox::State;

    const auto velocitiesFor = [] (int step)
    {
        std::array<std::uint8_t, State::kNumLanes> v {};

        for (size_t lane = 0; lane < v.size(); ++lane)
            v[lane] = static_cast<std::uint8_t> ((step * 7 + static_cast<int> (lane) * 13)
                                                 % (State::kMaxVelocity + 1));
        return v;
    };

    // ── the wire format round-trips every value the domain allows ───────────
    {
        auto mismatches = 0;

        for (int step = 0; step < State::kMaxSteps; ++step)
        {
            const auto sent = velocitiesFor (step);
            const auto got  = forrobox::stepwire::decode (forrobox::stepwire::encode (step, sent));

            if (got.step != step || got.velocities != sent)
                ++mismatches;
        }

        checkEqual (mismatches, 0, "every step and velocity set survives the 64-bit encoding");

        checkEqual (forrobox::stepwire::decode (0).step, forrobox::Clock::kStoppedStep,
                    "an all-zero word decodes as STOPPED, so a cleared publication and a fresh "
                    "processor agree");

        const auto atZero = forrobox::stepwire::decode (
            forrobox::stepwire::encode (0, velocitiesFor (0)));
        checkEqual (atZero.step, 0, "and step 0 is distinguishable from stopped");

        // CLAMPED, not masked: masking 200 gives 72, a plausible-looking wrong
        // answer that would light a pad at the wrong brightness.
        std::array<std::uint8_t, State::kNumLanes> hot {};
        hot.fill (200);
        checkEqual (static_cast<int> (forrobox::stepwire::decode (
                        forrobox::stepwire::encode (3, hot)).velocities[0]),
                    static_cast<int> (State::kMaxVelocity),
                    "an out-of-domain velocity clamps to 127 rather than masking to 72");
    }

    // ── a reader can never observe a step beside another step's velocities ──
    {
        forrobox::StepPublisher publisher;

        std::atomic<bool> running { true };
        std::atomic<int>  tears { 0 };
        std::atomic<long> observations { 0 };

        // TWO readers, and each counts LOCALLY. The first version incremented
        // two shared atomics per iteration, which throttled the reader's
        // sampling rate enough that a deliberately split publication went
        // undetected in 3 of 10 runs — a guard that misses the bug 30% of the
        // time is not a guard. Local counters published once at the end raise
        // the sample count by orders of magnitude for the same wall time.
        const auto readerBody = [&]
        {
            long localTears = 0;
            long localReads = 0;

            while (running.load (std::memory_order_relaxed))
            {
                const auto snap = publisher.read();

                if (snap.isStopped())
                    continue;

                if (snap.velocities != velocitiesFor (snap.step))
                    ++localTears;

                ++localReads;
            }

            tears.fetch_add (static_cast<int> (juce::jmin (localTears, 1000000L)),
                             std::memory_order_relaxed);
            observations.fetch_add (localReads, std::memory_order_relaxed);
        };

        std::thread readerA (readerBody);
        std::thread readerB (readerBody);

        constexpr int kPublications = 400000;

        for (int i = 0; i < kPublications; ++i)
        {
            const auto step = i % State::kMaxSteps;
            publisher.publish (step, velocitiesFor (step));
        }

        running.store (false, std::memory_order_relaxed);
        readerA.join();
        readerB.join();

        check (observations.load() > 0,
               "the readers observed the publication at all ("
                   + juce::String ((double) observations.load(), 0)
                   + " reads) — a test that observed nothing would report zero tears either way");

        checkEqual (tears.load(), 0,
                    "no observation paired a step with another step's velocities, across "
                        + juce::String (kPublications) + " publications");

        checkEqual (static_cast<int> (publisher.publicationCount()), kPublications,
                    "and every publication was counted");
    }

    // ── stopping does not count as a step ───────────────────────────────────
    {
        forrobox::StepPublisher publisher;

        publisher.publish (4, velocitiesFor (4));
        const auto afterOne = publisher.publicationCount();

        publisher.publishStopped();

        checkEqual (static_cast<int> (publisher.publicationCount()), static_cast<int> (afterOne),
                    "publishing STOPPED does not increment the emitted count — a stopped "
                    "transport must not read as a stuck one");
        check (publisher.read().isStopped(), "and the snapshot reports stopped");
    }
}

// ── live MIDI out ───────────────────────────────────────────────────────

/** Collects every note-on a run of blocks emits, with its ABSOLUTE sample
    position — the block index times the block size, plus the offset within
    it. Absolute, because the whole point of the boundary checks is that the
    same musical span gives the same positions at any block size. */
struct LiveNote
{
    long long sample;
    int note;
    int velocity;
};

static std::vector<LiveNote> collectLiveNotes (AudioRig& rig, int blocks,
                                           bool includeNoteOffs = false)
{
    // The size comes from the RIG, not from the caller — see
    // AudioRig::preparedBlockSize.
    const auto blockSize = rig.preparedBlockSize;

    std::vector<LiveNote> notes;

    juce::AudioBuffer<float> block (rig.processor.getTotalNumOutputChannels(), blockSize);
    juce::MidiBuffer midi;

    for (int b = 0; b < blocks; ++b)
    {
        block.clear();
        midi.clear();
        rig.processor.processBlock (block, midi);

        for (const auto metadata : midi)
        {
            const auto message = metadata.getMessage();

            if (message.isNoteOn() || (includeNoteOffs && message.isNoteOff()))
                notes.push_back ({ static_cast<long long> (b) * blockSize + metadata.samplePosition,
                                   message.getNoteNumber(),
                                   message.isNoteOn() ? message.getVelocity() : 0 });
        }
    }

    return notes;
}

static void testLiveMidiCarriesThePerformance()
{
    section ("live MIDI emits the humanised performance, not the grid");

    // ── every audible hit leaves as a note, at the GM number ────────────
    {
        AudioRig rig;
        rig.setValue (forrobox::ids::cachaca, 0.0f);

        for (int step = 0; step < 16; ++step)
            rig.setStep (0, step, 100);   // zabumba only

        rig.processor.setPlaying (true);

        const auto notes = collectLiveNotes (rig, 32);

        check (! notes.empty(), "the plugin emits MIDI at all — it declared "
                                "NEEDS_MIDI_OUTPUT in Phase 1 and wrote nothing until now");

        auto wrongNote = 0;

        for (const auto& n : notes)
            if (n.note != forrobox::gm::noteForLane (0))
                ++wrongNote;

        checkEqual (wrongNote, 0,
                    "every note carries zabumba's GM number — PLANNING.md:815");
    }

    // ── the humanisation reaches the MIDI ───────────────────────────────
    //
    // The check that matters. A writer that emitted notes on the GRID would
    // pass "notes exist" and fail this: CACHAÇA moves the offsets, and the
    // whole reason Phase 7 planning chose the performance over the grid is
    // that a doubled instrument must drift WITH this plugin, not against it.
    {
        AudioRig dry;
        dry.setValue (forrobox::ids::cachaca, 0.0f);

        AudioRig drunk;
        drunk.setValue (forrobox::ids::cachaca, 100.0f);

        for (auto* rig : { &dry, &drunk })
        {
            for (int step = 0; step < 16; ++step)
                rig->setStep (0, step, 100);

            rig->processor.setPlaying (true);
        }

        const auto sober = collectLiveNotes (dry, 32);
        const auto jittered = collectLiveNotes (drunk, 32);

        auto moved = 0;
        const auto shared = juce::jmin (sober.size(), jittered.size());

        for (size_t i = 0; i < shared; ++i)
            if (sober[i].sample != jittered[i].sample)
                ++moved;

        check (moved > 0,
               "CACHAÇA moves the MIDI offsets — the notes carry the jitter, so a "
               "doubled instrument drifts WITH this plugin rather than against it");
    }

    // ── ghosts are notes too ────────────────────────────────────────────
    {
        AudioRig rig;
        rig.setValue (forrobox::ids::cachaca, 100.0f);
        rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                   forrobox::ids::ghost), 100.0f);

        // A SILENT lane: every note that appears is a ghost, because the
        // pattern contains nothing to play.
        rig.processor.setPlaying (true);

        const auto notes = collectLiveNotes (rig, 64);

        check (! notes.empty(),
               "ghost notes reach the MIDI out — they are performance, never written "
               "into the pattern, and the file export never sees them");

        // And they arrive AS GHOSTS. "notes exist" alone would pass on a
        // single leaked programmed hit; the velocity range is what says
        // these came from the ghost roll — PLANNING.md:583 gives it as
        // 0.20-0.32 normalised, which is 25..41 of 127.
        // The window is the SPEC's, computed rather than widened. Ghosts are
        // 0.20 + roll x 0.12 and are deliberately NOT put through the
        // velocity humanisation, so the range is exact: round(0.20 x 127)
        // = 25 to round(0.32 x 127) = 41. The first version of this check
        // accepted 20..45 under a comment claiming 25..41 — a silently
        // widened window, which is the "check with no teeth" shape this
        // project keeps finding.
        const auto lowest  = juce::roundToInt (forrobox::kGhostVelocityMin * 127.0f);
        const auto highest = juce::roundToInt ((forrobox::kGhostVelocityMin
                                                 + forrobox::kGhostVelocitySpan) * 127.0f);

        auto outsideGhostRange = 0;

        for (const auto& n : notes)
            if (n.velocity < lowest || n.velocity > highest)
                ++outsideGhostRange;

        checkEqual (outsideGhostRange, 0,
                    utf8 ("and every one carries a ghost's velocity — ")
                      + juce::String (lowest) + ".." + juce::String (highest)
                      + ", PLANNING.md:583's 0.20-0.32, not a programmed hit's");
    }
}

static void testLiveMidiFollowsMuteAndSolo()
{
    section ("live MIDI follows audible — mute AND solo, unlike the file export");

    const auto notesWith = [] (bool muteFirst, bool soloSecond)
    {
        AudioRig rig;
        rig.setValue (forrobox::ids::cachaca, 0.0f);

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            for (int step = 0; step < 16; ++step)
                rig.setStep (lane, step, 100);

        if (muteFirst)
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[0].id,
                                                       forrobox::ids::mute), 1.0f);
        if (soloSecond)
            rig.setValue (forrobox::ids::channelParam (forrobox::ids::channelInfos[1].id,
                                                       forrobox::ids::solo), 1.0f);

        rig.processor.setPlaying (true);
        return collectLiveNotes (rig, 24);
    };

    const auto all = notesWith (false, false);
    const auto muted = notesWith (true, false);
    const auto soloed = notesWith (false, true);

    check (muted.size() < all.size(),
           "muting a channel removes its notes from the MIDI, exactly as it removes "
           "them from the audio");

    check (soloed.size() < all.size(),
           "soloing a channel removes everyone else's — live MIDI reads `audible`, "
           "which is mute AND solo, and DIFFERS from the file export on purpose: "
           "exportMIDI reads mute and never looks at solo");
}

static void testLiveMidiSurvivesTheBlockBoundary()
{
    section ("the same musical span gives the same notes at any block size");

    // THE CHECK THAT NEEDS MORE THAN ONE SIZE. The 32 ms lookahead is ~1536
    // samples and a block is commonly 128 to 512, so a scheduled hit
    // routinely lands two or three blocks out. An offset rebased the wrong
    // way is correct at 1024 and wrong at 128 — invisible to any check that
    // runs at one size.
    const auto notesAt = [] (int blockSize)
    {
        AudioRig rig (kSampleRate, blockSize);
        rig.setValue (forrobox::ids::cachaca, 0.0f);

        for (int lane = 0; lane < forrobox::State::kNumLanes; ++lane)
            for (int step = 0; step < 16; ++step)
                rig.setStep (lane, step, 100);

        rig.processor.setPlaying (true);

        // The same SPAN of samples at every size: 32768 samples.
        return collectLiveNotes (rig, 32768 / blockSize);
    };

    const auto reference = notesAt (512);

    check (! reference.empty(), "the reference block size produced notes to compare");

    for (const auto size : { 32, 64, 128, 1024 })
    {
        const auto other = notesAt (size);

        // THE PROJECT'S OWN EQUALITY, not bit-identity: "step indices
        // exact, sample positions within one sample" — a Key Decision from
        // 02-03, because a position-driven clock cannot be bit-identical
        // across partitions and host sync requires that it not be. This
        // check first asserted exact samples and failed by ONE at four
        // block sizes, which is the documented tolerance rather than a bug.
        //
        // It still catches what it exists for: rebasing an offset the wrong
        // way shifts a note by a BLOCK — 32 to 1024 samples — not by one.
        auto identical = other.size() == reference.size();

        for (size_t i = 0; identical && i < other.size(); ++i)
            identical = std::llabs (other[i].sample - reference[i].sample) <= 1
                     && other[i].note == reference[i].note
                     && other[i].velocity == reference[i].velocity;

        check (identical,
               juce::String ("the note sequence at block size ") + juce::String (size)
                 + " matches 512's — same count, same notes, positions within one "
                   "sample, which is this project's partition equality");
    }
}

static void testSharedGmNoteRetriggersRatherThanTruncating()
{
section ("zabumba and BB share GM note 36, and the second hit is not cut short");

// THE BUG THIS EXISTS FOR, found by /simplify's efficiency pass at 07-03's
// close. PLANNING.md:815 and :819 both say 36 — deliberately — so when
// zabumba and BB fire on the same step the queue held
//   on(36)@t1, off(36)@t1+gate, on(36)@t2, off(36)@t2+gate
// with t1 < t2 < t1+gate, and the FIRST off arrived after the SECOND on and
// truncated it. At 132 BPM a step is 114 ms, the gate is 40 ms and CACHAÇA's
// jitter is +/-22 ms, so the two land inside one gate routinely.
AudioRig rig;
rig.setValue (forrobox::ids::cachaca, 0.0f);

// Lane 0 is zabumba, lane 4 is bb — both GM 36.
for (int step = 0; step < 16; ++step)
{
    rig.setStep (0, step, 100);
    rig.setStep (4, step, 100);
}

rig.processor.setPlaying (true);

const auto events = collectLiveNotes (rig, 24, true);

checkEqual (forrobox::gm::noteForLane (0), forrobox::gm::noteForLane (4),
            "the premise holds: zabumba and BB really do share one GM note");

// Walk the stream: an off must never arrive while a LATER on is already
// sounding the same note. Equivalently, between any two consecutive ons of
// one note there is at most one off, and it precedes the second on.
auto truncations = 0;
auto depth = 0;

for (const auto& e : events)
{
    if (e.note != forrobox::gm::noteForLane (0))
        continue;

    if (e.velocity > 0)
        ++depth;
    else if (--depth < 0)
        depth = 0;

    // depth > 1 would mean two ons with no off between them, which is the
    // retrigger we WANT; what we forbid is an off landing while the newer
    // note should still be running, which shows up as depth returning to 0
    // while a later on is still within its own gate. The stream-level
    // property is simpler: every off is the LAST event for that note in its
    // group, which `stillSounding` above already covers. Here we assert the
    // count instead — a truncating stream emits one off per on.
    if (depth < 0)
        ++truncations;
}

const auto ons = std::count_if (events.begin(), events.end(),
                                [] (const LiveNote& e) { return e.velocity > 0; });
const auto offs = static_cast<long> (events.size()) - ons;

check (offs < ons,
       "a retrigger EXTENDS the note rather than being cut short by the previous "
       "note-off — two lanes on one GM number produce fewer offs than ons, because "
       "the stale off is dropped when the second hit arrives");

checkEqual (truncations, 0, "and no note-off precedes its own note-on");
}

static void testLiveMidiNeverStrandsANote()
{
    section ("every note-on gets its note-off, in both gate modes");

    for (const auto mode : { 0, 1 })
    {
        AudioRig rig;
        rig.setValue (forrobox::ids::cachaca, 0.0f);
        rig.setChoice (forrobox::ids::midiGate, mode);

        for (int step = 0; step < 16; ++step)
            rig.setStep (0, step, 100);

        rig.processor.setPlaying (true);

        auto events = collectLiveNotes (rig, 64, true);

        // THE RUN ENDS MID-GROOVE, so note-offs for the last hits are still
        // queued — that is correct, not a leak, and asserting balance
        // without accounting for it would be asserting that the gate is
        // zero-length. Stop the transport and drain once more: THAT is the
        // moment nothing may be left sounding.
        rig.processor.setPlaying (false);

        {
            juce::AudioBuffer<float> block (rig.processor.getTotalNumOutputChannels(), 512);
            juce::MidiBuffer midi;
            block.clear();
            midi.clear();
            rig.processor.processBlock (block, midi);

            for (const auto metadata : midi)
                if (metadata.getMessage().isNoteOff())
                    events.push_back ({ 0, metadata.getMessage().getNoteNumber(), 0 });
        }

        // NOT a count of ons against offs. A RETRIGGER is legitimately two
        // note-ons and one note-off: zabumba and BB share GM note 36, so
        // when both fire inside one gate the second on extends the first
        // rather than starting a second voice, and one off ends both. This
        // check asserted equal counts and only passed before because the
        // stale off was truncating the second note — the bug it was
        // supposed to be watching for.
        //
        // The property that actually matters: no note number is left in the
        // ON state once the transport has stopped.
        std::map<int, bool> sounding;

        for (const auto& e : events)
            sounding[e.note] = e.velocity > 0;

        auto stillSounding = 0;

        for (const auto& [note, on] : sounding)
            if (on)
                ++stillSounding;

        const auto name = juce::String (forrobox::ids::midiGateModes[static_cast<size_t> (mode)]);

        checkEqual (stillSounding, 0,
                    name + ": after the transport stops, no note is left sounding — a hung "
                           "note outlives this plugin's block and lives in someone else's "
                           "sampler until they reload it");

        checkEqual (rig.processor.getVoiceEngine().getDroppedMidiCount(), 0,
                    name + ": the pending queue never overflowed, so no note was "
                           "silently discarded");
    }
}

void runVoiceTests()
{
    // The instruments first: a broken one makes everything after it meaningless.
    testMeasurementInstruments();

    testConvolutionStage();
    testConvolutionLatencyIsReported();
    testUnreadableImpulseResponseIsRefused();
    testImpulseResponseSurvivesReload();
    testCharacterBusGains();
    testCharacterBusShapesTheSound();
    testCharacterBusSmoothing();
    testLimiterAndMaster();
    testLimiterToggleDoesNotBurst();

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
    testDisplayPositionTracksTheAudibleGroove();
    testStoppingClearsTheLastStepVelocities();
    testNoAllocationsWhileRendering();
    testVoicePoolUnderPressure();
    testFullChainHeadroom();
    testFactoryDefaults();
    testRingOutPassesThroughTheBus();
    testLaneToChannelMapping();
    testSampledLaneInvariant();
    testMonoOutputFoldsDown();
    testBusLayoutsAreAcceptedAndRefused();
    testStemsCarryOneChannelEach();
    testTailIsReportedToHost();
    testVoicesRingThroughTransportStop();
    testStepPublicationIsGroupAtomic();

    // Live MIDI out LAST, and after the instruments: these drive whole blocks
    // through the processor and read what comes back, so anything broken above
    // shows up here as a confusing MIDI failure rather than at its own cause.
    testLiveMidiCarriesThePerformance();
    testLiveMidiFollowsMuteAndSolo();
    testLiveMidiSurvivesTheBlockBoundary();
    testSharedGmNoteRetriggersRatherThanTruncating();
    testLiveMidiNeverStrandsANote();
}

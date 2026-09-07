/* ============================================================================
   FORRÓ BOX — plugin processor

   Skeleton only: silent, no parameters, no sequencer. What it does establish
   is the audio-thread contract every later phase inherits — processBlock does
   no allocation, no locking and no I/O.
============================================================================ */
#pragma once

#include <JuceHeader.h>

#include "Clock.h"
#include "ForroBoxState.h"
#include "ParameterIDs.h"

#include <atomic>

class ForroBoxAudioProcessor final : public juce::AudioProcessor,
                                     private forrobox::StepListener
{
public:
    ForroBoxAudioProcessor();
    ~ForroBoxAudioProcessor() override = default;

    // ── lifecycle ───────────────────────────────────────────────────────────
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    // ── audio thread ────────────────────────────────────────────────────────
    // Pull the double-precision overload into scope so declaring only the
    // float one does not hide it (-Woverloaded-virtual).
    using juce::AudioProcessor::processBlock;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;

    // ── editor ──────────────────────────────────────────────────────────────
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }

    // ── identity and capabilities ───────────────────────────────────────────
    const juce::String getName() const override         { return JucePlugin_Name; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }

    // ── programs (a single default; real presets are a post-v0.1 concern) ───
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    // ── parameters ──────────────────────────────────────────────────────────
    /** The full automatable surface: 10 globals + 7 params x 5 channels = 45,
        arranged into a GLOBAL group plus one group per instrument. */
    static juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    // ── transport ───────────────────────────────────────────────────────────
    /** The header's play/stop button (Phase 4) drives this.

        Deliberately NOT an APVTS parameter and NOT persisted:
          - not a parameter, because PLANNING.md's VST3 parameter-mapping list
            omits it, and a play toggle on an automation lane would fight the
            host's own transport;
          - not persisted, because a plugin that resumes playing when a project
            is reopened is hostile. This is the opposite call from `dirty` and
            `activeProfile`, which are persisted state.

        Starting resets the clock, so it never resumes mid-pattern. */
    void setPlaying (bool shouldPlay);
    bool isPlaying() const noexcept { return playing.load (std::memory_order_relaxed); }

    /** The step most recently triggered, or -1 while stopped. Phase 5's
        playhead reads this at frame rate. Relaxed on purpose: it is a display
        value, and a one-frame-stale read is invisible where a lock would not
        be. */
    int getCurrentStep() const noexcept { return currentStep.load (std::memory_order_relaxed); }

    /** The step window for a `steps` CHOICE index, from ids::stepWindows.

        Named rather than inlined because forwarding the choice index where a
        step count belongs is exactly the trap 02-01 removed from expandPattern,
        and it would be silent: the clock would run a 1-step window and the
        groove would simply be wrong. */
    static int stepsForChoiceIndex (int choiceIndex) noexcept;

    // ── state ───────────────────────────────────────────────────────────────
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    /** RAII handle to the non-automatable state: pattern grid, profile, dirty
        flag, preset index, pattern slots.

        Access goes through this rather than a raw reference so that EVERY touch
        is serialised against a host calling get/setStateInformation from a loader
        thread. A lock that only the state methods respected would have been worse
        than none: the header would advertise protection while the primary access
        path — the editor holding a reference — stayed unguarded.

        Hold it for as short a scope as possible, and never on the audio thread. */
    class LockedState
    {
    public:
        LockedState (juce::CriticalSection& sectionToLock, forrobox::State& stateToUse)
            : lock (sectionToLock), state (stateToUse) {}

        forrobox::State* operator->() const noexcept { return &state; }
        forrobox::State& operator*()  const noexcept { return state; }

        LockedState (const LockedState&) = delete;
        LockedState& operator= (const LockedState&) = delete;

    private:
        const juce::ScopedLock lock;
        forrobox::State& state;
    };

    /** Locks the non-automatable state for the lifetime of the returned handle. */
    LockedState lockPatternState() { return { stateLock, patternState }; }

    // Last values seen by prepareToPlay. 0 only before the first prepare —
    // releaseResources deliberately retains them, so a host closing its audio
    // device while the editor stays open cannot hand callers a zero to divide by.
    double getCurrentSampleRate() const noexcept        { return currentSampleRate.load (std::memory_order_relaxed); }
    int    getCurrentBlockSize()  const noexcept        { return currentBlockSize.load  (std::memory_order_relaxed); }

private:
    // Receives each step the clock places. Private: the clock is an
    // implementation detail, not part of the processor's public surface.
    void stepTriggered (forrobox::StepEvent event) override;

    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // Guards patternState. Taken by lockPatternState() and by both state
    // methods, so there is no unguarded path to the data. Never taken on the
    // audio thread — processBlock does not touch patternState.
    juce::CriticalSection stateLock;
    forrobox::State patternState;

    // The sequencer clock. Advanced from processBlock, prepared in
    // prepareToPlay. Its step events drive nothing until Phase 3.
    forrobox::Clock clock;

    // Cached raw parameter pointers. Looked up once at construction so
    // processBlock reads a float through a pointer instead of doing a
    // string-keyed lookup on the audio thread.
    std::atomic<float>* bpmParam   { nullptr };
    std::atomic<float>* swingParam { nullptr };
    std::atomic<float>* stepsParam { nullptr };

    // Written on the message/prepare thread, read on the audio thread and by the
    // editor. Atomic because plain scalars across threads are a data race, not
    // merely a stale read — and this file is the template later phases inherit.
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };
    std::atomic<bool>   playing           { false };

    // Set by setPlaying on the message thread, consumed by processBlock on the
    // audio thread. The clock's own fields are plain doubles and ints, so
    // resetting it from the message thread while the audio thread is inside
    // advance() is a data race on non-atomic memory — not a benign stale read.
    // The reset therefore happens where the clock is actually used.
    std::atomic<bool>   resetPending      { false };
    std::atomic<int>    currentStep       { forrobox::Clock::kStoppedStep };

    static_assert (std::atomic<double>::is_always_lock_free,
                   "atomic<double> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "atomic<int> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<bool>::is_always_lock_free,
                   "atomic<bool> must be lock-free — it is read on the audio thread");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessor)
};

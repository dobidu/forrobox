/* ============================================================================
   FORRÓ BOX — plugin processor

   Skeleton only: silent, no parameters, no sequencer. What it does establish
   is the audio-thread contract every later phase inherits — processBlock does
   no allocation, no locking and no I/O.
============================================================================ */
#pragma once

#include <JuceHeader.h>

#include "ForroBoxState.h"
#include "ParameterIDs.h"

#include <atomic>

class ForroBoxAudioProcessor final : public juce::AudioProcessor
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
    juce::AudioProcessorValueTreeState apvts { *this, nullptr, "PARAMETERS", createParameterLayout() };

    // Guards patternState. Taken by lockPatternState() and by both state
    // methods, so there is no unguarded path to the data. Never taken on the
    // audio thread — processBlock does not touch patternState.
    juce::CriticalSection stateLock;
    forrobox::State patternState;

    // Written on the message/prepare thread, read on the audio thread and by the
    // editor. Atomic because plain scalars across threads are a data race, not
    // merely a stale read — and this file is the template later phases inherit.
    std::atomic<double> currentSampleRate { 0.0 };
    std::atomic<int>    currentBlockSize  { 0 };

    static_assert (std::atomic<double>::is_always_lock_free,
                   "atomic<double> must be lock-free — it is read on the audio thread");
    static_assert (std::atomic<int>::is_always_lock_free,
                   "atomic<int> must be lock-free — it is read on the audio thread");

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ForroBoxAudioProcessor)
};

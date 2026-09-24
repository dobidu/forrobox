#pragma once

/*  A user-loaded sample per channel — PLANNING.md:840's "load a user sample for
    that channel".

    WHAT IT REPLACES. `voiceSpecs[lane].usesSample` splits `scheduleSample` from
    `scheduleSynth` (VoiceEngine.cpp:303). A loaded user sample makes its lane
    sampled for as long as it is loaded, whatever its spec says — so it stands in
    for a synthesised voice, or for zabumba's three measured layers. Clearing it
    returns the built-in voice, which is what makes the feature reversible
    without reloading the project.

    THE HANDOVER, AND WHY IT IS NOT A DOUBLE BUFFER.

    It was one, with an argument attached: two slots per channel, and a voice
    could not still be reading the retired one because "a sample voice is bounded
    by its file's length and by DECAY's envelope, which is seconds at most" and
    "two loads require two trips through a file chooser".

    BOTH PREMISES WERE FALSE. `scheduleUserSample` sets the envelope from
    `decayFraction`, which reaches 1.0 at DECAY 100 — so a voice plays the WHOLE
    file, up to the thirty-second cap `load` admits. And `restoreUserSamples`
    calls `load` unconditionally from `setStateInformation`, which a host runs on
    the message thread WHILE AUDIO IS RUNNING for every preset click, undo and
    project reload. Two loads therefore need no user interaction at all, and the
    third one's `setSize` frees the block a live voice is reading. /code-review.

    So nothing is ever overwritten. A load allocates a NEW buffer and publishes
    a pointer to it; the previous one is retired to a list and kept alive. The
    retired buffers are freed only in `prepare`, which the processor calls after
    `VoiceEngine::prepare` has reset every voice — so at that moment no voice can
    hold a pointer into any of them. That is a proof rather than an argument, and
    it is the distinction the first version got wrong.
*/

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>
#include <atomic>
#include <memory>
#include <vector>

#include "ForroBoxState.h"

namespace forrobox
{

/** One decoded, host-rate sample. Read by the audio thread; never resized by it. */
struct UserSample
{
    juce::AudioBuffer<float> audio;
    // NO `sourceRate`. One was stored and never read: `load` resamples to the
    // host rate, so `scheduleUserSample` uses the pitch factor as the whole
    // read rate and nothing downstream needs the file's own. /code-review.

    bool isLoaded() const noexcept { return audio.getNumSamples() > 0; }

    /** Linearly interpolated read, clamped. Mirrors `ZabumbaSampler::readSample`
        so both sources behave the same under PITCH. */
    float read (int channel, double position) const noexcept;
};

class UserSamples
{
public:
    static constexpr int kNumChannels = State::kNumChannels;

    /** Records the host rate and FREES retired buffers.

        Called from `prepareToPlay`, after `VoiceEngine::prepare` has reset every
        voice — which is the only moment a retired buffer is provably unreachable.
        See the header. */
    void prepare (double hostSampleRate);

    /** Decodes `file` for `channel`, resampling to the host rate.

        Opens a file, so never from the audio thread. Returns false and changes
        nothing if no audio reader can be made for it: `existsAsFile()` is not
        the same question, and 09-07 shipped a bug where it was treated as if it
        were. */
    bool load (int channel, const juce::File& file);

    /** Stops using this channel's sample; the built-in voice returns. */
    void clear (int channel) noexcept;

    /** The sample the audio thread should use, or nullptr. Real-time safe. */
    const UserSample* active (int channel) const noexcept;

private:
    /** Published per channel. A pointer, not an index, because the buffer it
        names is never reused — see the header. */
    std::array<std::atomic<const UserSample*>, static_cast<size_t> (kNumChannels)> published {};

    /** Everything ever loaded and not yet freed, INCLUDING what is published.
        Freed wholesale in `prepare`; see the header for why that is the only
        safe moment. */
    std::vector<std::unique_ptr<UserSample>> owned;

    /** SEEDED at a nominal 48 kHz rather than 0, so a load before the host's
        first `prepareToPlay` is not refused — which an editor opened ahead of
        the first prepare makes reachable, and which made every load in
        `ChassisRig` fail. `outputDelaySamples` seeds itself the same way. A
        later `prepare` at a different rate re-decodes, because
        `restoreUserSamples` runs from `prepareToPlay`. */
    double rate { 48000.0 };
};

} // namespace forrobox

/* ============================================================================
   FORRÓ BOX — CACHAÇA's humanisation: its constants, and its randomness

   PLANNING.md calls CACHAÇA "the signature control", and "not an effect; a
   humanisation amount applied at three points": timing jitter, velocity
   variation and ghost-note probability. The numbers are transcribed from
   PLANNING.md (~line 556) and cross-checked against `app.js:633-676`, which
   settles what the prose leaves open — which draw happens per step and which
   per hit.

   Lives here rather than in Voices.h, where these constants started, because
   none of them is a voice concern: every consumer is the engine's scheduling
   path, and kLookaheadSeconds sizes the plugin's REPORTED LATENCY, which is a
   sequencer property. Voices.h kept the envelope floor, decayScaleFor and
   pitchFactorForSemitones, which are.
============================================================================ */
#pragma once

#include <juce_core/juce_core.h>

#include "ParameterIDs.h"

#include <cstdint>

namespace forrobox
{

// ── the numbers ─────────────────────────────────────────────────────────────

/** Timing jitter is `+/-(cachaca/100) x 22 ms`, uniform and bipolar, drawn ONCE
    per step and applied to that step's time — so every lane of a step moves
    together. */
inline constexpr double kMaxJitterSeconds = 0.022;

/** A ghost note is displaced a further `+/-10 ms` from its step's ALREADY
    JITTERED time (`t2 = t + (random - 0.5) * 0.02`, where `t` already contains
    the jitter). */
inline constexpr double kGhostJitterSeconds = 0.010;

/** Velocity variation: `v *= 1 - (cachaca/100) x 0.25 x random()`, drawn per
    HIT — so eight lanes of one step get eight different multipliers, unlike the
    timing jitter. The roll is [0, 1), so the multiplier is (0.75, 1.0] and a
    hit can never be made LOUDER. */
inline constexpr float kVelocityHumaniseDepth = 0.25f;

/** Ghost chance is `(ghost/100) x (0.22 + (cachaca/100) x 0.6)`.

    Note the base term: at CACHAÇA 0 ghosts still fire at `(ghost/100) x 0.22`.
    CACHAÇA raises the rate; it does not gate it. */
inline constexpr float kGhostBaseChance  = 0.22f;
inline constexpr float kGhostCachacaSpan = 0.60f;

/** A ghost's velocity, ALREADY normalised: `0.20 + random() x 0.12`. It is not
    put through the velocity humanisation above — it is random already. */
inline constexpr float kGhostVelocityMin  = 0.20f;
inline constexpr float kGhostVelocitySpan = 0.12f;

/** How far the scheduling origin is delayed so a hit placed EARLIER than its
    step is representable at all.

    The two excursions above, summed — **32 ms, not 22**. A ghost's offset is
    applied on top of the step's jitter, so 22 ms of headroom would still clamp
    a ghost, at a rate rising with CACHAÇA, and silently.

    The prototype gets away with `Math.max(engine.now(), t)` because its `now`
    sits a 100 ms lookahead behind the scheduling horizon, so the clamp never
    bites. In a plugin, offsets clamp at zero and it bites constantly: 32 ms at
    48 kHz is 1536 samples, wider than two 512-sample blocks, which would make
    the render block-size dependent — the one property AC-7 exists to protect.

    Reported to the host with setLatencySamples so a recording stays
    sample-exact. Reported UNCONDITIONALLY, including at CACHAÇA 0: what must
    not vary is the KNOB, because a latency that grew as CACHAÇA rose would
    force a host re-negotiation mid-session. It does change with the sample
    RATE, which hosts expect across a prepareToPlay.

    This cannot instead be pushed into the Clock. A sample-domain lookahead has
    to cross into step-domain through the CURRENT rate, and both ways of doing
    that are worse: shifting each span's start breaks the exact tiling that
    makes "no step emitted twice and none in a gap" structural, by
    `L x (rate2 - rate1)` at every tempo change; shifting the watermark keeps
    tiling but makes the lead `shift / rate`, so doubling the tempo halves the
    lead to 16 ms while the engine still adds 32 — the groove would sound late
    as a function of tempo, silently. Under SYNC both also pre-commit steps
    from a timeline the host abandons at every loop point. */
inline constexpr double kLookaheadSeconds = kMaxJitterSeconds + kGhostJitterSeconds;

/** The lookahead in samples, derived from the SAME expressions that produce the
    excursions it has to cover.

    Not `roundToInt (kLookaheadSeconds * rate)`: that rounds the SUM while the
    two use sites round independently, and at 24 545 of the 192 001 integer
    rates between 8 kHz and 200 kHz the reachable early excursion comes out one
    sample larger than the reported lookahead (at 8069 Hz, 259 against 258).
    Every standard rate is exact and the extreme draw needs two zero rolls in a
    row, so it was never a defect — but three comments in this codebase claim
    the offset is non-negative BY CONSTRUCTION, and that is only true if the
    figure is built the same way the excursion is. */
inline int lookaheadSamplesFor (double sampleRate) noexcept
{
    return static_cast<int> (std::lround (kMaxJitterSeconds * sampleRate))
         + static_cast<int> (std::lround (kGhostJitterSeconds * sampleRate));
}

// ── the randomness ──────────────────────────────────────────────────────────

/** What a humanisation value is FOR. Part of its key, so two purposes never
    collide even at the same step and lane. */
enum class Purpose
{
    jitter,           ///< the step's timing displacement
    velocity,         ///< a hit's velocity multiplier
    ghostRoll,        ///< compared against the ghost chance
    ghostOffset,      ///< the ghost's own displacement
    ghostVelocity,    ///< the ghost's velocity
    detune            ///< the triângulo's per-partial detune
};

/** A uniform value in [0, 1), derived from its key with no stored state.

    STATELESS, and that is the whole point. The property this has to hold is
    that muting a channel, editing one lane's pattern, or changing a GHOST knob
    must not re-time or re-level any OTHER channel — a groove's feel cannot
    depend on what else happens to be sounding.

    A stream of draws cannot hold that property structurally, only by
    discipline, and the discipline failed twice in one plan. First the velocity
    and ghost draws were conditional, so a gated channel consumed fewer draws
    and every later step's jitter shifted: measured, muting the ganzá moved BB's
    hits on 11 of 12 steps by up to 21 ms. That was fixed by drawing
    unconditionally — and the fix was incomplete, because `SynthVoice::trigger`
    draws five more values for the triângulo's detune, only for that lane, only
    after the audibility gate, and only for a voice that was actually claimed.
    Muting the triângulo still re-levelled everything else, and the header
    asserting the invariant listed that very detune as part of the stream in the
    sentence above the claim.

    Keyed, a value is a FUNCTION of its key. Draw order, lane iteration order,
    gating, how many partials the triângulo has, and anything a later phase adds
    below this level are all irrelevant by construction. There is no stream to
    keep in step, so there is nothing to be disciplined about.

    The mixing is splitmix64 — integer only, so it is bit-identical across
    compilers and platforms, which is what keeps a render reproducible. */
inline float humanisedValue (std::uint64_t seed, std::uint64_t step, int lane,
                             Purpose purpose, int index = 0) noexcept
{
    auto mix = [] (std::uint64_t x) noexcept
    {
        x += 0x9e3779b97f4a7c15ull;
        x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
        x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
        return x ^ (x >> 31);
    };

    // Each component folded in separately, so no two keys can alias by
    // arithmetic coincidence the way a plain sum would allow.
    auto hash = mix (seed);
    hash = mix (hash ^ step);
    hash = mix (hash ^ static_cast<std::uint64_t> (static_cast<std::uint32_t> (lane)));
    hash = mix (hash ^ static_cast<std::uint64_t> (static_cast<std::uint32_t> (purpose)));
    hash = mix (hash ^ static_cast<std::uint64_t> (static_cast<std::uint32_t> (index)));

    // Top 24 bits over 2^24: exactly representable in a float, and [0, 1) with
    // the same half-open range juce::Random::nextFloat gives.
    return static_cast<float> (hash >> 40) / 16777216.0f;
}

/** The same value mapped to [-1, 1), matching `Math.random() * 2 - 1` in the
    sketch — including its slight asymmetry. */
inline double bipolarHumanisedValue (std::uint64_t seed, std::uint64_t step, int lane,
                                     Purpose purpose, int index = 0) noexcept
{
    return static_cast<double> (humanisedValue (seed, step, lane, purpose, index)) * 2.0 - 1.0;
}

/** The default key seed. Changing it changes every groove's feel, so it is
    named rather than inline. */
inline constexpr std::uint64_t kHumanisationSeed = 0x464f52524f424f58ull;   // 'FORROBOX'

} // namespace forrobox

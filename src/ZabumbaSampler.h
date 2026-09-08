/* ============================================================================
   FORRÓ BOX — the sampled zabumba

   Phase 3's engine decision is hybrid: the zabumba plays four user-supplied
   one-shots, the other seven lanes are synthesised. This class owns the
   samples, the measurements that classify them, and the read path.

   WHAT THE MEASUREMENTS FOUND (2026-09-08, all four files 48 kHz / 24-bit /
   true stereo, one attack each so genuinely single hits):

     file  length   peak        rms      peak/rms  centroid  brightness
     01    0.498 s  -3.1 dBFS   0.1958      3.6      96 Hz     0.143
     02    0.457 s  -2.5 dBFS   0.1223      6.2     126 Hz     0.222
     03    0.344 s  -2.0 dBFS   0.0373     21.3     527 Hz     0.810
     04    0.265 s  -23.1 dBFS  0.0182      3.8     125 Hz     0.235

   `brightness` is RMS after a two-pole 250 Hz highpass over total RMS.

   They are NOT four variants of one articulation, which is what the project
   notes originally recorded and what the hybrid decision was taken on.
   01, 02 and 04 are one strike at three levels — 87-98% of their energy below
   160 Hz, centroids within 30 Hz of each other. 03 is a different articulation
   entirely: centroid 527 Hz, 78% of its energy ABOVE 160 Hz, peak/RMS 21. It is
   the stick or *pá* hit that happens to share the filename prefix.

   So the velocity set is 04 -> 02 -> 01 and 03 is held aside. The classifier
   derives that from the audio rather than from the filenames, which is what
   makes replacing a sample safe: the threshold is 0.45, measured to sit 1.9x
   above the brightest boom and 1.8x below the outlier.

   Levels are normalised by measured RMS, not by peak. Peak-normalising looked
   obvious and is wrong: 04's peak/RMS is 3.8 against 03's 21.3, so equal peaks
   would make the SOFT layer the loudest thing in the set. Adjacent layers
   crossfade, so there is no discontinuity at a boundary at all.
============================================================================ */
#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

#include <array>

namespace forrobox
{

class ZabumbaSampler
{
public:
    /** Four embedded files; at most four slots. */
    static constexpr int kMaxSlots = 4;

    /** Above this brightness a slot is a different articulation, not a
        velocity layer. Measured, not chosen: the three boom hits span
        0.143-0.235 and the outlier is 0.810. */
    static constexpr float kArticulationBrightnessThreshold = 0.45f;

    /** The RMS every velocity layer is normalised to before velocity is
        applied. Set to the loudest layer's own RMS so normalisation only ever
        attenuates, which keeps headroom for the mix bus 03-03 adds. */
    static constexpr float kTargetRms = 0.20f;

    /** Decodes and measures the embedded samples, then records the read rate
        for `hostSampleRate`.

        Allocates. Message/prepare thread only. Idempotent: the decode and the
        measurements happen once, and a later call at a different host rate
        recomputes only the read rate. */
    void prepare (double hostSampleRate);

    bool isReady() const noexcept { return numVelocityLayers > 0; }

    /** Slots classified as velocity layers, softest first. */
    int getNumVelocityLayers() const noexcept { return numVelocityLayers; }

    /** Slots held aside as a different articulation — the *pá* hit. Loaded and
        measured but not reachable from the velocity mapping, because the
        sequencer has one zabumba lane and nothing to select an articulation
        with until that lane gains a split of its own. */
    int getNumAlternateArticulations() const noexcept { return numAlternates; }

    /** The two layers a velocity blends between, with gains summing to 1.

        Two rather than one because the layers differ in body as well as level:
        switching hard at a boundary is audible even after normalisation. */
    struct LayerBlend
    {
        int   slotA { -1 }, slotB { -1 };
        float gainA { 0.0f }, gainB { 0.0f };
    };

    LayerBlend blendForVelocity (float velocity) const noexcept;

    /** That slot's file rate over the host rate. Multiplied by the pitch factor
        to give the read increment, so rate conversion and PITCH are ONE
        interpolation rather than two — see the note in ZabumbaSampler.cpp.

        PER SLOT, not one value for the set. It was a single member taken from
        the first file that decoded, which made this class's own promise — that
        "a replacement at another rate still plays at the right speed" — false
        for a mixed-rate set: replacing one file with a 44.1 kHz one played it
        8.8% fast and a semitone and a half sharp while the others stayed
        correct. The source archive these came from does contain 44.1 kHz
        material, so that is a reachable mistake, not a hypothetical one. */
    double getBaseReadRate (int slot) const noexcept;

    int getLengthSamples (int slot) const noexcept;

    /** How many channels this slot actually holds.

        The render path needs it to pick a pan law. It used to pick by which
        POOL the voice was in — every sample voice got the stereo law — which
        made readSample's documented handling of a mono replacement file wrong
        in the very next stage: at hard pan the stereo law has leftToLeft = 1
        AND rightToLeft = cos 0 = 1, so a mono file's two identical channels sum
        and it comes out at 2x amplitude. The question is about the source, so
        the source answers it. */
    int getNumChannels (int slot) const noexcept;

    /** Normalisation gain for a slot: kTargetRms / measured RMS. */
    float getNormalisationGain (int slot) const noexcept;

    /** One channel of one slot at a fractional position, by 4-point Hermite.

        Hermite rather than linear because this is now the ONLY interpolation
        stage in the path — folding the rate conversion into the read index
        removed the other one, so its quality is worth paying for. Out of range
        reads return 0 rather than clamping, so a voice runs off the end into
        silence instead of holding the last sample. */
    float readSample (int slot, int channel, double position) const noexcept;

    // ── measurements, for the tests and for the audition script ─────────────
    float getMeasuredRms (int slot) const noexcept;
    float getMeasuredPeak (int slot) const noexcept;
    float getMeasuredBrightness (int slot) const noexcept;
    bool  isVelocityLayer (int slot) const noexcept;

    /** How many slots decoded. A COUNT, not an index bound — see kMaxSlots. */
    int getNumLoadedSlots() const noexcept { return numLoadedSlots; }

    /** True if this slot holds audio. Iterate `0..kMaxSlots` and ask this,
        never `0..getNumLoadedSlots()`: a file that fails to decode leaves its
        index empty without shifting the others up, so the loaded slots are not
        contiguous and a count is the wrong bound. Using one meant a single
        failed decode would have had callers visit an empty slot and miss a real
        one — silently, since the empty slot reports zero for everything. */
    bool isLoaded (int slot) const noexcept;

    /** That slot's own file rate. */
    double getFileSampleRate (int slot) const noexcept;

private:
    void loadAndMeasure();

    struct Slot
    {
        juce::AudioBuffer<float> audio;
        float rms { 0.0f };
        float peak { 0.0f };
        float brightness { 0.0f };
        bool  isLayer { false };
        double fileSampleRate { 48000.0 };
        double baseReadRate { 1.0 };
    };

    std::array<Slot, kMaxSlots> slots;

    /** Slot indices classified as velocity layers, ordered softest to loudest
        by measured RMS. */
    std::array<int, kMaxSlots> layerOrder {};

    int numLoadedSlots { 0 };
    int numVelocityLayers { 0 };
    int numAlternates { 0 };
    bool loaded { false };
};

} // namespace forrobox

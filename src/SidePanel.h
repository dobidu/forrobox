/* ============================================================================
   FORRÓ BOX — the side panel

   The 280 px column on the right: the four regional profiles with the `CUSTOM`
   dirty tag, the three timbre characters with their LEDs, the `MIX` knob beside
   `LOAD IR…`, and the bundle footer pinned to the bottom.

   04-01 reserved this box and has painted it as a bare raised rectangle ever
   since — the last empty region of the chassis.

   Its own component owning its own layout, as HeaderBar, FooterBar and
   SequencerGrid each are, and for the reason all three record:
   `Component::getBounds` is parent-relative, and comparing a child's bounds
   against a chassis-space rectangle is the bug that made two tests read the
   footer's control as the header's.
============================================================================ */
#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include "Button.h"
#include "Knob.h"
#include "KnobAttachment.h"
#include "LookAndFeel.h"
#include "ParameterIDs.h"
#include "Surface.h"
#include "TimbreRow.h"

#include <array>
#include <memory>

class ForroBoxAudioProcessor;

namespace forrobox
{

/** 30 Hz. The tag's fade is 200 ms and the stored profile changes on a click —
    neither needs the 60 Hz the playhead does, and the header and footer already
    settled on this rate for the same reason. */
inline constexpr int kSidePanelPollHz = 30;

namespace side
{
inline constexpr int kPadX = 14;   ///< css:388 .side padding 13px 14px
inline constexpr int kPadY = 13;

inline constexpr int kSectionGap = 11;      ///< css:389 .side gap
inline constexpr int kSectionInnerGap = 8;  ///< css:391 .side-sect gap

inline constexpr int kProfileGap  = 5;   ///< css:392 .profiles gap
inline constexpr int kProfilePadX = 10;  ///< css:395 .profile padding 7px 10px
inline constexpr int kProfilePadY = 7;

/** `margin-top: 3px` on the description block — css:400. */
inline constexpr int kDescriptionMarginTop = 3;

/** `line-height: 1.4` — css:400, and the reason the ACTIVE button is taller.

    The three lines are drawn as three lines rather than joined and wrapped.
    `app.js:281` joins them with a space and lets the box wrap, which at this
    width lands on four or five lines; `PLANNING.md:317` calls it "the 3-line
    description", the data IS three lines, and a wrapped box would make the
    button's height depend on font metrics rather than on the design. */
inline constexpr float kDescriptionLineHeight = 1.4f;

/** The active button's description, which is TWO rules and not one alpha.

    css:403 paints it `rgba(0,0,0,0.6)`; css:404 overrides the whole colour for
    the light theme to `rgba(255,255,255,0.7)` — a different colour AND a
    different alpha, because `--active` inverts between the themes. Painting
    `--bg` at one alpha looked right in dark and was wrong in light, which is the
    same two-row shape `StepPad::paintUnlit` records for the pad's ground. */
inline constexpr float kDescriptionAlpha = 0.6f;
inline constexpr float kDescriptionAlphaLight = 0.7f;

/** `color-mix(in srgb, var(--panel) 70%, var(--active))` — css:422, the lit
    timbre row's ground. The weight of the SECOND colour, which is what
    `theme::mix` takes. */
inline constexpr float kTimbreActiveMix = 0.30f;

/** The `●` floated right of the active profile's name — css:406, `font-size: 7px`. */
inline constexpr int kActiveDotSize = 7;

inline constexpr int kTimbreGap  = 4;    ///< css:415 .timbre-opts gap
inline constexpr int kTimbrePadX = 10;   ///< css:419 .timbre padding 6px 10px
inline constexpr int kTimbrePadY = 6;
inline constexpr int kTimbreLedSize = 7; ///< css:428 .tb-led

/** `box-shadow: 0 0 6px var(--c-ganza)` on the lit LED — css:429. */
inline constexpr float kTimbreLedGlowRadius = 6.0f;

inline constexpr int kMixGap = 10;       ///< css:431 .mix-row gap
inline constexpr int kMixKnobSize = 28;  ///< app.js:302, and PLANNING.md:382

inline constexpr int kBundleGap = 8;      ///< css:436 .bundle gap
inline constexpr int kBundlePadTop = 12;  ///< css:436 .bundle padding-top
inline constexpr int kBundleDotSize = 7;  ///< css:439 .bundle .bdot

/** `transition: opacity 0.2s` on the CUSTOM tag — css:411. */
inline constexpr double kCustomTagFadeSeconds = 0.2;
} // namespace side

/** Every box the side panel reserves, derived once from the region.

    The WHOLE stack, including what Task 2 fills — `StripLayout`'s rule, and the
    one 04-01 learned by computing a rect and dropping it. */
struct SidePanelLayout
{
    juce::Rectangle<int> content;        ///< the region minus its padding

    juce::Rectangle<int> profilesLabel;
    juce::Rectangle<int> customTag;

    struct ProfileRow
    {
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> name;
        juce::Rectangle<int> dot;          ///< empty unless this row is active
        juce::Rectangle<int> description;  ///< empty unless this row is active
    };

    std::array<ProfileRow, ids::profileInfos.size()> profiles;

    juce::Rectangle<int> timbreLabel;

    struct TimbreRow
    {
        juce::Rectangle<int> bounds;
        juce::Rectangle<int> name;
        juce::Rectangle<int> subLabel;
        juce::Rectangle<int> led;
    };

    std::array<TimbreRow, 3> timbres;

    juce::Rectangle<int> mixRow, mixKnob, loadIr;

    juce::Rectangle<int> bundle;        ///< the footer box, its top edge the border
    juce::Rectangle<int> bundleDot, bundleText;

    /** The height one profile button needs — taller when it shows its
        description, which only the ACTIVE one does (css:400/403). */
    static int profileHeight (bool showsDescription) noexcept;

    /** The height of one timbre row. */
    static int timbreHeight() noexcept;

    /** `activeProfile` selects which button is tall; -1 for none, which is what
        an unknown id from a newer build produces. */
    static SidePanelLayout forBounds (juce::Rectangle<int>, int activeProfileIndex) noexcept;
};

class SidePanel final : public juce::Component
{
public:
    explicit SidePanel (ForroBoxLookAndFeel&);
    ~SidePanel() override;

    void attachParameters (juce::AudioProcessorValueTreeState&);

    /** Which profile the stored state names, or -1 if it names one this build
        does not know — `findProfile` returns nullptr for that rather than
        resolving to the wrong groove, and so does this. */
    int activeProfileIndex() const noexcept { return activeProfile; }

    /** Pull the active profile and the dirty flag out of the stored state.
        CALLED, never waited for — 04-04's lesson. */
    void refreshFromState();

    /** How faded in the CUSTOM tag is, 0 to 1. */
    float customTagOpacity() const noexcept { return tagOpacity; }

    /** Advance the tag's fade by elapsed SECONDS it is TOLD. Never reads a
        clock; the poll that drives it does. */
    void advanceCustomTag (double seconds) noexcept;

    /** One tick of the panel's own poll, for the tests. CALLED, never waited
        for — 04-04's lesson, where three checks failed on MSVC's clock. */
    void pollForTest() { poll(); }

    /** The MIX knob, for the tests. */
    Knob& getMixKnob() const noexcept { return *mixKnob; }

    void paint (juce::Graphics&) override;
    void resized() override;

    const SidePanelLayout& getLayout() const noexcept { return layout; }

private:
    void paintProfiles (juce::Graphics&, juce::Rectangle<int> clip) const;
    void paintBundle (juce::Graphics&) const;

    ForroBoxLookAndFeel& lnf;
    SidePanelLayout layout;

    ::ForroBoxAudioProcessor* processor { nullptr };

    int activeProfile { -1 };
    bool dirty { false };
    float tagOpacity { 0.0f };

    /** Advance the fade and follow the stored state, on this panel's own tick —
        the way every other region here follows state. */
    void poll();

    std::unique_ptr<Button> loadIrButton;

    /** The three timbre rows, as BUTTONS bound to `ids::timbre` through the
        attachment 05-03 built for the STEPS pair. The lit row follows the
        PARAMETER and never the click, so host automation moves it with no
        editor gesture — 04-03's law.

        In the parameter's own CHOICE order, not visual order: passing them in
        the order they happen to be drawn is how a reordered layout silently
        re-maps a parameter. */
    std::array<std::unique_ptr<TimbreRow>, timbreSpecs.size()> timbreRows;
    std::unique_ptr<juce::ParameterAttachment> timbreAttachment;

    std::unique_ptr<Knob> mixKnob;
    std::unique_ptr<KnobAttachment> mixAttachment;

    PollTimer statePoll;
    double lastPollSeconds { 0.0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidePanel)
};

} // namespace forrobox

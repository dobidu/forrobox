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
#include "LookAndFeel.h"
#include "ParameterIDs.h"
#include "Surface.h"

#include <array>
#include <memory>

class ForroBoxAudioProcessor;

namespace forrobox
{

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

/** `rgba(0,0,0,0.6)` — css:403, the active button's description over its fill. */
inline constexpr float kDescriptionAlpha = 0.6f;

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

    void paint (juce::Graphics&) override;
    void resized() override;

    const SidePanelLayout& getLayout() const noexcept { return layout; }

private:
    void paintProfiles (juce::Graphics&, juce::Rectangle<int> clip) const;
    void paintTimbres (juce::Graphics&, juce::Rectangle<int> clip) const;
    void paintBundle (juce::Graphics&) const;

    ForroBoxLookAndFeel& lnf;
    SidePanelLayout layout;

    ::ForroBoxAudioProcessor* processor { nullptr };

    int activeProfile { -1 };
    bool dirty { false };
    float tagOpacity { 0.0f };

    std::unique_ptr<Button> loadIrButton;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SidePanel)
};

} // namespace forrobox

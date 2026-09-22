/* ============================================================================
   FORRÓ BOX — the settings gear

   THE ONLY INVENTED CONTROL IN THIS PLUGIN, and this header is where that is
   written down rather than discovered.

   `PLANNING.md:852` says the five tweakable settings "belong in a settings/gear
   menu" and stops there. There is no gear in `forrobox.css`, none in `app.js`,
   none in PLANNING.md's chassis layout, and `PLANNING.md:902` explicitly calls
   the prototype's own `tweaks-panel.jsx` "prototype-only scaffolding — not part
   of the plugin design". So there is no size, no position, no colour and no
   shape to port. PROJECT.md's design mandate is that a control the design source
   does not specify is not invented; the user asked for this one explicitly at
   08-02 planning, which is the explicit decision that mandate requires.

   EVERYTHING HERE IS THEREFORE A CHOICE, not a transcription, and each one is
   made to borrow from something that already exists:

     * The SIZE matches the mini button's height, so it sits on the same optical
       line as `÷2` and `×2` rather than introducing a third control height.
     * The COLOURS are `--fg-dim` at rest and `--fg` on hover, which is what
       every other quiet affordance in this chassis does.
     * There is NO press scale. 04-03 established that only `.btn` and
       `.arrow-btn` declare `:active`, and inventing one here would be the same
       error that plan corrected on the mute/solo variant.

   DRAWN AS A PATH, not as a glyph. `⚙` (U+2699) is in neither embedded font —
   04-01 already recorded that neither carries U+266A for Phase 8's `♪ NO PONTO`
   — and `verify-charset.py` pins every non-ASCII character in a `src/` literal
   to the repertoire this UI actually draws. A missing glyph renders as a box or
   as nothing, and nothing is what a silent failure looks like.
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "Button.h"
#include "LookAndFeel.h"

#include <functional>

namespace forrobox
{

namespace gear
{

/** The mini button's height, ASKED FOR rather than copied.

    It shipped as a literal 20 against that function's 18, so the comment
    claiming they matched was false on the first build — 04-02's `kKnobCellHeight`
    lesson, which is that a constant derived from another component must ask the
    component. The test asserts the relationship, so if the mini button moves,
    both move or the check fails. */
inline constexpr int kSize = Button::heightOf (Button::Variant::mini);

/** Teeth, and the two radii as fractions of the box. Chosen to read as a gear at
    20 px, which is the only size this is ever drawn at. */
inline constexpr int   kTeeth      = 8;
inline constexpr float kOuterRatio = 0.46f;
inline constexpr float kInnerRatio = 0.34f;
inline constexpr float kBoreRatio  = 0.16f;

} // namespace gear

/** A gear that opens the settings menu. */
class GearButton final : public juce::Component
{
public:
    explicit GearButton (ForroBoxLookAndFeel& lookAndFeelToUse);

    void paint (juce::Graphics&) override;

    void mouseEnter (const juce::MouseEvent&) override;
    void mouseExit  (const juce::MouseEvent&) override;
    void mouseUp    (const juce::MouseEvent&) override;

    /** Fired on release inside the button. The menu is the CALLER's business:
        this component knows how to look like a gear and nothing about settings,
        which is what keeps it testable without a `Settings` store. */
    std::function<void()> onClick;

    /** The shape, as a free function so a test can measure the geometry without
        constructing a component or rendering one. */
    static juce::Path shapeFor (juce::Rectangle<float> box) noexcept;

private:
    ForroBoxLookAndFeel& lnf;
    bool hovered { false };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (GearButton)
};

} // namespace forrobox

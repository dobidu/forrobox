/* ============================================================================
   FORRÓ BOX — LogoMark

   The 46x34 mark: three overlapping monoline silhouettes — a sanfona's bellows,
   a zabumba's head, a triângulo — from `PLANNING.md:248-256` and `app.js:47-58`.

   RELATIVE, for the reason the knob is. The paths are authored in a 46x34
   viewBox and drawn into a 36x27 box, so the coordinates AND the stroke widths
   are viewBox units: treating 1.5 / 1.7 / 1.9 as pixels would draw one correct
   mark at 46 px and a wrong one at every size the header actually uses.

   Three strokes in three colours — `--fg-dim`, `--fg` + `--fg-faint`, and
   `--c-zabumba`. That the triângulo is the accent is the whole point of the
   lockup, and a test asserts the three differ, because one `fillAll` also
   satisfies "there is ink".
============================================================================ */
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include "LookAndFeel.h"
#include "Theme.h"

namespace forrobox
{

namespace logo
{
inline constexpr float kViewBoxWidth  = 46.0f;   ///< app.js:47 viewBox "0 0 46 34"
inline constexpr float kViewBoxHeight = 34.0f;

inline constexpr int kWidth  = 36;   ///< css:158 .logo-mark
inline constexpr int kHeight = 27;

inline constexpr float kSanfonaStroke  = 1.5f;   ///< css:159
inline constexpr float kZabumbaStroke  = 1.7f;   ///< css:160
inline constexpr float kRodStroke      = 1.0f;   ///< css:161
inline constexpr float kTrianguloStroke = 1.9f;  ///< css:163

/// `cx=24.5 cy=18.5 r=8.8` — app.js:52.
inline constexpr float kZabumbaCx = 24.5f;
inline constexpr float kZabumbaCy = 18.5f;
inline constexpr float kZabumbaR  = 8.8f;

/// `gap: 10px` between the mark and the wordmark — css:157.
inline constexpr int kLockupGap = 10;
} // namespace logo

class LogoMark final : public juce::Component
{
public:
    explicit LogoMark (ForroBoxLookAndFeel&);

    void paint (juce::Graphics&) override;

private:
    ForroBoxLookAndFeel& lnf;

    juce::Path sanfona, pleats, zabumba, rods, triangulo, beater;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LogoMark)
};

} // namespace forrobox

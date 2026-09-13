#include "DragMidiButton.h"

namespace forrobox
{

namespace
{
/** U+2193, the arrow app.js:422 puts in `.dm-arrow`.

    A juce::String built from EXPLICIT UTF-8, not a const char*: String's
    const char* constructor goes through CharPointer_ASCII, which is how U+2039
    reached 04-04's reference sheet as "a<EUR>1/2" with every check green. */
const juce::String& arrowGlyph()
{
    static const juce::String text { juce::CharPointer_UTF8 ("\xe2\x86\x93") };
    return text;
}

const juce::String& subLabel()
{
    static const juce::String text { ".mid" };
    return text;
}
} // namespace

DragMidiButton::DragMidiButton (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    // `cursor: grab` — css:524. The affordance is the whole point of a call to
    // action, and it is honest here: the control does respond to the pointer,
    // it simply has nothing to hand over yet.
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
}

void DragMidiButton::paint (juce::Graphics& g)
{
    const auto box = contentBox().toFloat();

    if (box.isEmpty())
        return;

    const auto accent = theme::accent (theme::Accent::zabumba);
    const auto panel = lnf.token (theme::Token::panel);
    const auto radius = lnf.cornerRadius (dragmidi::kRadiusExtra);

    // `transform: scale(0.98)` — css:538. On the CONTEXT, around the box's own
    // centre, so the press reads as a push rather than a slide.
    //
    // A transform and not a smaller rectangle: a CSS transform scales the
    // element's TEXT with it, and shrinking the box alone would move the three
    // labels 2% closer together while leaving them at full size. The border and
    // the corner radius scale too, which is also what the browser does.
    juce::Graphics::ScopedSaveState pressState (g);

    if (pressed)
        g.addTransform (juce::AffineTransform::scale (dragmidi::kPressScale,
                                                       dragmidi::kPressScale,
                                                       box.getCentreX(), box.getCentreY()));

    const auto tintPct = hovered ? dragmidi::kHoverTintPct : dragmidi::kTintPct;

    // ── the hover glow, an OUTER shadow, so it is drawn first ──────────────
    //
    // `0 0 30px color-mix(in srgb, var(--c-zabumba) 55%, transparent)` —
    // css:537. Hover only: the idle rule's glow is `0 0 0 transparent` until the
    // 2.6s animation breathes it, and that animation is Phase 7's.
    if (hovered)
        juce::DropShadow (accent.withAlpha (dragmidi::kHoverGlowPct / 100.0f),
                          dragmidi::kHoverGlowRadius, {})
            .drawForRectangle (g, box.toNearestInt());

    // ── the ring: `0 0 0 Npx <accent>`, a spread with no blur ──────────────
    //
    // Drawn as a stroke OUTSIDE the border box, which is what a zero-blur
    // spread is. Solid accent on hover (css:537), a translucent 18% at rest
    // (css:526).
    {
        const auto ringWidth = hovered ? dragmidi::kHoverRingWidth
                                       : static_cast<float> (dragmidi::kRingWidth);

        g.setColour (hovered ? accent
                             : accent.withAlpha (dragmidi::kRingPct / 100.0f));
        g.drawRoundedRectangle (box.expanded (ringWidth * 0.5f), radius + ringWidth * 0.5f,
                                ringWidth);
    }

    // ── the ground: `linear-gradient(180deg, <accent at N%> + panel, panel)` ──
    g.setGradientFill (juce::ColourGradient::vertical (
        theme::mix (panel, accent, theme::mixWeight (100.0f - tintPct, tintPct)), box.getY(),
        panel, box.getBottom()));
    g.fillRoundedRectangle (box, radius);

    // `inset 0 1px 0 rgba(255,255,255,0.06)` — css:526, and it survives the
    // hover rule, which restates it.
    g.setColour (juce::Colours::white.withAlpha (dragmidi::kInsetAlpha));
    g.fillRect (box.withHeight (1.0f).reduced (radius * 0.5f, 0.0f));

    // ── the border: 1.5px color-mix(--c-zabumba 45%, --line-strong) ────────
    g.setColour (hovered ? accent
                         : theme::mix (lnf.token (theme::Token::lineStrong), accent,
                                       theme::mixWeight (100.0f - dragmidi::kBorderPct,
                                                          dragmidi::kBorderPct)));
    g.drawRoundedRectangle (box.reduced (dragmidi::kBorder * 0.5f), radius,
                            dragmidi::kBorder);

    // ── the three runs: arrow, label, sub-label ────────────────────────────
    //
    // Laid out left to right inside the padding, each taking its own width —
    // `display: flex; align-items: center; gap: 11px` (css:521).
    auto content = box.reduced (dragmidi::kPadX + dragmidi::kBorder,
                                dragmidi::kPadY + dragmidi::kBorder);

    const auto run = [&] (type::Style style, const juce::String& text, juce::Colour colour)
    {
        const auto width = type::trackedWidth (style, text);

        g.setColour (colour);
        type::drawTracked (g, style, text, content.withWidth (width),
                           juce::Justification::centred);

        content.removeFromLeft (width + dragmidi::kGap);
    };

    run (type::Style::dragMidiArrow, arrowGlyph(), accent);

    // `color: var(--fg)` on hover (css:535); at rest the label inherits the
    // footer's own `--fg-dim`.
    run (type::Style::dragMidiLabel, "DRAG MIDI",
         lnf.token (hovered ? theme::Token::fg : theme::Token::fgDim));

    // The row's OWN opacity, 0.7, which type::styleFor carries so that no call
    // site has to remember it.
    run (type::Style::dragMidiSub, subLabel(), accent);
}

void DragMidiButton::mouseEnter (const juce::MouseEvent&)
{
    hovered = true;
    repaint();
}

void DragMidiButton::mouseExit (const juce::MouseEvent&)
{
    hovered = false;
    pressed = false;
    repaint();
}

void DragMidiButton::mouseDown (const juce::MouseEvent& e)
{
    // Right-click belongs to the host, as it does on every other control here.
    if (e.mods.isPopupMenu())
        return;

    pressed = true;
    repaint();
}

void DragMidiButton::mouseUp (const juce::MouseEvent&)
{
    if (! pressed)
        return;

    pressed = false;
    repaint();

    // And NOTHING else. There is no onClick to fire and no drag to start: the
    // export is Phase 7's, and a stub that did half of it would be worse than
    // one that does none.
}

} // namespace forrobox

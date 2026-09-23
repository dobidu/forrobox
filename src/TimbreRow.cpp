#include "TimbreRow.h"

#include "Chassis.h"
#include "SidePanel.h"
#include "Surface.h"
#include "Theme.h"
#include "Typography.h"

namespace forrobox
{

TimbreRow::TimbreRow (ForroBoxLookAndFeel& lookAndFeelToUse, int indexToUse)
    : SelectableTile (lookAndFeelToUse, indexToUse)
{
}

int TimbreRow::heightOf() noexcept
{
    // A flex row of the two stacked labels against the LED — css:417's
    // `align-items: center`, so the row is as tall as its tallest child.
    const auto labels = textBox (type::Style::timbreName)
                      + textBox (type::Style::timbreSubLabel);

    return flexRow (labels, side::kTimbreLedSize)
         + side::kTimbrePadY * 2 + side::kBorder * 2;
}

juce::Rectangle<int> TimbreRow::ledBounds() const noexcept
{
    auto inner = getLocalBounds().reduced (side::kTimbrePadX + side::kBorder,
                                           side::kTimbrePadY + side::kBorder);

    return centredInRow (inner, inner.removeFromRight (side::kTimbreLedSize)
                                     .withHeight (side::kTimbreLedSize));
}

std::array<TimbreRow::Fringe, 2> TimbreRow::aberrationFringes (ForroBoxLookAndFeel& lookAndFeel)
{
    // `--danger` is a TOKEN (#ff4136) and `--c-triangulo` is an ACCENT
    // (#00c2c7). Reaching for `--c-bateria` because it is also red would be the
    // `accentSpecs`/`channelInfos` mistake one table over: they are different
    // hexes and the stylesheet names one of them.
    //
    // BACK TO FRONT. css:425 lists `1.2px 0 <danger>` first and
    // `-1.2px 0 <triangulo>` second, and CSS paints text shadows in reverse
    // source order — so the cyan one listed second is furthest back.
    return { Fringe { theme::accent (theme::Accent::triangulo)
                          .withAlpha (ciclo::kAberrationWeight), -ciclo::kAberrationPx },
             Fringe { lookAndFeel.token (theme::Token::danger)
                          .withAlpha (ciclo::kAberrationWeight),  ciclo::kAberrationPx } };
}

bool TimbreRow::isCiclotron() const noexcept
{
    return index == ciclotronTimbreIndex();
}

void TimbreRow::advanceBlink (double seconds)
{
    blink.advance (seconds);
}

float TimbreRow::blinkOpacityForTest() const noexcept
{
    // EXACTLY what `paint` reads. The ternary that used to be here —
    // `isRunning() ? value() : kBlinkOn` — had two branches returning the same
    // number, because `setRunning(false)` zeroes the phase and this track's 0%
    // stop IS `kBlinkOn`. `KeyframeLoop`'s own header states the hazard: a rest
    // value kept beside the curve is how the two hand-rolled drivers 08-04
    // replaced had diverged, and this was that shape in the one accessor the
    // checks read. /simplify.
    return blink.value();
}

void TimbreRow::selectionChanged()
{
    // css:426 is `.timbre.ciclo.active` — a CONJUNCTION. An unselected
    // CICLOTRON row is an ordinary row and a selected HI-FI row is too.
    blink.setRunning (isCiclotron() && isSelected());
}

void TimbreRow::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();
    const auto radius = lnf.cornerRadius();
    const auto& spec = timbreSpecs[static_cast<size_t> (index)];

    // `color-mix(in srgb, var(--panel) 70%, var(--active))` — css:422.
    g.setColour (isSelected() ? theme::mix (lnf.token (theme::Token::panel),
                                        lnf.token (theme::Token::active),
                                        side::kTimbreActiveMix)
                          : lnf.token (theme::Token::panel));
    g.fillRoundedRectangle (area.toFloat(), radius);

    g.setColour (lnf.token (isSelected() ? theme::Token::active : theme::Token::line));
    g.drawRoundedRectangle (area.toFloat().reduced (0.5f), radius, 1.0f);

    auto inner = area.reduced (side::kTimbrePadX + side::kBorder,
                               side::kTimbrePadY + side::kBorder);

    const auto led = ledBounds();

    inner.removeFromRight (side::kTimbreLedSize);

    const auto nameHeight = textBox (type::Style::timbreName);
    const auto subHeight  = textBox (type::Style::timbreSubLabel);

    auto labels = centredInRow (inner, inner.withHeight (nameHeight + subHeight));

    // THROUGH `CharPointer_UTF8`, like the sub-label below it. Every name in
    // this table was pure ASCII until 08-05 put the `™` on CICLOTRON's, and a
    // `juce::String` built from a `const char*` reads its bytes as LATIN-1 —
    // `juce_String.cpp:308`, the trap `tests/TestHarness.h` carries 228
    // literals' worth of scar tissue about.
    const auto name = juce::String (juce::CharPointer_UTF8 (spec.displayName));
    const auto nameBox = labels.removeFromTop (nameHeight).toFloat();

    // css:425 — `text-shadow: 1.2px 0 <danger 70%>, -1.2px 0 <triangulo 70%>`
    // on the SELECTED Ciclotron row only. CSS paints text shadows back to front
    // in reverse source order, so the cyan one listed second is furthest back
    // and the red one listed first sits between it and the glyphs.
    // `blink.isRunning()` IS the conjunction — `selectionChanged` sets it from
    // `isCiclotron() && isSelected()` and nothing else can. Recomputing it here
    // would be a second answer to one question.
    const auto aberrated = blink.isRunning();

    if (aberrated)
    {
        for (const auto& fringe : aberrationFringes (lnf))
        {
            g.setColour (fringe.colour);
            type::drawTracked (g, type::Style::timbreName, name,
                               nameBox.translated (fringe.offsetPx, 0.0f),
                               juce::Justification::centredLeft);
        }
    }

    g.setColour (lnf.token (theme::Token::fg));
    type::drawTracked (g, type::Style::timbreName, name, nameBox,
                       juce::Justification::centredLeft);

    // css:426 — `--danger`, blinking, on that same row and no other.
    g.setColour (aberrated
                     ? lnf.token (theme::Token::danger).withMultipliedAlpha (blink.value())
                     : lnf.token (theme::Token::fgFaint));

    type::drawTracked (g, type::Style::timbreSubLabel,
                       juce::String (juce::CharPointer_UTF8 (spec.subLabel)),
                       labels.toFloat(), juce::Justification::centredLeft);

    // `--line-strong` unlit; `--c-ganza` with a 6 px glow lit — css:428/429.
    if (isSelected())
        surface::glowDot (g, led, theme::accent (theme::Accent::ganza),
                          juce::roundToInt (side::kTimbreLedGlowRadius));
    else
    {
        g.setColour (lnf.token (theme::Token::lineStrong));
        g.fillEllipse (led.toFloat());
    }
}

} // namespace forrobox

#include "GainReductionMeter.h"

namespace forrobox
{

GainReductionMeter::GainReductionMeter (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setSize (grmeter::kWidth, grmeter::kHeight);
}

void GainReductionMeter::setReductionDb (float reductionDb, float seconds)
{
    const auto target = juce::jlimit (0.0f, grmeter::kRangeDb, reductionDb);
    const auto previous = displayedDb;

    if (target >= displayedDb)
    {
        // Up at once. The reading is the MAXIMUM since the last read, so a peak
        // that was smoothed on the way up is a peak that never appeared.
        displayedDb = target;
    }
    else
    {
        // Down linearly, full scale in kDecaySeconds — the CSS transition's
        // rate, not its restart-on-change semantics, which would make the fall
        // take 60 ms from wherever it happened to be and turn a slower poll
        // into a slower meter.
        const auto step = grmeter::kRangeDb * (seconds / grmeter::kDecaySeconds);
        displayedDb = juce::jmax (target, displayedDb - juce::jmax (0.0f, step));
    }

    if (! juce::approximatelyEqual (displayedDb, previous))
        repaint();
}

float GainReductionMeter::displayedProportion() const noexcept
{
    return juce::jlimit (0.0f, 1.0f, displayedDb / grmeter::kRangeDb);
}

void GainReductionMeter::paint (juce::Graphics& g)
{
    const auto box = getLocalBounds().toFloat();

    // `border-radius: 999px` — fully rounded, so the radius is half the height
    // rather than a number of its own. Fader's track records the same reading.
    const auto radius = box.getHeight() * 0.5f;

    g.setColour (lnf.token (theme::Token::screen));
    g.fillRoundedRectangle (box, radius);

    // `overflow: hidden` (css:514) is what keeps the fill inside the rounded
    // ground, and a rounded fill inside a rounded box is the same picture at
    // this size — the fill's right edge is the ground's right edge, and its
    // left edge is a 3 px radius against a 3 px radius.
    const auto proportion = displayedProportion();

    if (proportion > 0.0f)
    {
        const auto inner = box.reduced (static_cast<float> (grmeter::kBorder));
        const auto width = inner.getWidth() * proportion;

        g.setColour (lnf.token (theme::Token::danger));
        g.fillRoundedRectangle (inner.withLeft (inner.getRight() - width),
                                juce::jmin (radius, width * 0.5f));
    }

    g.setColour (lnf.token (theme::Token::line));
    g.drawRoundedRectangle (box.reduced (grmeter::kBorder * 0.5f), radius,
                            static_cast<float> (grmeter::kBorder));
}

} // namespace forrobox

#include "SequencerGrid.h"

#include "PluginProcessor.h"

namespace forrobox
{

int SequencerLayout::rowGap (int availableHeight) noexcept
{
    constexpr auto rows = ChassisLayout::kNumStrips;

    // What is left once the five pads have their fixed height, shared between
    // the four gaps. See seq::kDeclaredRowGap: the stylesheet's 7 does not fit,
    // and the pad height is the number that survives.
    const auto leftover = availableHeight - rows * seq::kPadHeight;

    return juce::jmax (0, leftover / (rows - 1));
}

juce::Rectangle<int> SequencerLayout::padBounds (juce::Rectangle<int> pads, int index,
                                                 int stepCount) noexcept
{
    if (stepCount <= 0 || ! juce::isPositiveAndBelow (index, stepCount))
        return {};

    // Fractional edges rounded, not a width multiplied by an index: at 32 steps
    // the strip does not divide evenly, and accumulating the remainder would put
    // every rounding error into the last pad. ChassisLayout::forBounds places
    // the five channel strips the same way.
    const auto gaps = static_cast<float> (seq::kPadGap * (stepCount - 1));
    const auto each = (static_cast<float> (pads.getWidth()) - gaps) / static_cast<float> (stepCount);

    const auto left = static_cast<float> (pads.getX())
                    + static_cast<float> (index) * (each + static_cast<float> (seq::kPadGap));

    return juce::Rectangle<int>::leftTopRightBottom (juce::roundToInt (left), pads.getY(),
                                                     juce::roundToInt (left + each),
                                                     pads.getBottom());
}

SequencerLayout SequencerLayout::forBounds (juce::Rectangle<int> bounds) noexcept
{
    SequencerLayout out;

    auto interior = bounds.reduced (seq::kPadSide, 0)
                        .withTrimmedTop (seq::kPadTop)
                        .withTrimmedBottom (seq::kPadBottom);

    // ── the head row: SEQUENCER + the isolate hint left, STEPS + 16/32 right ──
    const auto headHeight = flexRow (textBox (type::Style::sectionLabel),
                                     textBox (type::Style::seqHint),
                                     Button::heightOf (Button::Variant::base));

    out.head = interior.removeFromTop (headHeight);
    interior.removeFromTop (seq::kHeadMarginBottom);

    {
        auto row = out.head;

        // Right first: the two step buttons and their label are content-sized,
        // and the hint on the left takes what is left.
        const auto stepsWidth = Button::widthOf (Button::Variant::base, "32");

        out.steps32 = centredInRow (row, row.removeFromRight (stepsWidth)
                                             .withHeight (Button::heightOf (Button::Variant::base)));
        row.removeFromRight (seq::kStepsGap);
        out.steps16 = centredInRow (row, row.removeFromRight (stepsWidth)
                                             .withHeight (Button::heightOf (Button::Variant::base)));
        row.removeFromRight (seq::kStepsGap);

        const auto stepsLabelWidth = juce::roundToInt (
            type::trackedWidth (type::Style::seqHint, "STEPS"));

        out.stepsLabel = centredInRow (row, row.removeFromRight (stepsLabelWidth)
                                                .withHeight (textBox (type::Style::seqHint)));

        // Left: the section label, then the hint.
        const auto sectionWidth = juce::roundToInt (
            type::trackedWidth (type::Style::sectionLabel, "SEQUENCER"));

        out.sectionLabel = centredInRow (row, row.removeFromLeft (sectionWidth)
                                                  .withHeight (textBox (type::Style::sectionLabel)));
        row.removeFromLeft (seq::kHeadGap);

        const auto hintWidth = juce::roundToInt (
            type::trackedWidth (type::Style::seqHint, isolateHintText()));

        out.isolateHint = centredInRow (row, row.removeFromLeft (hintWidth)
                                                 .withHeight (textBox (type::Style::seqHint)));
    }

    // ── five rows, the gap derived from what the region left ─────────────────
    const auto gap = rowGap (interior.getHeight());

    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        auto& row = out.rows[static_cast<size_t> (i)];

        row.bounds = interior.removeFromTop (seq::kPadHeight);

        if (i < ChassisLayout::kNumStrips - 1)
            interior.removeFromTop (gap);

        auto cells = row.bounds;

        row.label = cells.removeFromLeft (seq::kLabelWidth);
        cells.removeFromLeft (seq::kLabelGap);
        row.pads = cells;

        auto label = row.label;

        row.chip = centredInRow (label, label.removeFromLeft (seq::kChipWidth)
                                             .withHeight (seq::kChipHeight));
        label.removeFromLeft (seq::kChipGap);
        row.name = centredInRow (label, label.withHeight (
                                            textBox (type::Style::sequencerRowLabel)));
    }

    return out;
}

const juce::String& isolateHintText()
{
    // app.js:319, in Brazilian Portuguese as every instructional string is.
    static const juce::String text { juce::CharPointer_UTF8 ("CLIQUE O NOME P/ ISOLAR") };
    return text;
}

SequencerGrid::SequencerGrid (ForroBoxLookAndFeel& lookAndFeelToUse)
    : lnf (lookAndFeelToUse)
{
    setOpaque (true);
}

SequencerGrid::~SequencerGrid() = default;

void SequencerGrid::attachParameters (juce::AudioProcessorValueTreeState& apvts)
{
    juce::ignoreUnused (apvts);

    // Task 3 fills this. The region is reserved first, deliberately: 04-01
    // computed the strip's content rect and discarded it, which made its own
    // "reserve their boxes" deliverable unreachable by the plans that needed it,
    // and 04-02 paid to redo the work.
    resized();
}

void SequencerGrid::resized()
{
    layout = SequencerLayout::forBounds (getLocalBounds());
}

void SequencerGrid::paint (juce::Graphics& g)
{
    const auto area = getLocalBounds();

    g.setColour (lnf.token (theme::Token::sunken));
    g.fillRect (area);

    const auto shadows = lnf.shadows();
    surface::wellShadow (g, area, shadows.wellShadow, shadows.wellRadius);

    g.setColour (lnf.token (theme::Token::line));
    g.fillRect (area.getX(), area.getY(), area.getWidth(), 1);

    const auto clip = g.getClipBounds();

    paintHeadRow (g, clip);
    paintRowLabels (g, clip);
}

void SequencerGrid::paintHeadRow (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    g.setColour (lnf.token (theme::Token::fgFaint));

    if (layout.sectionLabel.intersects (clip))
        type::drawTracked (g, type::Style::sectionLabel, "SEQUENCER",
                           layout.sectionLabel.toFloat(), juce::Justification::centredLeft);

    if (layout.isolateHint.intersects (clip))
        type::drawTracked (g, type::Style::seqHint, isolateHintText(),
                           layout.isolateHint.toFloat(), juce::Justification::centredLeft);

    if (layout.stepsLabel.intersects (clip))
        type::drawTracked (g, type::Style::seqHint, "STEPS",
                           layout.stepsLabel.toFloat(), juce::Justification::centredLeft);
}

void SequencerGrid::paintRowLabels (juce::Graphics& g, juce::Rectangle<int> clip) const
{
    for (int i = 0; i < ChassisLayout::kNumStrips; ++i)
    {
        const auto& row = layout.rows[static_cast<size_t> (i)];

        if (! row.label.intersects (clip))
            continue;

        // The same accent binding the strips use, protected by the static_assert
        // at the top of Chassis.h.
        g.setColour (theme::accent (static_cast<theme::Accent> (i)));
        g.fillRoundedRectangle (row.chip.toFloat(), static_cast<float> (seq::kChipRadius));

        // `--fg-dim` at rest; 05-03's isolate raises it to `--fg`.
        g.setColour (lnf.token (theme::Token::fgDim));
        type::drawTracked (g, type::Style::sequencerRowLabel,
                           juce::String (juce::CharPointer_UTF8 (
                               ids::channelInfos[static_cast<size_t> (i)].displayName)),
                           row.name.toFloat(), juce::Justification::centredLeft);
    }
}

} // namespace forrobox

/* ============================================================================
   FORRÓ BOX — test suite entry points

   Declared in one place so the runner and each suite agree, and so neither
   definition is a bare global function (Clang's -Wmissing-prototypes).
============================================================================ */
#pragma once

#include <juce_core/juce_core.h>

void runStateTests();
void runClockTests();
void runVoiceTests();

/** Not a suite: renders each profile to a WAV for A/B listening. */
void renderAuditionFiles (const juce::String& outputDirectory);

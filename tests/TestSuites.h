/* ============================================================================
   FORRÓ BOX — test suite entry points

   Declared in one place so the runner and each suite agree, and so neither
   definition is a bare global function (Clang's -Wmissing-prototypes).
============================================================================ */
#pragma once

void runStateTests();
void runClockTests();

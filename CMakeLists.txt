# ============================================================================
#  Forró Box — a rhythm-production instrument plugin for Brazilian forró
#  percussion. VST3 + Standalone, built on JUCE 8.
# ============================================================================
cmake_minimum_required(VERSION 3.22)

project(ForroBox VERSION 0.1.0 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Single-config generators default to an unoptimised, unspecified build; pin
# Release so an accidental bare configure never produces a debug plugin.
get_property(_is_multi_config GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT _is_multi_config AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

# ── JUCE acquisition ────────────────────────────────────────────────────────
#  JUCE_PATH short-circuits to a checkout already on the machine. Unset, the
#  build fetches the pinned tag instead, so a fresh clone works untouched.
#  The tag is pinned literally — tracking a branch turns any future build
#  failure into an unbounded bisect.
set(JUCE_PATH "" CACHE PATH "Path to a local JUCE checkout (optional)")
set(FORROBOX_JUCE_TAG "8.0.12")

if(JUCE_PATH AND EXISTS "${JUCE_PATH}/CMakeLists.txt")
  message(STATUS "JUCE: using local ${JUCE_PATH}")
  add_subdirectory("${JUCE_PATH}" "${CMAKE_BINARY_DIR}/JUCE" EXCLUDE_FROM_ALL)
else()
  if(JUCE_PATH)
    # A non-empty JUCE_PATH is an assertion that a checkout is there. Falling
    # back to a network clone would bury a typo behind minutes of cloning, or
    # an opaque git error on an air-gapped machine.
    message(FATAL_ERROR "JUCE_PATH is set to '${JUCE_PATH}' but contains no CMakeLists.txt. "
                        "Fix the path, or leave JUCE_PATH unset to fetch ${FORROBOX_JUCE_TAG}.")
  endif()
  message(STATUS "JUCE: fetching ${FORROBOX_JUCE_TAG}")
  include(FetchContent)
  FetchContent_Declare(JUCE
    GIT_REPOSITORY https://github.com/juce-framework/JUCE.git
    GIT_TAG        ${FORROBOX_JUCE_TAG}
    GIT_SHALLOW    TRUE)
  FetchContent_MakeAvailable(JUCE)
endif()

# ── Plugin target ───────────────────────────────────────────────────────────
#  Every identifier and display string here is ASCII: artifact paths, ldd and
#  MSVC never meet a non-ASCII byte, and neither does the VST3 metadata.
juce_add_plugin(ForroBox
  # ASCII, deliberately. JUCE's VST3 module-info writer converts these two
  # narrow char* defines with `infoW.fromAscii()` (juce_VST3ModuleInfo.h:296),
  # which sign-extends each byte — UTF-8 "ó" (c3 b3) came out as U+FFC3 U+FFB3,
  # so a scanning host displayed "Forrￃﾳ Box". Parameter and group names are
  # unaffected: those travel toString128's juce::String overload, which converts
  # UTF-16 correctly, so CACHAÇA / TRIÂNGULO / GANZÁ keep their accents.
  COMPANY_NAME                  "Forro Box"
  BUNDLE_ID                     "com.forrobox.ForroBox"
  PLUGIN_MANUFACTURER_CODE      Frbx
  PLUGIN_CODE                   Frbx
  FORMATS                       VST3 Standalone
  PRODUCT_NAME                  "ForroBox"
  PLUGIN_NAME                   "Forro Box"
  IS_SYNTH                      TRUE
  NEEDS_MIDI_INPUT              TRUE
  NEEDS_MIDI_OUTPUT             TRUE
  IS_MIDI_EFFECT                FALSE
  EDITOR_WANTS_KEYBOARD_FOCUS   TRUE
  COPY_PLUGIN_AFTER_BUILD       FALSE)

juce_generate_juce_header(ForroBox)

target_sources(ForroBox PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/ForroBoxState.cpp
  src/Profiles.cpp)

target_compile_definitions(ForroBox PUBLIC
  # The WebView GUI path is out of scope (PROJECT.md); leaving the browser on
  # drags in webkit2gtk for nothing.
  JUCE_WEB_BROWSER=0
  JUCE_USE_CURL=0
  JUCE_VST3_CAN_REPLACE_VST2=0)
  # JUCE_DISPLAY_SPLASH_SCREEN deliberately omitted: JUCE 8 dropped the splash
  # screen and the flag now only emits a #pragma message on every build.

target_link_libraries(ForroBox
  PRIVATE
    juce::juce_audio_utils
    juce::juce_dsp
  PUBLIC
    juce::juce_recommended_config_flags
    juce::juce_recommended_warning_flags)

# LTO is a distribution concern, not a "don't accidentally ship Debug" one. Tied
# to the Release default above it charged every edit-build-test cycle a full
# serial LTRANS pass (~69 s on this scaffold) for a binary with no shipping
# stakes. Opt in when packaging: -DFORROBOX_LTO=ON
option(FORROBOX_LTO "Enable LTO (for distributable builds)" OFF)
if(FORROBOX_LTO)
  target_link_libraries(ForroBox PUBLIC juce::juce_recommended_lto_flags)
endif()

# ── Musical-content cross-check ──────────────────────────────────────────────
#  A wrong digit in a groove table produces no crash, no failed build and no
#  failing test. This compares src/Profiles.cpp against data.js, the design
#  source of truth. Wired as a build dependency so it cannot rot unnoticed —
#  a comment claiming the tables are "verified" is worthless if nothing runs it.
option(FORROBOX_REQUIRE_PROFILE_CHECK
       "Fail configuration if the groove cross-check cannot run" ON)

find_package(Python3 COMPONENTS Interpreter QUIET)

if(NOT Python3_Interpreter_FOUND)
  if(FORROBOX_REQUIRE_PROFILE_CHECK)
    # Deliberately fatal. A warning here would turn "cannot build with diverged
    # groove tables" into "verified only if Python happens to be installed" —
    # silently, behind a log line. That is the same class of silent divergence
    # this gate exists to prevent, moved one layer up. Opt out explicitly with
    # -DFORROBOX_REQUIRE_PROFILE_CHECK=OFF if you really mean to.
    message(FATAL_ERROR
            "Python3 not found, so the data.js groove cross-check cannot run. "
            "The musical content would be unverified. Install Python 3, or pass "
            "-DFORROBOX_REQUIRE_PROFILE_CHECK=OFF to build without the check.")
  else()
    message(WARNING "Groove cross-check DISABLED by request — the musical content is unverified")
  endif()
else()
  set(_fb_profile_stamp "${CMAKE_CURRENT_BINARY_DIR}/verify-profiles.stamp")

  # A stamp with DEPENDS, not add_custom_target(ALL): the latter re-ran the
  # script on every build, including no-op rebuilds and edits to unrelated
  # files — measured ~44 ms each time, ~41 ms of it bare interpreter startup.
  add_custom_command(
    OUTPUT "${_fb_profile_stamp}"
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-profiles.py"
    COMMAND "${CMAKE_COMMAND}" -E touch "${_fb_profile_stamp}"
    DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-profiles.py"
            "${CMAKE_CURRENT_SOURCE_DIR}/data.js"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/Profiles.cpp"
            "${CMAKE_CURRENT_SOURCE_DIR}/src/ParameterIDs.h"
    # No WORKING_DIRECTORY: MSBuild runs custom commands through cmd.exe, which
    # refuses a UNC path as the current directory — and the Windows build's
    # source dir IS a UNC path. The script resolves paths from __file__.
    COMMENT "Verifying groove tables against data.js"
    VERBATIM)

  add_custom_target(verify-profiles ALL DEPENDS "${_fb_profile_stamp}")
  add_dependencies(ForroBox verify-profiles)
endif()

# ── Tests ───────────────────────────────────────────────────────────────────
#  A console app linking the plugin's shared code so the processor can be
#  constructed directly — no GUI, no audio device. Deliberately not given the
#  LTO flags: this target is built to be run often.
option(FORROBOX_TESTS "Build the test target" ON)
if(FORROBOX_TESTS)
  # Deliberately a plain executable rather than juce_add_console_app: that helper
  # forces JUCE_STANDALONE_APPLICATION=1, which collides with the =0 the plugin's
  # shared code exports, warning on every translation unit. We supply our own
  # main(), so the helper's scaffolding buys nothing.
  add_executable(ForroBoxTests tests/StateRoundTripTest.cpp)
  target_link_libraries(ForroBoxTests
    PRIVATE
      ForroBox
      juce::juce_audio_utils
      juce::juce_dsp
    PUBLIC
      juce::juce_recommended_config_flags
      juce::juce_recommended_warning_flags)
  target_include_directories(ForroBoxTests PRIVATE
    "${CMAKE_CURRENT_BINARY_DIR}/ForroBox_artefacts/JuceLibraryCode"
    "${CMAKE_CURRENT_SOURCE_DIR}/src")
endif()

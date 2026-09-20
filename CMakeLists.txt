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

# ── Embedded samples ────────────────────────────────────────────────────────
#  The zabumba plays four user-supplied one-shots (Phase 3's hybrid engine
#  decision). Embedded rather than loaded from a path: a runtime path breaks the
#  moment the plugin is installed anywhere but the machine that built it.
#  ~456 KB total. The archive's four tempo-locked loops are deliberately absent
#  — see assets/samples/README.md.
#
#  This is juce_add_binary_data (JUCEUtils.cmake:452) with ONE line dropped:
#  its `WORKING_DIRECTORY ${CMAKE_CURRENT_LIST_DIR}`. That line breaks the
#  Windows build outright —
#
#    '\\wsl.localhost\Ubuntu-24.04\home\bidu\forrobox'
#    CMD does not support UNC paths as current directories.
#    error MSB8066: custom build for BinaryData1.cpp.rule exited with code 1
#
#  — because MSBuild runs custom commands through cmd.exe and this project's
#  Windows source dir IS a UNC path. It is the same trap the groove cross-check
#  below already documents. Dropping it is safe rather than a workaround: every
#  path handed to juceaide is absolute already (the input list is written with
#  absolute names, the output folder is under CMAKE_CURRENT_BINARY_DIR), so the
#  command never resolves anything against its working directory.
#
#  Inlined rather than patched upstream: ~/JUCE is consumed read-only.
function(forrobox_add_binary_data target)
  cmake_parse_arguments(FB_ARG "" "NAMESPACE;HEADER_NAME" "SOURCES" ${ARGN})

  if(NOT FB_ARG_SOURCES)
    message(FATAL_ERROR "forrobox_add_binary_data needs at least one file to encode")
  endif()

  # JUCE's defaults, restored. Dropping them was drift, not part of the
  # WORKING_DIRECTORY fix: without HEADER_NAME the output list below would
  # append a bare directory and hand juceaide an empty header name.
  if(NOT FB_ARG_NAMESPACE)
    set(FB_ARG_NAMESPACE BinaryData)
  endif()

  if(NOT FB_ARG_HEADER_NAME)
    set(FB_ARG_HEADER_NAME BinaryData.h)
  endif()

  add_library(${target} STATIC)

  # The binary-data target is a static library linked into a shared-object
  # plugin, which the linker rejects without PIC.
  set_target_properties(${target} PROPERTIES POSITION_INDEPENDENT_CODE TRUE)

  set(_fb_data_dir "${CMAKE_CURRENT_BINARY_DIR}/forrobox_binarydata_${target}")
  file(MAKE_DIRECTORY "${_fb_data_dir}")

  # juceaide emits one BinaryData<N>.cpp per input file, plus the header.
  set(_fb_outputs)
  list(LENGTH FB_ARG_SOURCES _fb_count)
  foreach(index RANGE 1 ${_fb_count})
    list(APPEND _fb_outputs "${_fb_data_dir}/BinaryData${index}.cpp")
  endforeach()
  list(APPEND _fb_outputs "${_fb_data_dir}/${FB_ARG_HEADER_NAME}")

  # Absolute paths, one per line — the same contract juceaide expects from
  # JUCE's own helper.
  set(_fb_list_contents)
  set(_fb_absolute_sources)
  foreach(name IN LISTS FB_ARG_SOURCES)
    get_filename_component(_fb_absolute "${name}" ABSOLUTE)
    list(APPEND _fb_absolute_sources "${_fb_absolute}")
    string(APPEND _fb_list_contents "${_fb_absolute}\n")
  endforeach()

  # Written only when it changes, so the custom command does not re-run on
  # every configure.
  set(_fb_list_file "${_fb_data_dir}/input_file_list")
  set(_fb_previous "")
  if(EXISTS "${_fb_list_file}")
    file(READ "${_fb_list_file}" _fb_previous)
  endif()
  if(NOT "${_fb_previous}" STREQUAL "${_fb_list_contents}")
    file(WRITE "${_fb_list_file}" "${_fb_list_contents}")
  endif()

  add_custom_command(OUTPUT ${_fb_outputs}
    COMMAND juce::juceaide binarydata "${FB_ARG_NAMESPACE}" "${FB_ARG_HEADER_NAME}"
            "${_fb_data_dir}" "${_fb_list_file}"
    # No WORKING_DIRECTORY — see the note above.
    DEPENDS "${_fb_list_file}" ${_fb_absolute_sources}
    COMMENT "Embedding ${_fb_count} sample(s) into ${target}"
    VERBATIM)

  target_sources(${target} PRIVATE ${_fb_outputs})
  target_include_directories(${target} INTERFACE "${_fb_data_dir}")
  target_compile_features(${target} PRIVATE cxx_std_17)

  # Also dropped by accident, and NOT related to UNC paths. The generated
  # JuceHeader.h guards `#include "BinaryData.h"` behind exactly this define
  # (juceaide's Main.cpp), so without it the documented route to BinaryData is
  # silently dead — this built only because ZabumbaSampler.cpp includes the
  # header directly.
  target_compile_definitions(${target} INTERFACE JUCE_TARGET_HAS_BINARY_DATA=1)

  # JUCE's own comment: "This fixes an issue where Xcode is unable to find
  # binary data during archive." Unrelated to UNC paths; kept so a macOS
  # archive build of the VST3 does not break for a reason we removed.
  if(CMAKE_GENERATOR STREQUAL "Xcode")
    set_target_properties(${target} PROPERTIES ARCHIVE_OUTPUT_DIRECTORY "./")
  endif()
endfunction()

forrobox_add_binary_data(ForroBoxSamples
  HEADER_NAME BinaryData.h
  NAMESPACE   BinaryData
  SOURCES
    assets/samples/ZAB_LOW_01.wav
    assets/samples/ZAB_LOW_02.wav
    assets/samples/ZAB_LOW_03.wav
    assets/samples/ZAB_LOW_04.wav)

# A SECOND target rather than seven more SOURCES on the one above. BinaryData.h
# is included by ZabumbaSampler.cpp, so folding the fonts in would recompile the
# sampler and the whole audio path on any font change -- and the two sets have
# nothing to do with each other beyond both being bytes.
#
# The four Space Grotesk files are built by scripts/build-fonts.py, not
# downloaded: upstream publishes that family as a variable font only and JUCE
# cannot select a variable axis, so the statics are instanced offline. The three
# IBM Plex Mono files are upstream statics, unmodified. Both OFL licences ship
# beside them, as the compliance constraint requires.
forrobox_add_binary_data(ForroBoxFonts
  HEADER_NAME FontData.h
  NAMESPACE   FontData
  SOURCES
    assets/fonts/SpaceGrotesk-Regular.ttf
    assets/fonts/SpaceGrotesk-Medium.ttf
    assets/fonts/SpaceGrotesk-SemiBold.ttf
    assets/fonts/SpaceGrotesk-Bold.ttf
    assets/fonts/IBMPlexMono-Regular.ttf
    assets/fonts/IBMPlexMono-Medium.ttf
    assets/fonts/IBMPlexMono-SemiBold.ttf
    assets/fonts/SpaceGrotesk-OFL.txt
    assets/fonts/IBMPlexMono-OFL.txt)

target_sources(ForroBox PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/ForroBoxState.cpp
  src/Profiles.cpp
  src/Clock.cpp
  src/PatternSnapshot.cpp
  src/Voices.cpp
  src/VoiceEngine.cpp
  src/ZabumbaSampler.cpp
  src/MixBus.cpp
  src/Theme.cpp
  src/Typography.cpp
  src/LookAndFeel.cpp
  src/Button.cpp
  src/StepPad.cpp
  src/Fader.cpp
  src/ValueScreen.cpp
  src/BpmField.cpp
  src/BpmAttachment.cpp
  src/Segmented.cpp
  src/LogoMark.cpp
  src/ToggleAttachment.cpp
  src/ChoiceAttachment.cpp
  src/ChoiceButtonsAttachment.cpp
  src/Knob.cpp
  src/KnobAttachment.cpp
  src/ValueTooltip.cpp
  src/Surface.cpp
  src/GainReductionMeter.cpp
  src/DragMidiButton.cpp
  src/HeaderBar.cpp
  src/FooterBar.cpp
  src/PatternPads.cpp
  src/SequencerGrid.cpp
  src/ProfileButton.cpp
  src/SidePanel.cpp
  src/TimbreRow.cpp
  src/HitVisualiser.cpp
  src/KitOverlay.cpp
  src/Playhead.cpp
  src/StepSnapshot.cpp
  src/Chassis.cpp)

# ── Source and execution charset ────────────────────────────────────────────
#  The accented strings in src/ are written as the characters they mean. That
#  is only safe once the compiler is TOLD how to read them, so it is pinned
#  here rather than worked around at each literal.
#
#  MSVC is the flag that is load-bearing: without /utf-8 it reads the source as
#  the system ANSI code page and mangles every accent. GCC and Clang already
#  default to UTF-8 for both charsets — pinned anyway, so the build does not
#  depend on a locale nobody set deliberately.
#
#  This replaces four local fixes of one bug. Accented text used to be spelled
#  as \xNN escapes, and the escape is GREEDY: "m\xc3\xa9dio" reads \xa9d as a
#  three-digit escape and is out of range, so Clang refused it, GCC truncated it
#  to \x9d silently, and verify-profiles.py could not see it at all because it
#  compares source text. Each earlier fix split the literal by hand and made the
#  class detectable; this makes it unrepresentable.
#
#  NOT u8"...": this project is C++20, where a u8 literal is const char8_t* and
#  will not bind to the const char* fields in ChannelInfo, ProfileInfo and
#  timbreSpecs.
#
#  PUBLIC so the test target inherits it. The test executable already takes
#  juce_recommended_config_flags transitively for exactly this reason, and a
#  second copy of these flags would be free to drift out of step.
#
#  Gated on the MSVC VARIABLE, not on CXX_COMPILER_ID. clang-cl reports its id
#  as "Clang" while taking CL-style arguments, so an id test would hand it
#  -finput-charset (which its driver reports as an unknown argument and IGNORES,
#  with only a warning) and never pass /utf-8 — reinstating the exact mangling
#  this block exists to prevent. CMake sets MSVC for clang-cl too. /code-review.
#
#  And the GNU-style pair is probed rather than assumed: -fexec-charset= was not
#  an accepted driver option before Clang 18, where it is a hard build failure
#  rather than a fallback. They buy nothing on a toolchain that already defaults
#  to UTF-8, so a version cliff for them would be a bad trade. /code-review.
if(MSVC)
  target_compile_options(ForroBox PUBLIC /utf-8)
else()
  # ONE probe for the pair, not two. Splitting them only changes behaviour on
  # Clang <18, which accepts -finput-charset and rejects -fexec-charset — and
  # there -finput-charset alone is a no-op, since these toolchains already
  # default to UTF-8 for both. Two probes bought a partial-support path whose
  # payload is nothing. /simplify.
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag("-finput-charset=UTF-8 -fexec-charset=UTF-8"
                          FORROBOX_HAS_CHARSET_FLAGS)

  if(FORROBOX_HAS_CHARSET_FLAGS)
    target_compile_options(ForroBox PUBLIC -finput-charset=UTF-8 -fexec-charset=UTF-8)
  endif()
endif()

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
    # PUBLIC, not PRIVATE: ZabumbaSampler.cpp is compiled into ForroBox, but the
    # test target includes the same headers and needs BinaryData.h on its
    # include path too.
    ForroBoxSamples
    ForroBoxFonts
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
  # One function rather than two copies of the same block — the same reason
  # forrobox_add_test_target exists, and the same hazard: the stamp mechanism
  # has already been revised once (see the comment inside), and a hand-copied
  # second block means the next revision has to be made twice, or drifts.
  function(forrobox_add_verify_target name script comment)
    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${name}.stamp")

    # A stamp with DEPENDS, not add_custom_target(ALL): the latter re-ran the
    # script on every build, including no-op rebuilds and edits to unrelated
    # files — measured ~44 ms each time, ~41 ms of it bare interpreter startup.
    add_custom_command(
      OUTPUT "${_stamp}"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}"
      COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}" ${ARGN}
      # No WORKING_DIRECTORY: MSBuild runs custom commands through cmd.exe,
      # which refuses a UNC path as the current directory — and the Windows
      # build's source dir IS a UNC path. The scripts resolve from __file__.
      COMMENT "${comment}"
      VERBATIM)

    add_custom_target(${name} ALL DEPENDS "${_stamp}")
    add_dependencies(ForroBox ${name})
  endfunction()

  forrobox_add_verify_target(verify-profiles verify-profiles.py
    "Verifying groove tables against data.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/data.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Profiles.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ParameterIDs.h")

  # ── Design-token cross-check ───────────────────────────────────────────────
  #  The same argument one layer over: a wrong hex digit in src/Theme.h is not a
  #  crash or a failed test, it is a colour that is subtly wrong with no way to
  #  tell which digit. This compares both themes against forrobox.css (and the
  #  bateria kit colours against app.js's SUBCOLORS), in both directions.
  #
  #  Shares the Python gate above deliberately: the two checks fail the same way
  #  and a second opt-out flag would let one rot while the other looked healthy.
  #
  #  Theme.cpp is a dependency as well as Theme.h: the two raised-edge highlight
  #  recipes are read out of shadowsFor(), so without it an edit to those values
  #  would not re-run the check that pins them.
  forrobox_add_verify_target(verify-theme verify-theme.py
    "Verifying design tokens against forrobox.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Theme.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/forrobox.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/app.js")

  # ── Strip-geometry cross-check ─────────────────────────────────────────────
  #  The same instrument again, for the box model instead of the colours.
  #  Added at 04-02 because three negative controls showed that the ~350 C++
  #  assertions over the strip stack cannot police its NUMBERS: the tests
  #  compare the derived layout against the same constants the layout is built
  #  from, so changing kStripPadTop moves both sides together. The C++ suite
  #  owns ORDER and non-overlap; this owns the lengths.
  forrobox_add_verify_target(verify-geometry verify-geometry.py
    "Verifying strip geometry against forrobox.css"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Chassis.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Knob.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/forrobox.css")

  # ── Source-charset cross-check ─────────────────────────────────────────────
  #  06-04 stopped spelling accented text as `\xNN` escapes and pinned the
  #  charset above instead. That deleted the greedy-escape bug for good, but an
  #  escape produces exact bytes under ANY assumed charset and a real UTF-8
  #  literal does not — so the literals need a guard they did not need before.
  #
  #  The runtime check in tests/UiTest.cpp is not that guard: it walks
  #  ids::profileInfos and forrobox::timbreSpecs and nothing else, while 06-04
  #  converted ~19 further literals. This scans EVERY literal under src/. The
  #  argument, and the mutation that proved the gap, are in the script itself.
  #
  #  CONFIGURE_DEPENDS so a newly added source is scanned without a manual
  #  reconfigure. A check whose coverage silently stops at the files that existed
  #  when it was wired is the enrolment failure this project already fixed once.
  file(GLOB_RECURSE FORROBOX_SRC_FOR_CHARSET CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")

  #  tests/UiTest.cpp is a dependency as well as the sources: the script asserts
  #  its own repertoire against the `allowed` code-point array THERE, so without
  #  it an edit to that array would not re-run the check that pins the two
  #  together. Same reason verify-theme takes Theme.cpp as well as Theme.h.
  forrobox_add_verify_target(verify-charset verify-charset.py
    "Verifying every src/ literal against the drawn character repertoire"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/UiTest.cpp"
    ${FORROBOX_SRC_FOR_CHARSET})
endif()

# ── Tests ───────────────────────────────────────────────────────────────────
#  Console apps linking the plugin's shared code so the processor and the clock
#  can be constructed directly — no GUI, no audio device. Deliberately not given
#  the LTO flags: these targets are built to be run often.
option(FORROBOX_TESTS "Build the test targets" ON)
if(FORROBOX_TESTS)
  # One function rather than two copies of the same block. A second hand-copied
  # copy is how the two targets drift apart — which is exactly the failure the
  # cross-check above exists to catch in the groove data.
  function(forrobox_add_test_target target)
    # Deliberately a plain executable rather than juce_add_console_app: that
    # helper forces JUCE_STANDALONE_APPLICATION=1, which collides with the =0 the
    # plugin's shared code exports, warning on every translation unit. We supply
    # our own main(), so the helper's scaffolding buys nothing.
    add_executable(${target} ${ARGN})
    target_link_libraries(${target}
      PRIVATE
        ForroBox
        juce::juce_audio_utils
        juce::juce_dsp)
    # juce_recommended_config_flags / _warning_flags are not repeated here:
    # ForroBox links them PUBLIC, so they arrive transitively. A second copy
    # would be free to drift out of step with the plugin target.
    target_include_directories(${target} PRIVATE
      "$<TARGET_PROPERTY:ForroBox,JUCE_GENERATED_SOURCES_DIRECTORY>"
      "${CMAKE_CURRENT_SOURCE_DIR}/src"
      "${CMAKE_CURRENT_SOURCE_DIR}/tests")

    # On the TEST target only. juce::ParameterAttachment delivers its
    # parameter -> UI update through an AsyncUpdater, so proving that a knob
    # follows host automation needs the message queue drained — and
    # MessageManager::runDispatchLoopUntil is compiled out unless this is set.
    #
    # Deliberately NOT set on the plugin: modal loops in a plugin are exactly
    # what the default forbids, and 03-03's lesson is that a rig default gets
    # mistaken for the product. This changes what the TESTS can do, never what
    # the plugin does.
    target_compile_definitions(${target} PRIVATE JUCE_MODAL_LOOPS_PERMITTED=1)
  endfunction()

  # One executable, two suites. A second executable would re-compile the entire
  # JUCE module set — juce_audio_utils and juce_dsp are INTERFACE targets that
  # inject their sources into each consumer — costing ~26s of every clean build
  # and 20 MB of duplicate objects, and rather more under MSVC. TestHarness.h's
  # inline counters already share one tally across translation units.
  forrobox_add_test_target(ForroBoxTests
    tests/TestMain.cpp
    tests/StateRoundTripTest.cpp
    tests/ClockTest.cpp
    tests/VoiceTest.cpp
    tests/UiTest.cpp)
endif()

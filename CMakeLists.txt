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
    assets/fonts/JetBrainsMono-Regular.ttf
    assets/fonts/JetBrainsMono-Medium.ttf
    assets/fonts/SpaceMono-Regular.ttf
    assets/fonts/SpaceGrotesk-OFL.txt
    assets/fonts/IBMPlexMono-OFL.txt
    assets/fonts/JetBrainsMono-OFL.txt
    assets/fonts/SpaceMono-OFL.txt)

target_sources(ForroBox PRIVATE
  src/PluginProcessor.cpp
  src/PluginEditor.cpp
  src/ForroBoxState.cpp
  src/Settings.cpp
  src/Profiles.cpp
  src/Clock.cpp
  src/PatternSnapshot.cpp
  src/Voices.cpp
  src/VoiceEngine.cpp
  src/MidiExport.cpp
  src/GrooveExport.cpp
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
  src/GearButton.cpp
  src/NoteGlyph.cpp
  src/AboutOverlay.cpp
  src/EffectOverlay.cpp
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
  #  ARGS and AFTER exist so the fifth gate is a CALL rather than a copy.
  #  verify-midi has to run AFTER ForroBoxTests links, because it asks the C++
  #  side for its bytes — so it cannot be a dependency OF ForroBox like the
  #  other four, which would be a cycle. That is one word of variation, and the
  #  comment below already predicts what a hand-copied second block costs.
  #    ARGS  — extra arguments passed to the script.
  #    AFTER — targets the check runs after; also suppresses the ForroBox
  #            dependency, since the two are mutually exclusive.
  #  File dependencies stay positional, so the four existing calls are unchanged.
  function(forrobox_add_verify_target name script comment)
    cmake_parse_arguments(FV "" "" "ARGS;AFTER" ${ARGN})

    set(_stamp "${CMAKE_CURRENT_BINARY_DIR}/${name}.stamp")

    # A stamp with DEPENDS, not add_custom_target(ALL): the latter re-ran the
    # script on every build, including no-op rebuilds and edits to unrelated
    # files — measured ~44 ms each time, ~41 ms of it bare interpreter startup.
    add_custom_command(
      OUTPUT "${_stamp}"
      COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}" ${FV_ARGS}
      COMMAND "${CMAKE_COMMAND}" -E touch "${_stamp}"
      DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/scripts/${script}"
              ${FV_UNPARSED_ARGUMENTS} ${FV_AFTER}
      # No WORKING_DIRECTORY: MSBuild runs custom commands through cmd.exe,
      # which refuses a UNC path as the current directory — and the Windows
      # build's source dir IS a UNC path. The scripts resolve from __file__.
      COMMENT "${comment}"
      VERBATIM)

    add_custom_target(${name} ALL DEPENDS "${_stamp}")

    if(NOT FV_AFTER)
      add_dependencies(ForroBox ${name})
    endif()
  endfunction()

  #  src/MixBus.h IS AN INPUT, and it was undeclared until 08-01 UNIFY.
  #  `check_timbres` (verify-profiles.py:260) reads `timbreSpecs` out of it to
  #  compare the TIMBRE table against data.js, so editing that table did not
  #  re-run the gate whose entire justification is that a wrong digit here
  #  produces no crash, no failed build and no failing test. Exactly the
  #  enrolment failure verify-geometry's block below was written for at 06-05,
  #  and the one 08-01 fixed in verify-charset — third instance, found by
  #  /simplify's altitude pass. See the deferred note in PROJECT.md: the class
  #  is only really closed by scraping the script's own bindings, the way
  #  verify-geometry already does.
  forrobox_add_verify_target(verify-profiles verify-profiles.py
    "Verifying groove tables against assets/profiles.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/profiles.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Profiles.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MixBus.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ParameterIDs.h"
    # 09-02 reads a SECOND constant out of this header — kMaxGroovesPerProfile,
    # which drives the bank-size refusal — so editing it must re-run the gate
    # that consumes it. /code-review.
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Profiles.h")

  # ── The generated copies of the grooves ───────────────────────────────────
  #
  #  `assets/profiles.json` is the source; `src/Profiles.cpp`'s table and
  #  `data.js`'s PROFILES block are written from it. This re-derives both and
  #  fails if either has drifted.
  #
  #  A SEPARATE GATE from verify-profiles above, and the split is the point.
  #  This one asks "do the generated files match their source"; that one asks
  #  "does the C++ the plugin builds against match the data, field by field,
  #  and do the timbres still match data.js". A generator could be rewritten to
  #  emit something self-consistent and wrong, and only the second would see it.
  #
  #  THE STANDALONE PAGE IS CHECKED TOO, and that was a late correction. It was
  #  first left out on the grounds that `build_standalone.py` owns it and
  #  enrolling it would make every build run the bundler — which is false: the
  #  page inlines data.js verbatim, so the same rendering splices into it and no
  #  bundler is needed for a groove-only change. And it is the ONE consumer a
  #  stale copy reaches a user through: it is committed, and README-prototype.md
  #  tells people to open it.
  forrobox_add_verify_target(verify-generated-profiles build-profiles.py
    "Verifying the generated groove tables against assets/profiles.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/assets/profiles.json"
    "${CMAKE_CURRENT_SOURCE_DIR}/data.js"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Profiles.cpp"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/ParameterIDs.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/Forró Box (standalone).html"
    # build-profiles.py IMPORTS verify-profiles.py for validate(), load_profiles()
    # and TIMBRE_INDEX — which is derived from MixBus.h. Neither was declared, so
    # a new rule inside validate() left this gate stale. Sixth instance of the
    # enrolment failure the comment above records. /code-review.
    "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-profiles.py"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/MixBus.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/src/Profiles.h"
    ARGS --verify)

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
  #  EVERY HEADER THE SCRIPT READS IS A DEPENDENCY, not just the two the check
  #  started with. The stamp listed three inputs while the script reads a table
  #  of sixteen — so editing any of the other fourteen left the stamp up to date
  #  and the gate did not re-run. 06-05 enrolled Surface.h and wrote a comment
  #  claiming that kept `kUiPollHz` "within the gate's reach"; the enrolment was
  #  real, the wiring was not, and the claim was stronger than the build.
  #  /code-review.
  #
  #  Scraped from the script rather than restated here, because a second copy is
  #  what `GEOMETRY_HEADERS` exists to avoid. The scrape is DELIBERATELY loose:
  #  it takes every `ROOT / "src" / "*.h"` in the file, so it also picks up
  #  TYPOGRAPHY_HEADER. An extra dependency only over-triggers, and a pattern
  #  narrow enough to isolate one list is a pattern that silently stops matching
  #  when the list is reformatted. No count is asserted here — an earlier
  #  version printed one and stated another, which is the failure this whole
  #  phase is about. /simplify.
  #  AND THE READ ITSELF IS A CONFIGURE DEPENDENCY. `file(READ)` is a
  #  configure-time snapshot: without this, adding a header to the script's list
  #  does not re-configure, so the new header never becomes a dependency and
  #  later edits to it do not re-run the gate. That is the enrolment failure
  #  this block exists to close, one level up — and `verify-charset` twenty
  #  lines below already states the rule with CONFIGURE_DEPENDS. /simplify.
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
               "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-geometry.py")

  file(READ "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-geometry.py" FORROBOX_GEOMETRY_SCRIPT)
  string(REGEX MATCHALL "ROOT / \"src\" / \"[A-Za-z]+\.h\"" _rows "${FORROBOX_GEOMETRY_SCRIPT}")
  set(FORROBOX_GEOMETRY_HEADERS)
  foreach(row IN LISTS _rows)
    string(REGEX REPLACE ".*\"([A-Za-z]+\.h)\"$" "\\1" _name "${row}")
    list(APPEND FORROBOX_GEOMETRY_HEADERS "${CMAKE_CURRENT_SOURCE_DIR}/src/${_name}")
  endforeach()
  list(REMOVE_DUPLICATES FORROBOX_GEOMETRY_HEADERS)

  #  A FLOOR, because the scrape can under-report silently: reformat the list,
  #  or name a header with a digit or an underscore, and the pattern matches
  #  fewer — or none — with nothing to say so. A checker that quietly stops
  #  covering things is this phase's whole subject. /simplify.
  list(LENGTH FORROBOX_GEOMETRY_HEADERS _forrobox_geometry_count)
  if(_forrobox_geometry_count LESS 16)
    message(FATAL_ERROR
            "verify-geometry: scraped only ${_forrobox_geometry_count} headers from "
            "verify-geometry.py — the enrolled list has more than that, so the pattern "
            "has stopped matching and the gate would silently lose coverage.")
  endif()

  forrobox_add_verify_target(verify-geometry verify-geometry.py
    "Verifying strip geometry against forrobox.css"
    ${FORROBOX_GEOMETRY_HEADERS}
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

  #  EVERY test source, not only tests/UiTest.cpp. UiTest.cpp is there because
  #  the script asserts its own repertoire against the `allowed` code-point array
  #  THERE, so without it an edit to that array would not re-run the check that
  #  pins the two together — same reason verify-theme takes Theme.cpp as well as
  #  Theme.h. But `check_test_message_literals` scans ALL of tests/, so enrolling
  #  one file left the other four scanned-but-not-depended-on: an incremental
  #  build that touched only tests/VoiceTest.cpp did not re-run the gate, and the
  #  violation surfaced two compilers later on a full rebuild. Found at 08-01, by
  #  putting an em dash in a VoiceTest message. Declared coverage and actual
  #  coverage are now the same set.
  file(GLOB_RECURSE FORROBOX_TESTS_FOR_CHARSET CONFIGURE_DEPENDS
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.h"
    "${CMAKE_CURRENT_SOURCE_DIR}/tests/*.cpp")

  forrobox_add_verify_target(verify-charset verify-charset.py
    "Verifying every src/ literal against the drawn character repertoire"
    ${FORROBOX_TESTS_FOR_CHARSET}
    ${FORROBOX_SRC_FOR_CHARSET})
endif()

# ── Tests ───────────────────────────────────────────────────────────────────
#  Console apps linking the plugin's shared code so the processor and the clock
#  can be constructed directly — no GUI, no audio device. Deliberately not given
#  the LTO flags: these targets are built to be run often.
option(FORROBOX_TESTS "Build the test targets" ON)

# The MIDI cross-check lives inside this block, because it asks ForroBoxTests
# for its bytes. Said out loud rather than left to be discovered: without this,
# -DFORROBOX_TESTS=OFF drops the fifth gate silently while the cross-check flag
# still reads ON — the same "quietly stops running" failure every gate here
# exists to prevent, arriving through an unrelated switch.
if(NOT FORROBOX_TESTS AND FORROBOX_REQUIRE_PROFILE_CHECK)
  message(FATAL_ERROR
          "FORROBOX_TESTS=OFF would also drop the MIDI cross-check, which is run through "
          "the test executable. Keep FORROBOX_TESTS=ON, or pass "
          "-DFORROBOX_REQUIRE_PROFILE_CHECK=OFF to build without the cross-checks.")
endif()

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
    tests/UiTest.cpp
    tests/MidiExportTest.cpp
    tests/EmitMidi.cpp)

  # ── MIDI-export cross-check ────────────────────────────────────────────────
  #  The fifth gate, and the same argument as the other four: PLANNING.md:824-825
  #  names exportMIDI() in audio.js as the reference implementation, so a wrong
  #  byte here is not a crash — it is a file a DAW opens with the groove subtly
  #  wrong. verify-midi.js RUNS that function rather than porting it.
  #
  #  Here rather than beside the other four because it asks the C++ side for its
  #  bytes, so it must run AFTER the test executable links — which is what the
  #  AFTER keyword above exists for. Everything else is the shared mechanism.
  #
  #  NO SECOND OPT-OUT FLAG. verify-theme's comment above says why: "a second
  #  opt-out flag would let one rot while the other looked healthy." A
  #  FORROBOX_REQUIRE_MIDI_CHECK existed here for one revision and immediately
  #  produced the interlock that comment predicts — a Python-less machine that
  #  had opted out of the groove check with -DFORROBOX_REQUIRE_PROFILE_CHECK=OFF
  #  then hard-failed on this one instead, because the second flag still read ON.
  find_program(FORROBOX_NODE_EXECUTABLE NAMES node node.exe)

  if(NOT FORROBOX_NODE_EXECUTABLE OR NOT Python3_Interpreter_FOUND)
    #  Fatal for the reason the groove check is fatal: a check that quietly
    #  stops running turns "the bytes match the design source" into "verified
    #  only if Node happens to be installed", silently, behind a log line.
    #  Blob must be a global, which is Node 18 and later.
    if(FORROBOX_REQUIRE_PROFILE_CHECK)
      message(FATAL_ERROR
              "Node 18+ or Python 3 was not found, so the prototype's exportMIDI cannot be "
              "run and the MIDI export would be unverified. Install them, or pass "
              "-DFORROBOX_REQUIRE_PROFILE_CHECK=OFF to build without the cross-checks.")
    else()
      message(WARNING "MIDI cross-check DISABLED by request — the exported bytes are unverified")
    endif()
  else()
    #  audio.js is in DEPENDS because it IS the reference, and ForroBoxTests
    #  because the C++ side is asked through its --emit-midi mode.
    #  verify-profiles.py is listed for the same reason — this script reuses its
    #  data.js reader. 06-06 shipped a gate whose DEPENDS did not list what it
    #  read.
    forrobox_add_verify_target(verify-midi verify-midi.py
      "Verifying the exported MIDI bytes against the prototype's exportMIDI"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-midi.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/scripts/verify-profiles.py"
      "${CMAKE_CURRENT_SOURCE_DIR}/audio.js"
      "${CMAKE_CURRENT_SOURCE_DIR}/data.js"
      #  The grooves it exports moved here at 09-01. Undeclared, a changed
      #  pattern would not re-run the byte comparison whose whole point is that
      #  a wrong velocity produces a valid .mid file nobody can tell is wrong.
      #  Fourth instance of this enrolment failure; see the note above.
      "${CMAKE_CURRENT_SOURCE_DIR}/assets/profiles.json"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/MidiExport.cpp"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/MidiExport.h"
      "${CMAKE_CURRENT_SOURCE_DIR}/src/ParameterIDs.h"
      ARGS  --exe "$<TARGET_FILE:ForroBoxTests>" --node "${FORROBOX_NODE_EXECUTABLE}"
      AFTER ForroBoxTests)
  endif()
endif()

---
phase: 19-groove-files
plan: 01
subsystem: state
tags: [groove-files, export, import, format, security, untrusted-input]

requires:
  - phase: 18-user-groove-library
    provides: "the .forrogroove format, UserGrooveLibrary, the processor's user-groove API"
provides:
  - "docs/groove-format.md: .forrogroove v1 documented (fields, strict reading, versioning, duplicate rules); the suite parses its example"
  - "readGrooveFile: the one guarded reader (64 KB cap, UTF-8 + BOM, no DOCTYPE/ENTITY, ≤ 32 elements, then strict fromXml) for scans and imports"
  - "writeGrooveFile: atomic write to any path (library and export)"
  - "UserGrooveLibrary::importFiles: identical → ignored, same id + different content → copy 'name (n)'; one rescan + one notify per batch"
  - "processor: grooveForExport / exportGroove / suggestedExportFileName / importGrooves"
affects: [19-02 (the UI over this API), the v0.3 release]

key-files:
  created: [docs/groove-format.md]
  modified: [src/UserGrooves.h, src/UserGrooves.cpp, src/PluginProcessor.h, src/PluginProcessor.cpp, tests/UserGroovesTest.cpp, CMakeLists.txt, README.md]

key-decisions:
  - "Export one groove at a time (the playing one); import many files; identical → ignored, different → copy (user, Phase 19 planning)"
  - "An export keeps the library id only when it IS that library groove field for field (not by the dirty flag)"
  - "v1 is strict: unknown attributes, lane content, non-numeric feel all refuse the file; only range is forgiven"
  - "Every read refuses before parsing what JUCE's parser would be hurt by: DTDs (entity expansion) and deep nesting (recursion)"

duration: ~1 session
completed: 2026-10-06
description: "Groove files you can share: the .forrogroove format documented and versioned, export of the playing groove, batch import with the user's duplicate rules, and every read guarded so damaged or hostile files — including a nesting attack that could have crashed the host — are refused before parsing"
type: Summary
about: "Forró Box"
---

# Phase 19 Plan 01: Groove files

**A groove can leave one machine as a file and arrive on another, and nothing a stranger sends
can damage the library or the host.** The format is documented in `docs/groove-format.md`; the
suite parses the page's own example through the real reader, so the page cannot drift.

## Acceptance Criteria Results

| Criterion | Status | Notes |
|-----------|--------|-------|
| AC-1: export/import round trip | **Pass** | A pristine user groove keeps its id whatever its file is called; a changed tempo exports under a fresh id and the library id returns once it matches; an edited regional groove at 16 steps arrives tiled, under a fresh id, named as the screen named it; re-import is identical |
| AC-2: the duplicate rules | **Pass** | identical; copies "X (2)", "X (3)"; a 24-character name trimmed so " (2)" fits; X untouched; a batch reports every file and notifies once |
| AC-3: hostile files refused safely | **Pass** | Over 64 KB, empty, random bytes, PNG, a directory, a missing path, UTF-16, another root, version 2, a DOCTYPE entity bomb (refused for its DOCTYPE, before parsing), deep nesting (refused by the element count, before parsing), not XML, plus every strict-rule variant — each with a reason, nothing written, all in under a second; planted in the library folder, a scan keeps exactly the valid grooves and reports each. A UTF-8 BOM still reads |
| AC-4: nothing else moves | **Pass** | Suite otherwise unchanged; processBlock untouched |

**Verification:** Linux GCC + Clang 5454/5454; Debug 5459/5459 (0 unexpected); gate 3/3; WSL
Windows 5454/5454 + pluginval (the doc is read over the UNC share). CI: `9cce962` failed the doc test
on two jobs — Windows checks out CRLF, and the portable runtime container did not mount `docs/` —
fixed in `0385826` (run 37463113739): all four jobs green, 5454/5454, Debug 5459/5459.

**Mutation proofs (16):** size cap (both layers), identical-by-id-only, overwrite-instead-of-copy,
unknown attributes, notify-per-file, import-by-file-name, export-always-fresh-id; then the review
fixes: element cap, BOM strip, export id by content, lane content, non-numeric feel, the doc's
fence. Two first-round survivors were weak proofs, not weak code: the nesting file was also
unclosed (the check now asserts the refusal's reason) and one mutant removed half a condition.

**Not run on purpose:** removing the DOCTYPE guard would really expand the entity bomb inside
the test process; the test instead pins that the bomb is refused for its DOCTYPE.

## /code-review (required): six findings, all fixed

1. **High — deep nesting could crash the host.** JUCE's `XmlDocument` parses and frees XML
   recursively; 64 KB of `<a><a>…` is ~21,000 levels, enough to overflow the message thread's stack
   (1 MB on Windows) at import and at every later library scan. Now refused before parsing above
   32 elements (a groove has nine).
2. **Regression — a UTF-8 BOM was refused;** the pre-19-01 scan accepted it. Stripped.
3. **An export's id followed `dirty`, which a feel change never sets** — one id could carry two
   grooves on two machines. Now: the library id only when the export equals the stored groove.
4. The doc's example fence was on the XML's last line, so GitHub rendered the rest as code.
5. Content inside a `<lane>` was accepted, contrary to the doc.
6. A non-numeric `bpm`/`swing`/`cachaca` was silently 0 → clamped. Now refused.

## Deviations

| Type | Count | Notes |
|------|-------|-------|
| Hardening beyond plan | 2 | The element cap and non-numeric refusal (review findings) |
| Test bug caught | 1 | A non-ASCII test literal passed as `const char*` (Latin-1) — fixed with `fromUTF8` |
| CI environment | 2 | CRLF on Windows; `docs/` not mounted in the portable runtime container |

## Next
**19-02:** Export groove… / Import groove… in the gear menu, file choosers, drag and drop of
`.forrogroove` files, /simplify at Phase 19 close, then the v0.3 milestone close.

---
*Phase: 19-groove-files, Plan: 01 · Completed: 2026-10-06*

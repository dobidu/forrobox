"""One declaration of what a cross-check gate reads — and a gate that reads more fails.

Every gate under scripts/ is re-run by the build only when one of its inputs changes, so a
file the gate reads but the build does not know about is a file whose edit the gate never
sees. That is the failure the gates exist to prevent — a wrong digit that is no crash, no
failed build and no failing test — and before 10-02 it had been fixed six times, one file at a
time, each time found late and by a review. The seventh, eighth and ninth were measured at
10-02 planning: verify-theme read src/EffectOverlay.cpp, verify-midi read MixBus.h and
Profiles.h through its import, and verify-charset read every embedded font, all undeclared.

So the list lives in ONE place, the script, and is enforced rather than trusted:

    INPUTS = gate_inputs.declare(__name__, files=[...], globs=[...], unobservable=[...])
    ...
    if __name__ == "__main__":
        sys.exit(gate_inputs.run(main))

- `--list-inputs` prints the declaration, one line each, and exits without checking anything.
  CMake reads that at configure time (forrobox_add_verify_target), so the build depends on
  exactly what the script says — there is no second copy to drift.
- Otherwise every file OPENED FOR READING under the repository is recorded by an audit hook,
  and `run()` fails a passing gate that read anything it did not declare, naming the file.
  Modules loaded through importlib are counted too: they raise no `open` event (measured at
  10-02 planning) and `exec_module` does not register them in `sys.modules`, so the `exec`
  audit event's code object supplies their filename; `sys.modules` is walked as well.
- `unobservable` is for what a CHILD process reads — verify-midi's Node run. Its reads cannot
  be seen from here; they are declared so the build depends on them, and exempt from
  enforcement because enforcement would be a claim this module cannot back.

Standard library only. Declarations made by a module that is IMPORTED rather than run are
no-ops: verify-midi and build-profiles load verify-profiles.py, whose own `declare` must not
hijack the importer's argv or its audit.
"""
from __future__ import annotations

import os
import pathlib
import sys
from typing import Callable, Iterable

ROOT = pathlib.Path(__file__).resolve().parent.parent
LIST_FLAG = "--list-inputs"

_declared_files: set[pathlib.Path] = set()
_declared_globs: list[tuple[pathlib.Path, str, bool]] = []
_unobservable: set[pathlib.Path] = set()
_reads: set[pathlib.Path] = set()


def _resolve(path: os.PathLike | str) -> pathlib.Path:
    return pathlib.Path(path).resolve()


def _build_dirs() -> set[pathlib.Path]:
    """Top-level directories holding a CMake cache: build trees, never inputs."""
    return {child for child in ROOT.iterdir() if (child / "CMakeCache.txt").is_file()}


def _in_repo(path: pathlib.Path, build_dirs: set[pathlib.Path]) -> bool:
    if ROOT not in path.parents:
        return False
    rel = path.relative_to(ROOT)
    if rel.parts and rel.parts[0] == ".git":
        return False
    if "__pycache__" in rel.parts:
        return False
    return not any(b == path or b in path.parents for b in build_dirs)


def _audit(event: str, args: tuple) -> None:
    # A module loaded with importlib's `module_from_spec` + `exec_module` is neither an
    # `open` event nor an entry in sys.modules — both were tried at 10-02 and missed it —
    # but running it is an `exec` of a code object that carries its own filename.
    if event == "exec":
        filename = getattr(args[0], "co_filename", None)
        if filename and not filename.startswith("<"):
            try:
                _reads.add(_resolve(filename))
            except (OSError, ValueError):
                pass
        return
    if event != "open":
        return
    target, mode = args[0], args[1]
    if isinstance(target, int):
        return
    # A write is not a read: build-profiles without --verify WRITES its outputs.
    if mode is not None and not any(c in str(mode) for c in "r+"):
        return
    try:
        _reads.add(_resolve(os.fsdecode(target)))
    except (OSError, ValueError):
        pass


def declare(module_name: str,
            files: Iterable[os.PathLike | str] = (),
            globs: Iterable[tuple[os.PathLike | str, str, bool]] = (),
            unobservable: Iterable[os.PathLike | str] = ()) -> list[pathlib.Path]:
    """Declares the calling gate's inputs. Pass `__name__` first.

    `globs` are `(directory, pattern, recursive)`. Returns the declared files, so a module
    another gate imports can publish what it reads.
    """
    declared = [_resolve(f) for f in files]
    if module_name != "__main__":
        return declared

    _declared_files.update(declared)
    _declared_globs.extend((_resolve(d), p, bool(r)) for d, p, r in globs)
    _unobservable.update(_resolve(f) for f in unobservable)

    if LIST_FLAG in sys.argv:
        # UTF-8 whatever the console says: CMake reads this with ENCODING UTF8, and one input is
        # "Forró Box (standalone).html" — a cp1252 console on Windows would hand it a path that
        # does not exist.
        sys.stdout.reconfigure(encoding="utf-8")
        for path in sorted(_declared_files | _unobservable):
            print(f"file {path.as_posix()}")
        for directory, pattern, recursive in _declared_globs:
            print(f"glob {directory.as_posix()} {pattern} {'recursive' if recursive else 'flat'}")
        sys.stdout.flush()
        sys.exit(0)

    return declared


def _covered(path: pathlib.Path) -> bool:
    if path in _declared_files or path in _unobservable:
        return True
    for directory, pattern, recursive in _declared_globs:
        if directory not in path.parents:
            continue
        rel = path.relative_to(directory)
        if not recursive and len(rel.parts) != 1:
            continue
        if pathlib.PurePath(path.name).match(pattern):
            return True
    return False


def undeclared_reads(script: os.PathLike | str) -> list[pathlib.Path]:
    """Repository files read (or imported) by this run that the declaration does not cover."""
    build_dirs = _build_dirs()
    exempt = {_resolve(script), _resolve(__file__)}

    seen = set(_reads)
    for module in list(sys.modules.values()):
        file = getattr(module, "__file__", None)
        if file:
            seen.add(_resolve(file))

    return sorted(p for p in seen
                  if p not in exempt and _in_repo(p, build_dirs) and p.is_file() and not _covered(p))


def run(main: Callable[[], int | None], script: os.PathLike | str | None = None) -> int:
    """Runs a gate's `main` and returns its exit code — failing a PASS that read undeclared files.

    A gate that already failed keeps its own code: its verdict is the more specific news.
    """
    script = script or sys.modules["__main__"].__file__
    try:
        code = main()
    except SystemExit as stop:
        code = stop.code

    if isinstance(code, str):  # sys.exit("message") semantics
        print(code, file=sys.stderr)
        code = 1
    code = int(code or 0)

    missing = undeclared_reads(script)
    for path in missing:
        print(f"UNDECLARED INPUT: {path.relative_to(ROOT).as_posix()} — read by "
              f"{pathlib.Path(script).name} but not declared, so the build would not re-run "
              f"this gate when it changes. Add it to the script's gate_inputs.declare().",
              file=sys.stderr)

    if missing and code == 0:
        return 1
    return code


# Installed on IMPORT, not in `declare()`: a gate that loads verify-profiles.py at module level
# (build-profiles does) reads MixBus.h while that module loads — before any `declare()` could run.
# Every gate imports this module first, so everything after that line is seen.
sys.addaudithook(_audit)

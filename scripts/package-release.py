#!/usr/bin/env python3
"""Forró Box — one platform's release archive, from a build's artefacts (16-01).

    package-release.py --platform linux|windows --artefacts <dir> --out <dir>
    package-release.py --checksums <dir>

The first form writes ForroBox-<version>-<platform>-<x64|universal>.{tar.gz|zip}: one top
folder holding the VST3 bundle, the standalone, LICENSE, ABOUT.md, NOTICE.md,
INSTALL.txt (from packaging/INSTALL-<platform>.txt.in) and the four OFL font
licences — the layout v0.1 and v0.2 shipped, which were assembled by hand.
<artefacts> is a build's `ForroBox_artefacts/Release` (the VST3/ and Standalone/
folders). <version> is CMakeLists.txt's `project(ForroBox VERSION …)`: one source
of truth, and the VST3's own moduleinfo must agree with it, so a stale build
cannot be packaged under a new number.

REPRODUCIBLE: entries sorted, one mtime (SOURCE_DATE_EPOCH, else the last
commit's time), owner 0, modes normalised. The same artefacts give the same
bytes, so the checksum identifies the contents.

The second form writes SHA256SUMS.txt over the archives in <dir>.

    package-release.py --check-tag <tag>

checks a release tag against that same version — `vX.Y` or `vX.Y-suffix` must
name CMakeLists' X.Y — and prints `version=… prerelease=true|false` for CI; one
parser of the version, so the tag rule and the archive names cannot disagree.
Standard library only: it runs on every CI runner as it is.
"""
from __future__ import annotations

import argparse
import gzip
import hashlib
import os
import plistlib
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path
from typing import NamedTuple

ROOT = Path(__file__).resolve().parent.parent

class Item(NamedTuple):
    """One thing the archive carries from the artefacts: a bundle or a file."""
    source: str                    # relative to the build's artefacts dir
    required: tuple[str, ...] = () # files that must exist inside it (a bundle)

    @property
    def name(self) -> str:
        return Path(self.source).name


class Platform(NamedTuple):
    suffix: str                    # the archive format
    arch: str                      # the archive name's architecture tag
    items: tuple[Item, ...]
    executables: tuple[str, ...]   # file names that get mode 0755
    bundle_binaries: bool = False  # also every file in a macOS bundle's Contents/MacOS/


# The VST3 everywhere; the standalone everywhere; the AU on macOS. The macOS
# bundles are copied whole, `_CodeSignature` included — an ad-hoc signature is
# what lets Apple Silicon load them, and the macos CI job re-verifies it from
# the unzipped archive (17-02).
PLATFORMS = {
    "linux": Platform(".tar.gz", "x64", (
        Item("VST3/ForroBox.vst3", ("Contents/x86_64-linux/ForroBox.so", "Contents/Resources/moduleinfo.json")),
        Item("Standalone/ForroBox"),
    ), ("ForroBox", "ForroBox.so")),
    "windows": Platform(".zip", "x64", (
        Item("VST3/ForroBox.vst3", ("Contents/x86_64-win/ForroBox.vst3", "Contents/Resources/moduleinfo.json")),
        Item("Standalone/ForroBox.exe"),
    ), ("ForroBox.exe", "ForroBox.vst3")),
    "macos": Platform(".zip", "universal", (
        Item("AU/ForroBox.component", ("Contents/MacOS/ForroBox", "Contents/Info.plist")),
        Item("VST3/ForroBox.vst3", ("Contents/MacOS/ForroBox", "Contents/Info.plist",
                                    "Contents/Resources/moduleinfo.json")),
        Item("Standalone/ForroBox.app", ("Contents/MacOS/ForroBox", "Contents/Info.plist")),
    ), (), bundle_binaries=True),
}
DOCS = ("LICENSE", "ABOUT.md", "NOTICE.md")
TAG_PATTERN = re.compile(r"^v(\d+)\.(\d+)(-[A-Za-z0-9.-]+)?$")


def fail(message: str) -> None:
    sys.exit(f"FATAL: {message}")


def cmake_version() -> str:
    text = (ROOT / "CMakeLists.txt").read_text(encoding="utf-8")
    match = re.search(r"project\(ForroBox\s+VERSION\s+(\d+\.\d+\.\d+)", text)
    if match is None:
        fail("no `project(ForroBox VERSION x.y.z)` in CMakeLists.txt")
    return match.group(1)


def source_date_epoch() -> int:
    if "SOURCE_DATE_EPOCH" in os.environ:
        return int(os.environ["SOURCE_DATE_EPOCH"])
    try:
        out = subprocess.run(["git", "-C", str(ROOT), "log", "-1", "--format=%ct"],
                             check=True, capture_output=True, text=True).stdout.strip()
        return int(out)
    except (OSError, subprocess.CalledProcessError, ValueError):
        fail("no SOURCE_DATE_EPOCH and no git history to take a timestamp from")


def fill_template(platform: str, version: str) -> str:
    template = (ROOT / "packaging" / f"INSTALL-{platform}.txt.in").read_text(encoding="utf-8")
    major, minor, _ = version.split(".")
    text = template.replace("@VERSION@", version).replace("@TAG@", f"v{major}.{minor}")
    title = text.splitlines()[0]
    text = text.replace("@UNDERLINE@", "=" * len(title))
    leftover = re.search(r"@[A-Z_]+@", text)
    if leftover:
        fail(f"INSTALL template for {platform} still holds {leftover.group(0)}")
    return text


def archive_top(platform: str, version: str) -> str:
    return f"ForroBox-{version}-{platform}-{PLATFORMS[platform].arch}"


def stage(platform: str, artefacts: Path, version: str, into: Path) -> Path:
    """Copies everything into <into>/<top>/, validating as it goes."""
    spec = PLATFORMS[platform]
    top = into / archive_top(platform, version)

    for item in spec.items:
        source = artefacts / item.source
        if not source.exists():
            fail(f"missing {source}")
        for inner in item.required:
            if not (source / inner).is_file():
                fail(f"missing {source / inner}")
        if source.is_dir():
            links = [p for p in source.rglob("*") if p.is_symlink()]
            if links:
                fail(f"{source} contains symlinks ({links[0]}) — the archive would dereference them "
                     f"and break a signed bundle")
    for doc in DOCS:
        if not (ROOT / doc).is_file():
            fail(f"missing {ROOT / doc}")

    # NOT json.loads: the VST3 SDK's moduleinfo is JSON5-flavoured (trailing
    # commas), and Python's parser rejects it. Every "Version" field in it — the
    # module's and each class's — is the build's version, so all must agree. A
    # macOS bundle's Info.plist must agree too.
    for item in spec.items:
        source = artefacts / item.source
        moduleinfo = source / "Contents" / "Resources" / "moduleinfo.json"
        if moduleinfo.is_file():
            found = set(re.findall(r'"Version"\s*:\s*"([^"]*)"', moduleinfo.read_text(encoding="utf-8")))
            if found != {version}:
                fail(f"{item.name} says version {sorted(found)} but CMakeLists.txt says {version!r} — a stale build?")
        plist = source / "Contents" / "Info.plist"
        if plist.is_file():
            with open(plist, "rb") as handle:
                found_plist = plistlib.load(handle).get("CFBundleShortVersionString")
            if found_plist != version:
                fail(f"{item.name}'s Info.plist says version {found_plist!r} but CMakeLists.txt says {version!r} — a stale build?")

    licences = sorted((ROOT / "assets" / "fonts").glob("*-OFL.txt"))
    if len(licences) != 4:
        fail(f"expected the four OFL font licences in assets/fonts, found {len(licences)}")

    top.mkdir(parents=True)
    for item in spec.items:
        source = artefacts / item.source
        if source.is_dir():
            shutil.copytree(source, top / item.name)
        else:
            shutil.copy2(source, top / item.name)
    for doc in DOCS:
        shutil.copy2(ROOT / doc, top / doc)
    (top / "licenses").mkdir()
    for licence in licences:
        shutil.copy2(licence, top / "licenses" / licence.name)
    (top / "INSTALL.txt").write_text(fill_template(platform, version), encoding="utf-8")
    return top


def is_executable(path: Path, platform: str) -> bool:
    """The platform's named binaries; on macOS, every file in a bundle's MacOS/."""
    if not path.is_file():
        return False
    spec = PLATFORMS[platform]
    return path.name in spec.executables or (spec.bundle_binaries and path.parent.name == "MacOS")


def entries(top: Path) -> list[Path]:
    """The top folder and everything under it, sorted, directories before their contents."""
    return [top] + sorted(top.rglob("*"), key=lambda p: p.relative_to(top).as_posix())


def write_tar(top: Path, out: Path, mtime: int, platform: str) -> None:
    # Streamed into a gzip whose header carries mtime 0, so nothing in the
    # compressed bytes depends on when the archive was made.
    with open(out, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz, \
         tarfile.open(fileobj=gz, mode="w|", format=tarfile.PAX_FORMAT) as tar:
        for path in entries(top):
            info = tar.gettarinfo(str(path), arcname=path.relative_to(top.parent).as_posix())
            info.mtime, info.uid, info.gid, info.uname, info.gname = mtime, 0, 0, "", ""
            info.mode = 0o755 if path.is_dir() or is_executable(path, platform) else 0o644
            if path.is_file():
                with open(path, "rb") as handle:
                    tar.addfile(info, handle)
            else:
                tar.addfile(info)


def write_zip(top: Path, out: Path, mtime: int, platform: str) -> None:
    stamp = time.gmtime(max(mtime, 315532800))[:6]   # zip cannot go before 1980
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in entries(top):
            name = path.relative_to(top.parent).as_posix() + ("/" if path.is_dir() else "")
            info = zipfile.ZipInfo(name, date_time=stamp)
            mode = 0o755 if path.is_dir() or is_executable(path, platform) else 0o644
            info.external_attr = ((0o040000 if path.is_dir() else 0o100000) | mode) << 16
            if path.is_dir():
                info.external_attr |= 0x10
                archive.writestr(info, b"")
            else:
                info.compress_type = zipfile.ZIP_DEFLATED
                with open(path, "rb") as source, archive.open(info, "w") as target:
                    shutil.copyfileobj(source, target)


def package(platform: str, artefacts: Path, out_dir: Path) -> Path:
    version = cmake_version()
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"{archive_top(platform, version)}{PLATFORMS[platform].suffix}"
    mtime = source_date_epoch()

    with tempfile.TemporaryDirectory() as tmp:
        top = stage(platform, artefacts, version, Path(tmp))   # validates before anything is written
        partial = out.with_name(out.name + ".partial")
        writer = write_tar if PLATFORMS[platform].suffix == ".tar.gz" else write_zip
        writer(top, partial, mtime, platform)
        partial.replace(out)
    print(f"{out}  ({out.stat().st_size} bytes)")
    return out


def checksums(directory: Path) -> Path:
    archives = sorted(p for p in directory.iterdir()
                      if p.name.startswith("ForroBox-") and p.name.endswith((".zip", ".tar.gz")))
    if not archives:
        fail(f"no ForroBox-*.zip / *.tar.gz in {directory}")
    def digest(path: Path) -> str:
        with open(path, "rb") as handle:
            return hashlib.file_digest(handle, "sha256").hexdigest()

    lines = [f"{digest(p)}  {p.name}" for p in archives]
    out = directory / "SHA256SUMS.txt"
    out.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(out.read_text(encoding="utf-8"), end="")
    return out


def check_tag(tag: str) -> None:
    match = TAG_PATTERN.match(tag)
    if match is None:
        fail(f"tag {tag!r} is not vX.Y or vX.Y-suffix")
    version = cmake_version()
    if version.rsplit(".", 1)[0] != f"{match.group(1)}.{match.group(2)}":
        fail(f"tag {tag} is {match.group(1)}.{match.group(2)} but CMakeLists.txt says {version} — not releasing")
    print(f"version={version}")
    print(f"prerelease={'true' if match.group(3) else 'false'}")


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--platform", choices=sorted(PLATFORMS))
    parser.add_argument("--artefacts", type=Path, help="a build's ForroBox_artefacts/Release")
    parser.add_argument("--out", type=Path)
    parser.add_argument("--checksums", type=Path, metavar="DIR")
    parser.add_argument("--check-tag", metavar="TAG")
    args = parser.parse_args()

    if args.check_tag:
        check_tag(args.check_tag)
    elif args.checksums:
        checksums(args.checksums)
    elif args.platform and args.artefacts and args.out:
        package(args.platform, args.artefacts, args.out)
    else:
        parser.error("give --platform, --artefacts and --out; --checksums DIR; or --check-tag TAG")


if __name__ == "__main__":
    main()

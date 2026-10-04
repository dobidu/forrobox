#!/usr/bin/env python3
"""Forró Box — one platform's release archive, from a build's artefacts (16-01).

    package-release.py --platform linux|windows --artefacts <dir> --out <dir>
    package-release.py --checksums <dir>

The first form writes ForroBox-<version>-<platform>-x64.{tar.gz|zip}: one top
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
import re
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

PLATFORMS = {
    # platform: (archive suffix, standalone name, the binary inside the bundle)
    "linux":   (".tar.gz", "ForroBox",     "x86_64-linux/ForroBox.so"),
    "windows": (".zip",    "ForroBox.exe", "x86_64-win/ForroBox.vst3"),
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


def stage(platform: str, artefacts: Path, version: str, into: Path) -> Path:
    """Copies everything into <into>/<top>/, validating as it goes."""
    _, standalone_name, bundle_binary = PLATFORMS[platform]
    top = into / f"ForroBox-{version}-{platform}-x64"

    bundle = artefacts / "VST3" / "ForroBox.vst3"
    standalone = artefacts / "Standalone" / standalone_name
    for required in (bundle / "Contents" / bundle_binary, bundle / "Contents" / "Resources" / "moduleinfo.json",
                     standalone, *(ROOT / doc for doc in DOCS)):
        if not required.is_file():
            fail(f"missing {required}")

    # NOT json.loads: the VST3 SDK's moduleinfo is JSON5-flavoured (trailing
    # commas), and Python's parser rejects it. Every "Version" field in it — the
    # module's and each class's — is the build's version, so all must agree.
    moduleinfo = (bundle / "Contents" / "Resources" / "moduleinfo.json").read_text(encoding="utf-8")
    module_versions = set(re.findall(r'"Version"\s*:\s*"([^"]*)"', moduleinfo))
    if module_versions != {version}:
        fail(f"the VST3 says version {sorted(module_versions)} but CMakeLists.txt says {version!r} — a stale build?")

    licences = sorted((ROOT / "assets" / "fonts").glob("*-OFL.txt"))
    if len(licences) != 4:
        fail(f"expected the four OFL font licences in assets/fonts, found {len(licences)}")

    top.mkdir(parents=True)
    shutil.copytree(bundle, top / "ForroBox.vst3")
    shutil.copy2(standalone, top / standalone_name)
    for doc in DOCS:
        shutil.copy2(ROOT / doc, top / doc)
    (top / "licenses").mkdir()
    for licence in licences:
        shutil.copy2(licence, top / "licenses" / licence.name)
    (top / "INSTALL.txt").write_text(fill_template(platform, version), encoding="utf-8")
    return top


def is_executable(path: Path, platform: str) -> bool:
    """The standalone and the bundle's binary — the two names PLATFORMS gives."""
    _, standalone_name, bundle_binary = PLATFORMS[platform]
    return path.is_file() and path.name in (standalone_name, Path(bundle_binary).name)


def entries(top: Path) -> list[Path]:
    """The top folder and everything under it, sorted, directories before their contents."""
    return [top] + sorted(top.rglob("*"), key=lambda p: p.relative_to(top).as_posix())


def write_tar(top: Path, out: Path, mtime: int) -> None:
    # Streamed into a gzip whose header carries mtime 0, so nothing in the
    # compressed bytes depends on when the archive was made.
    with open(out, "wb") as raw, gzip.GzipFile(filename="", mode="wb", fileobj=raw, mtime=0) as gz, \
         tarfile.open(fileobj=gz, mode="w|", format=tarfile.PAX_FORMAT) as tar:
        for path in entries(top):
            info = tar.gettarinfo(str(path), arcname=path.relative_to(top.parent).as_posix())
            info.mtime, info.uid, info.gid, info.uname, info.gname = mtime, 0, 0, "", ""
            info.mode = 0o755 if path.is_dir() or is_executable(path, "linux") else 0o644
            if path.is_file():
                with open(path, "rb") as handle:
                    tar.addfile(info, handle)
            else:
                tar.addfile(info)


def write_zip(top: Path, out: Path, mtime: int) -> None:
    stamp = time.gmtime(max(mtime, 315532800))[:6]   # zip cannot go before 1980
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as archive:
        for path in entries(top):
            name = path.relative_to(top.parent).as_posix() + ("/" if path.is_dir() else "")
            info = zipfile.ZipInfo(name, date_time=stamp)
            mode = 0o755 if path.is_dir() or is_executable(path, "windows") else 0o644
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
    suffix = PLATFORMS[platform][0]
    out_dir.mkdir(parents=True, exist_ok=True)
    out = out_dir / f"ForroBox-{version}-{platform}-x64{suffix}"
    mtime = source_date_epoch()

    with tempfile.TemporaryDirectory() as tmp:
        top = stage(platform, artefacts, version, Path(tmp))   # validates before anything is written
        partial = out.with_name(out.name + ".partial")
        (write_tar if platform == "linux" else write_zip)(top, partial, mtime)
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

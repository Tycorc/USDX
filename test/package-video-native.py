#!/usr/bin/env python3
"""Assemble the manually built Windows smoke bundle, including its Python runtime."""

import argparse
from datetime import datetime, timezone
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys


# The exact official runtime used by the existing October native tests.
MPV_URL = ("https://github.com/mpv-player/mpv/releases/download/git-release/"
           "libmpv-v0.41.0-dev-gbaa3e11d4-40141553607-x86_64-w64-mingw32-gpl.zip")
MPV_SHA256 = "17e808620076c835c714015baf858c74f6d5dd305f40b99a1e7762b54c790c0f"
MPV_FILES = {
    "libmpv-2.dll": "42ef849c6104ba2c7a69752f192a171bf30f20790b1268898df564a5c2fadaba",
    "licenses/libmpv/LICENSE.GPL": "edaef632cbb643e4e7a221717a6c441a4c1a7c918e6e4d56debc3d8739b233f6",
    "licenses/libmpv/runtime-provenance.json": "31667dd93f1085d794b5deea35e7778ceb3454d7e0771e688e6e08c6c3af9028",
}


def digest(path):
    checksum = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            checksum.update(block)
    return checksum.hexdigest()


def copy_file(source, target):
    target.parent.mkdir(parents=True, exist_ok=True)
    if target.exists() and digest(source) != digest(target):
        raise RuntimeError("Conflicting runtime file: " + str(target))
    shutil.copy2(source, target)


def package(root, output):
    if os.name != "nt":
        raise RuntimeError("Package with native Windows Python on the CI runner")
    if output.exists():
        raise RuntimeError("Use a new output directory: " + str(output))
    output.mkdir(parents=True)
    runtime = output / "runtime"
    runtime.mkdir()
    for source in (root / "game").iterdir():
        if source.suffix.lower() in (".dll", ".txt") or source.name == "usce.exe":
            copy_file(source, runtime / source.name)
    for variant, name in (("with", "VideoSmoke.exe"), ("without", "VideoSmoke-without.exe")):
        copy_file(root / "build" / ("video-" + variant) / "VideoSmoke.exe", runtime / name)
    for name in ("LICENSE", "COPYRIGHT.txt"):
        copy_file(root / name, output / name)
    for name in ("run-video-smoke.py", "run-video-native.ps1"):
        copy_file(root / "test" / name, output / "test" / name)
    copy_file(root / "tools" / "check_windows_runtime.py", output / "tools" / "check_windows_runtime.py")
    shutil.copytree(root / "build" / "video-fixtures", output / "fixtures")

    # Copy the hosted runner's CPython, without third-party packages or developer tools.
    # The isolated path file prevents the local machine's Python settings taking effect.
    python_root = Path(sys.base_prefix)
    python_target = output / "python"
    python_target.mkdir()
    copy_file(Path(sys.executable), python_target / "python.exe")
    for source in python_root.glob("*.dll"):
        copy_file(source, python_target / source.name)
    copy_file(python_root / "LICENSE.txt", python_target / "LICENSE.txt")
    ignored = shutil.ignore_patterns("site-packages", "__pycache__", "test", "tests", "idlelib", "tkinter", "turtledemo")
    shutil.copytree(python_root / "Lib", python_target / "Lib", ignore=ignored)
    shutil.copytree(python_root / "DLLs", python_target / "DLLs")
    version = str(sys.version_info.major) + str(sys.version_info.minor)
    (python_target / ("python" + version + "._pth")).write_text("Lib\nDLLs\n", encoding="utf-8")
    sha = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=root, text=True).strip()
    manifest = {
        "schema": 1,
        "source_sha": sha,
        "created_utc": datetime.now(timezone.utc).isoformat(),
        "workflow_run": os.environ.get("GITHUB_SERVER_URL", "https://github.com") + "/" +
                        os.environ.get("GITHUB_REPOSITORY", "") + "/actions/runs/" +
                        os.environ.get("GITHUB_RUN_ID", ""),
        "scope": "Native video smoke tests; no release or hardware-validation claim",
        "python_version": sys.version,
        "external_libmpv": {
            "original_url": MPV_URL,
            "original_archive_sha256": MPV_SHA256,
            "availability": "Original rolling-release asset returned HTTP 404 on 2026-11-13; use the preserved local runtime",
            "files": MPV_FILES,
        },
        "files": {path.relative_to(output).as_posix(): digest(path)
                  for path in sorted(output.rglob("*")) if path.is_file()},
    }
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Packaged native smoke inputs for " + sha)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", required=True, type=Path)
    arguments = parser.parse_args()
    package(Path(__file__).resolve().parent.parent, arguments.output.resolve())

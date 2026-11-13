#!/usr/bin/env python3
"""Compare real FFmpeg rendering in two configured USCE checkouts on one machine.

Build each checkout first, then run this same script/include against both:
  xvfb-run -a python3 test/run-video-compare.py --root /src/main \
      --output-dir /results/main --fixtures-dir /results/fixtures
  xvfb-run -a python3 test/run-video-compare.py --root /src/branch \
      --output-dir /results/branch --fixtures-dir /results/fixtures \
      --compare-to /results/main

Use the same libraries, GL driver and configuration for both. Outputs are PPM
captures, logs and a manifest; comparisons are exact, with no hidden tolerance.
This tests caller-equivalent draw settings, not interactive UI/audio behavior.
The preview reflection case preserves the existing clipping. A separate
negative-spacing overlap case must produce visible reflection pixels, using
the public spacing setter without changing the production drawing code.
"""

import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import shlex
import subprocess
import tempfile


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def make_fixture(path, width, height):
    if path.exists():
        return
    colors = ((240, 16, 24), (16, 240, 32), (24, 32, 240), (240, 240, 16))
    pixels = bytearray()
    for y in range(height):
        for x in range(width):
            color = colors[(2 if y >= height // 2 else 0) + (x >= width // 2)]
            # Asymmetric stripes make crop, zoom and orientation distinguishable.
            if (x // 13 + y // 17) % 7 == 0:
                color = tuple(value // 2 for value in color)
            pixels.extend(color)
    subprocess.run([
        "ffmpeg", "-hide_banner", "-loglevel", "error", "-y",
        "-f", "rawvideo", "-pixel_format", "rgb24", "-video_size", f"{width}x{height}",
        "-framerate", "25", "-i", "pipe:0", "-c:v", "libx264", "-threads", "1",
        "-crf", "10", "-pix_fmt", "yuv420p", str(path),
    ], input=bytes(pixels) * 25, check=True)


def compile_program(root, output, include):
    src = root / "src"
    if not (src / "Makefile").is_file() or not (src / "paths.inc").is_file():
        raise RuntimeError(f"Configure and build {root} first")
    # As in run-video-smoke.py, use the application's ordered unit list and
    # configured compiler/link flags, with fresh isolated generated units.
    project = (src / "usce.dpr").read_text()
    marker = re.search(r"(?m)^const\s*\n\s*sLineBreakWin", project)
    if marker is None:
        raise RuntimeError("Could not locate the application's unit-list boundary")
    header = project[:marker.start()].replace(
        "program usce;", "program VideoCompare;\n{$DEFINE CONSOLE}")
    header = re.sub(r"(?m)^\s*\{\$R [^\n]+\}\s*$", "", header)
    header = re.sub(r"in '([^']+)'", lambda match: "in '" + src.as_posix() + "/" +
                    match[1].replace("\\", "/") + "'", header)
    compile_dir = Path(tempfile.mkdtemp(prefix="compile-", dir=output))
    source = compile_dir / "VideoCompare.pas"
    source.write_text(header + include.read_text())
    executable = compile_dir / "VideoCompare"
    flags = ["-B", "-dCONSOLE", f"-Fi{src}", f"-FU{compile_dir}", f"-FE{compile_dir}",
             f"-o{executable}", str(source)]
    recipe = "video-compare-build:; $(PPC) $(PFLAGS_ALL) " + shlex.join(flags)
    with (output / "build.log").open("w") as log:
        subprocess.run(["make", "--no-print-directory", "-s", "-C", str(src),
                        "PCUNIT_FLAGS=", "--eval=.PHONY: video-compare-build",
                        "--eval=" + recipe, "video-compare-build"],
                       stdout=log, stderr=subprocess.STDOUT, check=True)
    return executable


def compare_files(expected, actual):
    left, right = expected.read_bytes(), actual.read_bytes()
    if left == right:
        return None
    # Our writer always emits this simple header. Reject incompatible output.
    header = b"P6\n640 240\n255\n"
    if not left.startswith(header) or not right.startswith(header):
        return "incompatible PPM headers"
    left, right = left[len(header):], right[len(header):]
    if len(left) != 640 * 240 * 3 or len(right) != len(left):
        return "incompatible pixel counts"
    changed = [i // 3 for i in range(0, len(left), 3) if left[i:i+3] != right[i:i+3]]
    return {"changed_pixels": len(changed),
            "max_channel_delta": max(abs(a-b) for a, b in zip(left, right)),
            "bounds": [min(i % 640 for i in changed), min(i // 640 for i in changed),
                       max(i % 640 for i in changed), max(i // 640 for i in changed)]}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parent.parent)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--fixtures-dir", type=Path, required=True)
    parser.add_argument("--compare-to", type=Path)
    parser.add_argument("--renderers", nargs="+", default=["gl", "gl2", "gles2", "gles3"],
                        choices=["gl", "gl2", "gles2", "gles3"])
    args = parser.parse_args()
    if os.name != "posix":
        parser.error("This comparison driver currently supports Linux/Unix compiler paths only")
    root, output, fixtures = args.root.resolve(), args.output_dir.resolve(), args.fixtures_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    fixtures.mkdir(parents=True, exist_ok=True)
    if any(output.glob("*/*.ppm")):
        parser.error("Use a fresh output directory so stale captures cannot count as results")
    include = Path(__file__).resolve().with_name("VideoCompare.inc")
    landscape, portrait = fixtures / "landscape.mp4", fixtures / "portrait.mp4"
    make_fixture(landscape, 320, 180)
    make_fixture(portrait, 180, 320)
    executable = compile_program(root, output, include)
    manifest = {"root": str(root), "include_sha256": digest(include),
                "fixtures": {p.name: digest(p) for p in (landscape, portrait)},
                "renderers": args.renderers, "captures": []}
    differences = {}
    for renderer in args.renderers:
        directory = output / renderer
        directory.mkdir(exist_ok=True)
        environment = dict(os.environ)
        if renderer == "gl2":
            environment["MESA_GL_VERSION_OVERRIDE"] = "2.1"
        elif renderer.startswith("gles"):
            environment["MESA_GLES_VERSION_OVERRIDE"] = "2.0" if renderer == "gles2" else "3.0"
        result = subprocess.run([str(executable), renderer, str(landscape), str(portrait), str(directory)],
                                cwd=directory, env=environment, capture_output=True, text=True, timeout=90)
        log = result.stdout + result.stderr
        (output / (renderer + ".log")).write_text(log)
        result.check_returncode()
        captures = sorted(directory.glob("*.ppm"))
        if len(captures) != 48 or re.search(r"(?:ERROR|CRITICAL):", log):
            raise RuntimeError(f"{renderer}: missing captures or logged errors; see {output / (renderer+'.log')}")
        for capture in captures:
            name = capture.relative_to(output).as_posix()
            manifest["captures"].append(name)
            if name.endswith("-reflection-overlap.ppm"):
                plain = output / name.replace("-reflection-overlap.ppm", "-preview-cover.ppm")
                if compare_files(plain, capture) is None:
                    raise RuntimeError(f"{name}: overlapping reflection changed no pixels")
            if "-memory-" in name:
                difference = compare_files(output / name.replace("-memory-", "-file-"), capture)
                if difference:
                    differences[name + " (memory versus file)"] = difference
        print(f"Captured {renderer}: {len(captures)} frames", flush=True)
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    if args.compare_to:
        baseline = args.compare_to.resolve()
        reference = json.loads((baseline / "manifest.json").read_text())
        for key in ("include_sha256", "fixtures", "renderers", "captures"):
            if reference[key] != manifest[key]:
                raise RuntimeError(f"Baseline and candidate differ in {key}; use identical harness, fixtures and profiles")
        for name in manifest["captures"]:
            difference = compare_files(baseline / name, output / name)
            if difference:
                differences[name] = difference
    (output / "differences.json").write_text(json.dumps(differences, indent=2) + "\n")
    if differences:
        print(json.dumps(differences, indent=2))
        raise SystemExit("Pixel differences found; inspect paired PPM files before accepting them")
    print("PASS exact pixel comparison" if args.compare_to else "PASS capture and memory/file equivalence")


if __name__ == "__main__":
    main()

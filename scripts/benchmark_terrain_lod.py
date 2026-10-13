#!/usr/bin/env python3
"""Compare Full and screen-error terrain LOD along identical camera samples."""

import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile


def summarize(path, warmup_frames):
    timings = {}
    gauges = {}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            if int(row["frame"]) < warmup_frames:
                continue
            name = row["scope"]
            destination = gauges if row["gauge"] else timings
            value = row["gauge"] if row["gauge"] else row["total_ms"]
            destination.setdefault(name, []).append(float(value))

    def distribution(values):
        ordered = sorted(values)
        return {
            "samples": len(ordered),
            "median": statistics.median(ordered),
            "p95": ordered[math.ceil(len(ordered) * 0.95) - 1],
            "max": ordered[-1],
        }

    return {
        "timings_ms": {name: distribution(values) for name, values in timings.items()},
        "gauges": {name: distribution(values) for name, values in gauges.items()},
    }


def prepare_project(root, destination, error_pixels, terrain_renderer):
    shutil.copytree(
        root / "examples/terrain_graph_3d",
        destination,
        ignore=shutil.ignore_patterns("generated", "build"),
    )
    manifest_path = destination / "demi.project.json"
    manifest = json.loads(manifest_path.read_text())
    manifest["display"] = {"vsync": False}
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n")

    scene_path = destination / "scenes/main.scene.json"
    scene = json.loads(scene_path.read_text())
    for entity in scene["entities"]:
        if entity["id"] == "terrain":
            entity["components"]["Terrain3D"]["lod_error_pixels"] = error_pixels
        if entity["id"] == "environment" and terrain_renderer == "mesh":
            entity["components"]["Environment3D"]["terrain_gpu_cache_mb"] = 0
        if entity["id"] == "camera":
            entity["components"]["LuaScript"] = {
                "module": "script://scripts/terrain_lod_camera.lua"
            }
    scene_path.write_text(json.dumps(scene, indent=2) + "\n")
    shutil.copyfile(
        root / "scripts/terrain_lod_camera.lua",
        destination / "scripts/terrain_lod_camera.lua",
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--build-configuration", required=True)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--width", type=int, default=1920)
    parser.add_argument("--height", type=int, default=1080)
    parser.add_argument("--error-pixels", type=float, default=2.0)
    parser.add_argument("--terrain-renderer", choices=["auto", "mesh"], default="auto",
                        help="Mesh disables the GPU patch cache for a same-binary comparison")
    arguments = parser.parse_args()
    if arguments.frames <= 60:
        parser.error("Frames must include the 60-frame warmup and measured samples")
    if not math.isfinite(arguments.error_pixels) or arguments.error_pixels <= 0:
        parser.error("Error pixels must be finite and positive")
    if not (1 <= arguments.width <= 65535 and 1 <= arguments.height <= 65535):
        parser.error("Window dimensions must be between 1 and 65535")

    binary = arguments.binary.resolve(strict=True)
    output = arguments.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    root = Path(__file__).resolve().parents[1]
    environment = dict(os.environ)
    environment.pop("DEMI_HEADLESS", None)
    environment.pop("DEMI_FIXED_DELTA_SECONDS", None)
    environment["DEMI_PROFILE_SLOW_MS"] = "1000000"

    results = {
        "binary": str(binary),
        "build_configuration": arguments.build_configuration,
        "binary_sha256": hashlib.sha256(binary.read_bytes()).hexdigest(),
        "scene_sha256": hashlib.sha256(
            (root / "examples/terrain_graph_3d/scenes/main.scene.json").read_bytes()
        ).hexdigest(),
        "lod_error_pixels": arguments.error_pixels,
        "terrain_renderer": arguments.terrain_renderer,
        "requested_size": [arguments.width, arguments.height],
        "frames": arguments.frames,
        "warmup_frames": 60,
        "vsync": False,
        "runs": {},
    }
    for name, error_pixels in [("full", 0), ("lod", arguments.error_pixels)]:
        with tempfile.TemporaryDirectory(prefix="demi-terrain-lod-") as temporary:
            project = Path(temporary) / "project"
            prepare_project(root, project, error_pixels, arguments.terrain_renderer)
            frame_report = output / f"{name}-frames.csv"
            command = [
                str(binary), "run", "--project", str(project),
                "--max-frames", str(arguments.frames),
                "--window-size", f"{arguments.width}x{arguments.height}",
                "--profile-report", str(output / f"{name}-profile.csv"),
                "--profile-frames", str(frame_report),
            ]
            with (output / f"{name}.log").open("w") as log:
                subprocess.run(
                    command, env=environment, stdout=log,
                    stderr=subprocess.STDOUT, check=True, timeout=300,
                )
            results["runs"][name] = summarize(frame_report, 60)
    (output / "summary.json").write_text(json.dumps(results, indent=2) + "\n")
    print(output / "summary.json")


if __name__ == "__main__":
    main()

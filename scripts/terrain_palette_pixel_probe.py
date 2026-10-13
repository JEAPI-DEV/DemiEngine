#!/usr/bin/env python3
"""Create a small authored terrain fixture or check a root-captured image.

This script never launches Demi or captures a window. Native execution remains
with the qualification coordinator. Image checks exercise the production shader
through its captured output, rather than implementing a second palette sampler.
"""

import argparse
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import subprocess


def write_document(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def source_hash(path):
    # The single-source AssetHash::hashFiles contract includes the filename.
    value = 14695981039346656037
    for byte in path.name.encode("utf-8") + b"\0" + path.read_bytes():
        value = ((value ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{value:016x}"


def generate(directory, variant):
    directory.mkdir(parents=True, exist_ok=True)
    if (directory / "demi.project.json").exists():
        raise ValueError("Choose a fresh fixture directory; existing projects are preserved")
    materials = [("unlit", "builtin://unlit")]
    if variant == "lit-unlit":
        materials.append(("lit", "builtin://lit"))
    for name, shader in materials:
        source = directory / "assets" / f"{name}.material.json"
        write_document(source, {
            "format_version": 1,
            "shader": shader,
            "parameters": {
                "base_color": [1, 1, 1, 1],
                "roughness": 1,
            },
            "render_state": {
                "cull": "none",
                "depth_test": True,
                "depth_write": True,
            },
        })
        write_document(source.with_name(f"{name}.asset.json"), {
            "format_version": 1,
            "id": f"asset://probe/{name}",
            "type": "Material",
            "importer": "material",
            "importer_version": 1,
            "source": source.name,
            "source_hash": source_hash(source),
            "dependencies": [],
        })
    if variant == "four-colors":
        size = 1
        colors = {
            "corner00": [1, 0, 0, 1],
            "corner10": [0, 1, 0, 1],
            "corner01": [0, 0, 1, 1],
            "corner11": [1, 1, 1, 1],
        }
        biomes = {}
        for name, color in colors.items():
            biomes[name] = {
                "color": color,
                "material": "asset://probe/unlit",
            }
        regions = []
        corners = [("corner10", [1, 0]), ("corner01", [0, 1]), ("corner11", [1, 1])]
        for name, point in corners:
            regions.append({"biome": name, "center": point, "radius": 0.2})
        default_biome = "corner00"
    else:
        size = 3
        left_color = [0, 0, 0, 1] if variant == "black-white" else [1, 1, 1, 1]
        left_material = "asset://probe/lit" if variant == "lit-unlit" else "asset://probe/unlit"
        biomes = {
            "left": {"color": left_color, "material": left_material},
            "right": {"color": [1, 1, 1, 1], "material": "asset://probe/unlit"},
        }
        # All four rows have black/lit samples at x=0,1 and white/unlit at x=2,3.
        regions = []
        for x in [2, 3]:
            for z in range(4):
                regions.append({
                    "biome": "right",
                    "center": [x, z],
                    "radius": 0.2,
                })
        default_biome = "left"
    recipe = {
        "format_version": 1,
        "size": [size, size],
        "resolution": [size, size],
        "chunk_cells": size,
        "default_biome": default_biome,
        "landforms": {"default": {"base_height": 0, "height_variation": 0}},
        "biomes": biomes, "regions": regions,
    }
    startup_assets = []
    for name, _ in materials:
        startup_assets.append(f"asset://probe/{name}")
    project = {
        "format_version": 1,
        "name": f"Terrain palette pixel probe: {variant}",
        "main_scene": "scene://palette_probe/main",
        "scenes": [{"id": "scene://palette_probe/main"}],
        "assets": startup_assets,
    }
    terrain = {
        "id": "terrain",
        "components": {
            "Transform3D": {},
            "Terrain3D": {
                "recipe": recipe,
                "lod_error_pixels": 0,
            },
        },
    }
    camera = {
        "id": "camera",
        "components": {
            "Transform3D": {
                "position": [size / 2, 3, size / 2],
                "rotation": [1.5707963267948966, 0, 0],
            },
            "Camera3D": {
                "perspective": False,
                "orthographic_size": size,
                "far_clip": 20,
                "clear_color": [1, 0, 1, 1],
            },
        },
    }
    lighting = {
        "id": "lighting",
        "components": {
            "Environment3D": {
                "ambient_color": [0, 0, 0, 1],
                "ambient_intensity": 0,
                "msaa_samples": 0,
            },
        },
    }
    scene = {
        "format_version": 1,
        "id": "scene://palette_probe/main",
        "entities": [terrain, camera, lighting],
    }
    write_document(directory / "demi.project.json", project)
    write_document(directory / "scenes/main.scene.json", scene)
    print(directory / "demi.project.json")


def image_pixels(path, rectangle):
    identify = shutil.which("identify")
    convert = shutil.which("convert")
    if not identify or not convert:
        raise RuntimeError("Image checks require ImageMagick identify/convert")
    dimensions = subprocess.run([identify, "-format", "%w %h", str(path)],
                                check=True, capture_output=True, text=True, timeout=15)
    width, height = map(int, dimensions.stdout.split())
    command = [convert, str(path)]
    if rectangle:
        x, y, width, height = rectangle
        command += ["-crop", f"{width}x{height}+{x}+{y}", "+repage"]
    command += ["-depth", "8", "rgb:-"]
    pixels = subprocess.run(command, check=True, capture_output=True, timeout=15).stdout
    if len(pixels) != width * height * 3:
        raise RuntimeError("Capture did not decode to the requested RGB dimensions")
    return width, height, pixels


def check(image, variant, expectation, rectangle, report):
    width, height, pixels = image_pixels(image, rectangle)
    if width < 64 or height < 64 or abs(width - height) > 2:
        raise ValueError("Use a square client-area capture of at least 64 pixels")
    def pixel(x, y):
        offset = (y * width + x) * 3
        return tuple(pixels[offset:offset + 3])
    center = []
    for y in [height // 2 - 1, height // 2]:
        for x in [width // 2 - 1, width // 2]:
            center.append(pixel(x, y))
    midpoint = []
    for channel in range(3):
        midpoint.append(statistics.mean(color[channel] for color in center))
    row = []
    for x in range(width):
        row.append(pixel(x, height // 2))
    errors = []
    if expectation == "smooth":
        if any(abs(channel - 188) > 5 for channel in midpoint):
            errors.append(f"Linear-light midpoint must be near sRGB188, observed {midpoint}")
    else:
        has_interpolated_channel = False
        for color in center:
            for channel in color:
                if 20 < channel < 235:
                    has_interpolated_channel = True
        if has_interpolated_channel:
            errors.append("Pre-change midpoint should contain only endpoint colors")
    if variant != "four-colors":
        if any(max(color) - min(color) > 3 for color in row[8:-8]):
            errors.append("Neutral-color probe unexpectedly contains colored pixels")
        endpoint = []
        for x in [width // 8, width * 7 // 8]:
            endpoint.append(statistics.mean(row[x]))
        if min(endpoint) > 5 or max(endpoint) < 250:
            errors.append(f"Uniform endpoint plateaus missing: {endpoint}")
        transition = []
        for x in range(width // 3, width * 2 // 3):
            transition.append(statistics.mean(row[x]))
        if expectation == "smooth":
            direction = 1 if endpoint[1] > endpoint[0] else -1
            for left, right in zip(transition, transition[1:]):
                if direction * (right - left) < -3:
                    errors.append("Transition is not monotonic")
                    break
            if len({round(value) for value in transition}) < 20:
                errors.append("Transition does not contain a continuous color ramp")
    result = {
        "image": str(image),
        "sha256": hashlib.sha256(image.read_bytes()).hexdigest(),
        "variant": variant,
        "expectation": expectation,
        "rectangle": rectangle,
        "dimensions": [width, height],
        "midpoint_rgb": midpoint,
        "passed": not errors,
        "errors": errors,
    }
    if report:
        write_document(report, result)
    print(json.dumps(result, indent=2))
    return 0 if not errors else 1


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    generate_parser = commands.add_parser("generate")
    generate_parser.add_argument("--output", type=Path, required=True)
    variants = ["black-white", "four-colors", "lit-unlit"]
    generate_parser.add_argument("--variant", choices=variants, default="black-white")
    check_parser = commands.add_parser("check")
    check_parser.add_argument("--image", type=Path, required=True)
    check_parser.add_argument("--variant", choices=variants, default="black-white")
    check_parser.add_argument("--expect", choices=["smooth", "step"], default="smooth")
    check_parser.add_argument("--rect", nargs=4, type=int, metavar=("X", "Y", "WIDTH", "HEIGHT"))
    check_parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    if args.command == "generate":
        generate(args.output, args.variant)
        return 0
    return check(args.image, args.variant, args.expect, args.rect, args.report)


if __name__ == "__main__":
    raise SystemExit(main())

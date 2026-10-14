#!/usr/bin/env python3
"""Generate a planar receiver and box shadow probe; never launch the engine.

Compare fresh fixtures at scales 1 and 0.01 using the same capture resolution.
Use --origin to test that translating the complete scene preserves its shadows.
All world distances, including camera clips and shadow bias, scale together.
The receiver has two coplanar triangles with identical analytical normals.
"""

import argparse
import json
import math
from pathlib import Path


def positive_number(value):
    number = float(value)
    if not math.isfinite(number) or number <= 0:
        raise argparse.ArgumentTypeError("Expected a positive finite number")
    return number


def finite_number(value):
    number = float(value)
    if not math.isfinite(number):
        raise argparse.ArgumentTypeError("Expected a finite number")
    return number


def write_document(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")


def generate(args):
    directory = args.directory
    if (directory / "demi.project.json").exists():
        raise ValueError("Choose a fresh directory; existing projects are preserved")
    scale = args.scale

    def scaled(vector):
        return [coordinate * scale for coordinate in vector]

    def placed(vector):
        return [origin + coordinate * scale
                for origin, coordinate in zip(args.origin, vector)]

    def plane_vertex(x, z):
        return [x, args.slope_x * x + args.slope_z * z, z]

    normal = [-args.slope_x, 1, -args.slope_z]
    length = math.sqrt(sum(component * component for component in normal))
    normal = [component / length for component in normal]
    a = plane_vertex(-40, -30)
    b = plane_vertex(-40, 30)
    c = plane_vertex(40, -30)
    d = plane_vertex(40, 30)
    camera = list(args.camera)
    target = list(args.target)
    target_offset = [destination - origin for origin, destination
                     in zip(camera, target)]
    entities = [
        {
            "id": "receiver",
            "components": {
                "Transform3D": {"position": list(args.origin),
                                "scale": [scale, scale, scale]},
                "MeshRenderer": {
                    "vertices": [a, b, c, c, b, d],
                    "normals": [normal] * 6,
                    "color": [0.75, 0.75, 0.75, 1],
                    "metallic": 0,
                    "roughness": 1,
                },
            },
        },
        {
            "id": "occluder",
            "components": {
                "Transform3D": {"position": placed([0, 6, 0]),
                                "scale": [scale, scale, scale]},
                "MeshRenderer": {"size": [4, 8, 4],
                                 "color": [0.65, 0.23, 0.16, 1],
                                 "metallic": 0, "roughness": 1},
            },
        },
        {
            "id": "camera",
            "components": {
                "Transform3D": {"position": placed(camera)},
                "Camera3D": {
                    "target_offset": scaled(target_offset),
                    "near_clip": 0.2 * scale,
                    "far_clip": 300 * scale,
                    "clear_color": [0.12, 0.16, 0.2, 1],
                },
            },
        },
        {
            "id": "sun",
            "components": {
                "DirectionalLight": {"direction": [-0.55, -1, -0.15],
                                     "intensity": 1.5,
                                     "casts_shadows": not args.shadows_off},
            },
        },
        {
            "id": "environment",
            "components": {
                "Environment3D": {"ambient_intensity": 0.15,
                                  "shadow_distance": 220 * scale,
                                  "shadow_resolution": args.resolution,
                                  "shadow_cascades": args.cascades,
                                  "shadow_filter": args.filter,
                                  "shadow_bias": args.bias * scale},
            },
        },
    ]
    scene_id = "scene://shadow_receiver_probe/main"
    write_document(directory / "demi.project.json", {
        "format_version": 1,
        "name": "Planar Shadow Receiver Probe",
        "main_scene": scene_id,
        "scenes": [{"id": scene_id}],
    })
    write_document(directory / "scenes/main.scene.json", {
        "format_version": 1, "id": scene_id,
        "name": "Uniformly scaled planar receiver and box",
        "entities": entities,
    })
    print(f"Fixture: {directory}")
    print(f"Scale: {scale}; receiver normal: {normal}")
    print("Capture at identical window dimensions and camera configuration.")
    print("Expected: smooth receiver outside one localized box shadow; "
          "no repeating self-shadow bands or triangle diagonal seam.")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--scale", type=positive_number, default=1)
    parser.add_argument("--origin", nargs=3, type=finite_number,
                        default=[0, 0, 0], metavar=("X", "Y", "Z"),
                        help="Translate all geometry and the camera in world units")
    parser.add_argument("--slope-x", type=finite_number, default=1.25)
    parser.add_argument("--slope-z", type=finite_number, default=0.15)
    parser.add_argument("--camera", nargs=3, type=finite_number,
                        default=[-60, 72, 80], metavar=("X", "Y", "Z"))
    parser.add_argument("--target", nargs=3, type=finite_number,
                        default=[0, 0, 0], metavar=("X", "Y", "Z"))
    parser.add_argument("--filter", choices=["hard", "pcf"], default="pcf")
    parser.add_argument("--resolution", type=int, choices=[512, 1024, 2048],
                        default=1024)
    parser.add_argument("--cascades", type=int, choices=[1, 2, 3, 4], default=1)
    parser.add_argument("--bias", type=finite_number, default=0.005)
    parser.add_argument("--shadows-off", action="store_true")
    args = parser.parse_args()
    if args.bias < 0:
        parser.error("--bias must be nonnegative")
    if 0.2 * args.scale < 0.001:
        parser.error("Scale must preserve Camera3D's 0.001-unit minimum near clip")
    if args.camera == args.target:
        parser.error("Camera and target must differ")
    generate(args)


if __name__ == "__main__":
    main()

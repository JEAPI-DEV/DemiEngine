#!/usr/bin/env python3
"""Write a deterministic mapped-terrain pixel fixture; never launch or capture.

Root coordinates native runs. The fixture uses a Terrain asset so normal import,
cook and prepared-heightfield loading can exercise the actual material shader.
"""

import argparse
import json
import math
from pathlib import Path
import struct

from terrain_palette_pixel_probe import source_hash, write_document


NAMESPACE = "terrain_material_probe"


def reference(name):
    return f"asset://{NAMESPACE}/{name}"


def write_bitmap(path, color, checker=False):
    edge = 8 if checker else 2
    pixels = bytearray()
    for y in reversed(range(edge)):
        for x in range(edge):
            factor = 0.5 if checker and (x // 2 + y // 2) % 2 else 1.0
            red, green, blue = (round(channel * factor) for channel in color)
            pixels.extend((blue, green, red, 255))
    header = struct.pack("<2sIHHI", b"BM", 54 + len(pixels), 0, 0, 54)
    header += struct.pack("<IiiHHIIiiII", 40, edge, edge, 1, 32, 0,
                          len(pixels), 0, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(header + pixels)


def manifest(path, identity, asset_type, importer, dependencies=(), settings=None):
    write_document(path.with_name(path.stem + ".asset.json"), {
        "format_version": 1,
        "id": identity,
        "type": asset_type,
        "importer": importer,
        "importer_version": 1,
        "source": path.name,
        "source_hash": source_hash(path),
        "dependencies": sorted(set(dependencies)),
        "settings": settings or {},
    })


def generate(directory, variant):
    if directory.exists():
        raise ValueError("Choose a fresh fixture directory; existing source is preserved")
    directory.mkdir(parents=True)
    strength = 0 if variant == "neutral" else 1
    checker = variant == "checker"
    material_ids = []
    for name, color, height in [("red", (255, 0, 0), 255),
                                 ("blue", (0, 0, 255), 0)]:
        map_ids = {}
        for slot, pixel in [("albedo", color), ("height", (height, height, height))]:
            image = directory / "assets" / "textures" / f"{name}-{slot}.bmp"
            write_bitmap(image, pixel, checker=checker and slot == "albedo")
            image_id = reference(f"textures/{name}-{slot}")
            manifest(image, image_id, "Texture2D", "image",
                     settings={"filter": "bilinear", "wrap": "repeat", "mipmaps": True})
            map_ids[slot] = image_id
        material = directory / "assets" / "materials" / f"{name}.material.json"
        material_id = reference(f"materials/{name}")
        write_document(material, {
            "format_version": 1,
            "shader": "builtin://unlit",
            "textures": map_ids,
            "parameters": {"height_blend_strength": strength},
            "render_state": {"cull": "none"},
        })
        manifest(material, material_id, "Material", "material", map_ids.values())
        material_ids.append(material_id)
    recipe = {
        "format_version": 1,
        "size": [3, 3],
        "resolution": [3, 3],
        "chunk_cells": 3,
        "seed": 1704,
        "default_biome": "left",
        "landforms": {"flat": {"base_height": 0, "height_variation": 0}},
        "default_landform": "flat",
        "biomes": {
            "left": {"color": [1, 1, 1, 1], "material": material_ids[0]},
            "right": {"color": [1, 1, 1, 1], "material": material_ids[1]},
        },
        "regions": [{"biome": "right", "center": [x, z], "radius": 0.2}
                    for x in [2, 3] for z in range(4)],
    }
    terrain_source = directory / "assets" / "terrain" / "surface.terrain.json"
    terrain_id = reference("terrain")
    write_document(terrain_source, {
        "format_version": 1, "id": terrain_id, "name": "Mapped layer pixel probe",
        "recipe": recipe,
    })
    manifest(terrain_source, terrain_id, "Terrain", "terrain_heightfield", material_ids)
    scene_id = f"scene://{NAMESPACE}/main"
    write_document(directory / "demi.project.json", {
        "format_version": 1,
        "name": f"Terrain material pixel probe: {variant}",
        "main_scene": scene_id,
        "scenes": [{"id": scene_id}],
        "assets": [terrain_id],
    })
    write_document(directory / "scenes" / "main.scene.json", {
        "format_version": 1,
        "id": scene_id,
        "entities": [
            {"id": "terrain", "components": {
                "Transform3D": {},
                "Terrain3D": {"asset": terrain_id, "lod_error_pixels": 0,
                              "surface_blending": variant != "sharp"},
            }},
            {"id": "camera", "components": {
                "Transform3D": {"position": [1.5, 3, 1.5],
                                "rotation": [math.pi / 2, 0, 0]},
                "Camera3D": {"perspective": False, "orthographic_size": 3,
                             "far_clip": 20, "clear_color": [1, 0, 1, 1]},
            }},
            {"id": "lighting", "components": {
                "Environment3D": {"ambient_color": [0, 0, 0, 1],
                                  "ambient_intensity": 0, "msaa_samples": 0,
                                  "shadow_distance": 0, "terrain_texture_max_size": 64},
            }},
        ],
    })
    write_document(directory / "probe-expectations.json", {
        "variant": variant,
        "height_blend_strength": strength,
        "source_hashes": {str(path.relative_to(directory)): source_hash(path)
                          for path in sorted((directory / "assets").rglob("*"))
                          if path.is_file() and not path.name.endswith(".asset.json")},
        "neutral_middle_rgb": [188, 0, 188],
        "height_middle_rgb_approximate": [255, 0, 2],
        "qualification": "Expected production shader output; no capture or validation performed by this generator",
    })
    (directory / "README.md").write_text(
        "# Mapped terrain pixel probe\n\n"
        f"Variant: `{variant}`. Source-only fixture; the generator launches nothing.\n\n"
        "Validate with `demi validate <directory>` after rebuilding the engine. "
        "Use the ordinary import/cook path for the Terrain asset before the coordinated native run. "
        "Capture a square client area after preparation settles; confirm positive GPU mapped patch "
        "instances and material-array resident bytes, rather than accepting a CPU fallback image.\n\n"
        "The flat terrain covers local [0,3]². Its center is local (1.5,1.5), midway "
        "between red samples at x=1 and blue samples at x=2. There are no lights, shadows, "
        "MSAA, geometric LOD or tint differences. Both materials are opaque and unlit.\n\n"
        "Compare fresh `neutral` and `height` fixtures. At strength zero the center is "
        "linear-light 50/50 red and blue, approximately RGB (188,0,188) after sRGB encoding. "
        "At strength one, red height=1 has factor 2 and blue height=0 retains factor .001: "
        "the center should be approximately (255,0,2). Compare a small symmetric center "
        "patch with tolerance for pixel placement; endpoint plateaus remain pure red/blue.\n\n"
        "`sharp` disables Surface Blending and must produce pure red/blue triangle assignments, "
        "with no continuous purple ramp. Avoid sampling directly on triangle edges. "
        "`checker` adds a real tiled checker to the color maps for visual sampling inspection; "
        "its pixel values are not covered by the uniform-color midpoint expectations.\n",
        encoding="utf-8")
    print(directory / "demi.project.json")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--variant", choices=["height", "neutral", "sharp", "checker"],
                        default="height")
    arguments = parser.parse_args()
    generate(arguments.output, arguments.variant)


if __name__ == "__main__":
    main()

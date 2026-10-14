#!/usr/bin/env python3
"""Write a fresh, deterministic material diagnostic project using only stdlib."""

import argparse
import json
import math
from pathlib import Path
import struct
import zlib


NAMESPACE = "material_surface_probe"
MAP_SIZE = 128


def png_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload
            + struct.pack(">I", zlib.crc32(kind + payload) & 0xFFFFFFFF))


def write_png(path, pixel):
    rows = bytearray()
    for y in range(MAP_SIZE):
        rows.append(0)  # PNG filter None: deterministic RGBA diagnostic samples.
        for x in range(MAP_SIZE):
            rows.extend(pixel(x, y))
    header = struct.pack(">IIBBBBB", MAP_SIZE, MAP_SIZE, 8, 6, 0, 0, 0)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + png_chunk(b"IHDR", header)
                     + png_chunk(b"IDAT", zlib.compress(bytes(rows), 9))
                     + png_chunk(b"IEND", b""))


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n", encoding="utf-8")


def source_hash(path):
    # AssetHash::hashFiles includes each basename, NUL, then file contents.
    digest = 14695981039346656037
    for byte in path.name.encode("utf-8") + b"\0" + path.read_bytes():
        digest = ((digest ^ byte) * 1099511628211) & 0xFFFFFFFFFFFFFFFF
    return f"fnv1a64:{digest:016x}"


def asset(root, relative, identity, kind, importer, document=None,
          dependencies=(), settings=None):
    source = root / relative
    if document is not None:
        write_json(source, document)
    manifest = source.with_name(source.stem + ".asset.json")
    write_json(manifest, {
        "format_version": 1, "id": identity, "type": kind,
        "source": source.name, "importer": importer, "importer_version": 1,
        "source_hash": source_hash(source),
        "dependencies": sorted(set(dependencies)), "settings": settings or {},
    })


def ref(name):
    return f"asset://{NAMESPACE}/{name}"


def gray(value):
    return (value, value, value, 255)


def normal_pixel(x, y, directx, detail=False):
    periods = 8 if detail else 2
    strength = 0.2 if detail else 0.65
    nx = strength * math.cos(2 * math.pi * periods * x / MAP_SIZE)
    ny = strength * math.sin(2 * math.pi * periods * y / MAP_SIZE)
    nz = math.sqrt(max(0, 1 - nx * nx - ny * ny))
    if directx:
        ny = -ny
    return tuple(round((value * 0.5 + 0.5) * 255)
                 for value in (nx, ny, nz)) + (255,)


def entity(identity, components, children=None):
    result = {"id": identity, "components": components}
    if children:
        result["children"] = children
    return result


def generate(root, options):
    if root.exists():
        raise ValueError("Destination already exists; choose a fresh project directory.")
    root.mkdir(parents=True)
    directx = options.normal_convention == "directx"
    pixels = {
        "albedo": lambda x, y: ((210, 64, 38, 255) if (x // 16 + y // 16) % 2
                                  else (36, 150, 205, 255)),
        "normal": lambda x, y: normal_pixel(x, y, directx),
        "detail_normal": lambda x, y: normal_pixel(x, y, directx, True),
        "roughness": lambda x, y: gray(32 if x < MAP_SIZE // 2 else 230),
        "metallic": lambda x, y: gray(0 if y < MAP_SIZE // 2 else 255),
        "ambient_occlusion": lambda x, y: gray(80 if x % 32 < 8 else 255),
        "height": lambda x, y: gray(round(127.5 * (1 + math.sin(x * math.pi / 16)))),
        "emissive": lambda x, y: (0, 0, 0, 255),
    }
    textures = {}
    for name, pixel in pixels.items():
        relative = f"assets/textures/{name}/{name}.png"
        write_png(root / relative, pixel)
        textures[name] = ref("textures/" + name)
        asset(root, relative, textures[name], "Texture2D", "image",
              settings={"filter": "bilinear", "mipmaps": True, "wrap": "repeat"})

    mappings = ["uv", "triplanar"] if options.mapping == "both" else [options.mapping]
    entities = []
    startup_assets = []
    for index, mapping in enumerate(mappings):
        material_id = ref("materials/" + mapping)
        selected_maps = {} if options.scalar_only else textures
        asset(root, f"assets/materials/{mapping}/{mapping}.material.json",
              material_id, "Material", "material", {
                  "format_version": 1, "shader": "builtin://lit",
                  "mapping": mapping, "normal_map_convention": options.normal_convention,
                  "textures": selected_maps,
                  "parameters": {"base_color": [1, 1, 1, 1],
                                 "roughness": options.roughness,
                                 "metallic": options.metallic,
                                 "normal_strength": options.normal_strength,
                                 "tiling": options.tiling,
                                 "detail_strength": 0 if options.scalar_only else options.detail_strength},
                  "render_state": {"blend": "opaque", "cull": "none"},
              }, selected_maps.values())
        material_set = ref("sets/" + mapping)
        asset(root, f"assets/sets/{mapping}/{mapping}.json", material_set,
              "DataAsset", "json_data", {
                  "format_version": 1, "name": mapping + " diagnostic roles",
                  "roles": {role: material_id for role in ("ground", "grass", "rock", "cliff", "sediment",
                                                                     "sand", "snow", "wet_ground", "underwater")},
              }, [material_id], {"content_type": "terrain_material_set"})
        palette = ref("palettes/" + mapping)
        asset(root, f"assets/palettes/{mapping}/{mapping}.json", palette,
              "DataAsset", "json_data", {
                  "format_version": 2, "name": mapping + " surfaces",
                  "material_set": material_set,
              }, [material_set], {"content_type": "terrain_palette"})
        terrain = ref("terrain/" + mapping)
        graph = {
            "format_version": 1, "output": "output",
            "nodes": [
                {"id": "shape", "type": "noise", "parameters": {
                    "profile": "ridged", "base_height": 0, "height_variation": 12,
                    "feature_size": 14, "roughness": 0.4, "octaves": 4}},
                {"id": "appearance", "type": "biomes"},
                {"id": "output", "type": "output"},
            ],
            "links": [
                {"id": "shape_appearance", "from": {"node": "shape", "port": "field"},
                 "to": {"node": "appearance", "port": "field"}},
                {"id": "appearance_output", "from": {"node": "appearance", "port": "field"},
                 "to": {"node": "output", "port": "field"}},
            ],
        }
        asset(root, f"assets/terrain/{mapping}/{mapping}.terrain.json", terrain,
              "Terrain", "terrain_heightfield", {
                  "format_version": 1, "id": terrain, "name": mapping + " steep feature",
                  "recipe": {"format_version": 1, "size": [64, 64], "resolution": [64, 64],
                             "chunk_cells": 16, "seed": 1704, "palette": palette,
                             "default_biome": "default", "biomes": {
                                 "default": {"color": [1, 1, 1, 1]}},
                             "graph": graph},
              }, [palette])
        startup_assets.extend([terrain, material_id])
        offset = index * 80
        entities.append(entity(mapping + "/display", {
            "Transform3D": {"position": [offset, 0, 0]}}, [
                entity(mapping + "/terrain", {"Transform3D": {}, "Terrain3D": {"asset": terrain, "lod_error_pixels": 0}}),
                entity(mapping + "/cube", {"Transform3D": {"position": [20, 4, 74]},
                    "MeshRenderer": {"shape": "cube", "size": [8, 8, 8],
                                     "material": material_id, "color": [1, 1, 1, 1]}}),
                entity(mapping + "/sphere", {"Transform3D": {"position": [44, 4, 74]},
                    "MeshRenderer": {"shape": "sphere", "size": [8, 8, 8],
                                     "material": material_id, "color": [1, 1, 1, 1]}}),
            ]))
    center_x = 32 + 40 * (len(mappings) - 1)
    entities.extend([
        entity("camera", {"Transform3D": {"position": [center_x, 44, 140]},
            "Camera3D": {"target_offset": [0, -36, -95], "far_clip": 400,
                         "fov": 60, "clear_color": [0.08, 0.1, 0.14, 1]}}),
        entity("sun", {"DirectionalLight": {"direction": [-0.5, -1, -0.3],
                                            "intensity": 1.5, "casts_shadows": False}}),
        entity("point", {"Transform3D": {"position": [center_x, 20, 72]},
            "PointLight": {"color": [1, .82, .68, 1], "intensity": 8, "range": 130}}),
        entity("environment", {"Environment3D": {"ambient_intensity": .15}}),
    ])
    write_json(root / "scenes/main.scene.json", {"format_version": 1,
        "id": "scene://material_surface_probe/main", "name": "Material diagnostic comparison",
        "entities": entities})
    write_json(root / "demi.project.json", {"format_version": 1,
        "name": "Material Surface Diagnostic", "main_scene": "scene://material_surface_probe/main",
        "scenes": [{"id": "scene://material_surface_probe/main", "path": "scenes/main.scene.json"}],
        "assets": startup_assets})
    (root / "README.md").write_text(
        "# Material surface diagnostic\n\n"
        "Generated diagnostic maps, not realistic art. Default view compares UV (left) "
        "and triplanar (right). Each terrain, cube and sphere uses exactly the same ordinary "
        "Material asset within its column; every terrain role binds that asset too.\n\n"
        "Red/cyan checks show mapping. Normal grooves test tangent orientation; generated "
        "green signs match the selected normal convention. Roughness and metallic halves "
        "test scalar channels; dark AO stripes test indirect occlusion. Height is a shading "
        "input, never terrain displacement or collision. Emissive is deliberately black.\n\n"
        "The original 64-by-64 grid is identical in both columns. White mesh/biome tints "
        "isolate material response. Shadows are disabled to isolate material lighting. "
        "Use --scalar-only, --normal-strength 0, --detail-strength 0, or --mapping uv/triplanar "
        "when generating fresh projects for differential captures. No app is launched.\n\n"
        f"Generation options: `{vars(options)}`\n", encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--mapping", choices=("both", "uv", "triplanar"), default="both")
    parser.add_argument("--normal-convention", choices=("opengl", "directx"), default="opengl")
    parser.add_argument("--scalar-only", action="store_true")
    parser.add_argument("--normal-strength", type=float, default=1)
    parser.add_argument("--detail-strength", type=float, default=.25)
    parser.add_argument("--tiling", type=float, default=.25)
    parser.add_argument("--roughness", type=float, default=.8)
    parser.add_argument("--metallic", type=float, default=0)
    options = parser.parse_args()
    for key in ("normal_strength", "detail_strength", "tiling", "roughness", "metallic"):
        value = getattr(options, key)
        if not math.isfinite(value) or value < 0 or (key == "tiling" and value == 0):
            parser.error(f"{key} requires a finite {'positive' if key == 'tiling' else 'nonnegative'} value")
    if options.roughness > 1 or options.metallic > 1:
        parser.error("roughness and metallic must be at most one")
    try:
        generate(options.directory, options)
    except ValueError as error:
        parser.error(str(error))
    print(options.directory / "demi.project.json")


if __name__ == "__main__":
    main()

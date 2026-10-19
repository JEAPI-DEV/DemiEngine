#!/usr/bin/env python3
"""Rebuild the original indexed vegetation meshes, without third-party art/tools."""

import base64
import json
import math
from pathlib import Path
import struct


ROOT = Path(__file__).resolve().parents[1]


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, indent=2) + "\n")


class Mesh:
    def __init__(self):
        self.positions = []
        self.normals = []
        self.indices = []

    def face(self, points):
        first = len(self.positions)
        a, b, c = points[:3]
        u = [b[i] - a[i] for i in range(3)]
        v = [c[i] - a[i] for i in range(3)]
        normal = [u[1] * v[2] - u[2] * v[1],
                  u[2] * v[0] - u[0] * v[2],
                  u[0] * v[1] - u[1] * v[0]]
        length = math.sqrt(sum(value * value for value in normal))
        normal = [value / length for value in normal]
        self.positions.extend(points)
        self.normals.extend([normal] * len(points))
        for index in range(1, len(points) - 1):
            self.indices.extend([first, first + index, first + index + 1])

    def cone(self, center, radius, height, sides, top_radius=0):
        x, y, z = center
        for side in range(sides):
            angle = side * math.tau / sides
            next_angle = (side + 1) * math.tau / sides
            bottom = (x + radius * math.cos(angle), y, z + radius * math.sin(angle))
            next_bottom = (x + radius * math.cos(next_angle), y, z + radius * math.sin(next_angle))
            top = (x + top_radius * math.cos(angle), y + height, z + top_radius * math.sin(angle))
            next_top = (x + top_radius * math.cos(next_angle), y + height, z + top_radius * math.sin(next_angle))
            if top_radius:
                self.face([bottom, top, next_top, next_bottom])
            else:
                self.face([bottom, top, next_bottom])
            self.face([(x, y, z), bottom, next_bottom])

    def stone(self, center, radii, sides, rings):
        x, y, z = center
        rx, ry, rz = radii
        # Flat facets retain an intentionally sculpted silhouette at each LOD.
        vertices = []
        for ring in range(rings + 1):
            latitude = -math.pi / 2 + (ring + 0.15) * math.pi / (rings + 0.3)
            row = []
            for side in range(sides):
                angle = side * math.tau / sides + ring * 0.13
                wobble = 1 + 0.12 * math.sin(side * 7 + ring * 3)
                row.append((x + rx * math.cos(latitude) * math.cos(angle) * wobble,
                            y + ry * math.sin(latitude),
                            z + rz * math.cos(latitude) * math.sin(angle) * wobble))
            vertices.append(row)
        for ring in range(rings):
            for side in range(sides):
                following = (side + 1) % sides
                self.face([vertices[ring][side], vertices[ring + 1][side],
                           vertices[ring + 1][following], vertices[ring][following]])
        for side in range(sides):
            following = (side + 1) % sides
            self.face([(x, y - ry, z), vertices[0][side], vertices[0][following]])
            self.face([(x, y + ry, z), vertices[-1][following], vertices[-1][side]])


def save_model(name, mesh):
    blob = bytearray()
    views = []
    accessors = []

    def append(values, width, code, component_type, accessor_type):
        while len(blob) % 4:
            blob.append(0)
        offset = len(blob)
        flat = [channel for value in values for channel in value] if width > 1 else values
        blob.extend(struct.pack("<" + code * len(flat), *flat))
        view = len(views)
        views.append({"buffer": 0, "byteOffset": offset, "byteLength": len(blob) - offset})
        accessor = {"bufferView": view, "componentType": component_type,
                    "count": len(values), "type": accessor_type}
        if accessor_type == "VEC3":
            accessor["min"] = [min(value[i] for value in values) for i in range(3)]
            accessor["max"] = [max(value[i] for value in values) for i in range(3)]
        accessors.append(accessor)
        return len(accessors) - 1

    position = append(mesh.positions, 3, "f", 5126, "VEC3")
    normal = append(mesh.normals, 3, "f", 5126, "VEC3")
    indices = append(mesh.indices, 1, "H", 5123, "SCALAR")
    document = {
        "asset": {"version": "2.0", "generator": "Demi original vegetation make_models.py"},
        "scene": 0, "scenes": [{"nodes": [0]}], "nodes": [{"mesh": 0}],
        "meshes": [{"name": name, "primitives": [{"attributes": {"POSITION": position, "NORMAL": normal}, "indices": indices}]}],
        "buffers": [{"byteLength": len(blob), "uri": "data:application/octet-stream;base64," + base64.b64encode(blob).decode()}],
        "bufferViews": views, "accessors": accessors,
    }
    directory = ROOT / "assets/models" / name
    source_path = directory / (name + ".gltf")
    write_json(source_path, document)
    source_hash = 14695981039346656037
    source_bytes = source_path.name.encode() + b"\0" + source_path.read_bytes()
    for byte in source_bytes:
        source_hash = ((source_hash ^ byte) * 1099511628211) & ((1 << 64) - 1)
    write_json(directory / (name + ".asset.json"), {
        "format_version": 1, "id": "asset://demi/vegetation/models/" + name,
        "type": "Model3D", "source": name + ".gltf", "importer": "gltf-model",
        "importer_version": 1, "source_hash": f"fnv1a64:{source_hash:016x}",
        "dependencies": [], "settings": {
            "model_import": {"format_version": 1, "preset": "static_prop", "source_up": "+y",
                             "source_forward": "+z", "meters_per_unit": 1,
                             "root_node": "preserve", "material_policy": "import",
                             "import_animations": False, "optimize_meshes": True}}})


def make_models():
    for detail, sides, rings, blades in [("", 10, 6, 9), ("_medium", 7, 4, 5), ("_low", 4, 2, 3)]:
        for name, height in [("grass", 0.65), ("reed", 1.8)]:
            mesh = Mesh()
            for blade in range(blades):
                angle = blade * 2.399963
                radius = 0.07 * math.sqrt(blade)
                x, z = radius * math.cos(angle), radius * math.sin(angle)
                width = 0.055 if name == "grass" else 0.035
                tip_height = height * (0.75 + 0.25 * math.sin(blade * 3.1 + 1))
                bend = 0.16 if name == "grass" else 0.08
                dx, dz = math.cos(angle) * width, math.sin(angle) * width
                mesh.face([(x - dx, 0, z - dz), (x + dx, 0, z + dz),
                           (x + dx + bend, tip_height * 0.65, z + dz),
                           (x + bend * 1.5, tip_height, z)])
            save_model(name + detail, mesh)
        trunk = Mesh()
        trunk.cone((0, 0, 0), 0.21, 3.5, sides, 0.1)
        save_model("trunk" + detail, trunk)
        oak = Mesh()
        oak.stone((0, 3.8, 0), (1.9, 1.65, 1.7), sides, rings)
        if not detail.endswith("low"):
            oak.stone((0.95, 3.2, 0.3), (1.2, 1.1, 1.1), sides, rings)
            oak.stone((-0.8, 3.4, -0.4), (1.3, 1.3, 1.2), sides, rings)
        save_model("oak_crown" + detail, oak)
        pine = Mesh()
        for level in range(3):
            pine.cone((0, 1.4 + level * 0.9, 0), 1.5 - level * 0.3, 2.1, sides)
        save_model("pine_crown" + detail, pine)
        boulder = Mesh()
        boulder.stone((0, 0.5, 0), (1.1, 0.8, 0.9), sides, rings)
        save_model("boulder" + detail, boulder)


if __name__ == "__main__":
    make_models()

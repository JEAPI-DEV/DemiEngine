#!/usr/bin/env python3
"""Check the shipped source mesh payloads and actual detail reduction."""

import base64
import json
import math
from pathlib import Path
import struct
import unittest


ROOT = Path(__file__).resolve().parents[1]


def load_mesh(name):
    path = ROOT / "assets/models" / name / (name + ".gltf")
    model = json.loads(path.read_text())
    data = base64.b64decode(model["buffers"][0]["uri"].split(",", 1)[1])
    primitive = model["meshes"][0]["primitives"][0]

    def unpack(accessor_index, code, channels):
        accessor = model["accessors"][accessor_index]
        view = model["bufferViews"][accessor["bufferView"]]
        offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
        count = accessor["count"] * channels
        values = struct.unpack_from("<" + code * count, data, offset)
        if channels == 1:
            return values
        return [values[index:index + channels] for index in range(0, count, channels)]

    positions = unpack(primitive["attributes"]["POSITION"], "f", 3)
    normals = unpack(primitive["attributes"]["NORMAL"], "f", 3)
    indices = unpack(primitive["indices"], "H", 1)
    return positions, normals, indices


class VegetationModelsTest(unittest.TestCase):
    def test_tree_parts_share_visibility_distances(self):
        for name in ["oak", "pine"]:
            path = ROOT / "prefabs" / (name + ".prefab.json")
            tree = json.loads(path.read_text())["entities"][0]
            trunk = tree["components"]["MeshRenderer"]
            crown_entity = tree["children"][0]
            crown = crown_entity["components"]["MeshRenderer"]
            # Both meshes use the same origin, so shared cutoffs prevent bare
            # poles and detached crowns when either part changes detail.
            self.assertEqual(crown_entity["components"]["Transform3D"], {})
            for field in ["medium_lod_distance", "low_lod_distance", "cull_distance"]:
                self.assertEqual(trunk[field], crown[field])

    def test_source_geometry_and_lods(self):
        for name in ["grass", "reed", "trunk", "oak_crown", "pine_crown", "boulder"]:
            previous_triangles = None
            for suffix in ["", "_medium", "_low"]:
                with self.subTest(model=name + suffix):
                    positions, normals, indices = load_mesh(name + suffix)
                    self.assertEqual(len(positions), len(normals))
                    self.assertEqual(len(indices) % 3, 0)
                    self.assertLess(max(indices), len(positions))
                    for point in positions:
                        self.assertTrue(all(math.isfinite(channel) for channel in point))
                    for normal in normals:
                        self.assertAlmostEqual(sum(value * value for value in normal), 1, places=5)
                    triangles = len(indices) // 3
                    if previous_triangles is not None:
                        self.assertLess(triangles, previous_triangles)
                    previous_triangles = triangles
                    # Every indexed triangle must span an actual surface.
                    for index in range(0, len(indices), 3):
                        a, b, c = [positions[value] for value in indices[index:index + 3]]
                        u = [b[channel] - a[channel] for channel in range(3)]
                        v = [c[channel] - a[channel] for channel in range(3)]
                        cross = [u[1] * v[2] - u[2] * v[1],
                                 u[2] * v[0] - u[0] * v[2],
                                 u[0] * v[1] - u[1] * v[0]]
                        self.assertGreater(sum(value * value for value in cross), 1e-12)


if __name__ == "__main__":
    unittest.main()

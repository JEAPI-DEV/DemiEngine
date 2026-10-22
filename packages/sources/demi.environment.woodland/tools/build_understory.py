"""Export intact CC0 source plants; run with Blender -b --python this_file -- CACHE.

Fetch the sources listed in sources-understory.json first. Original mesh/UVs
are retained. Only placement, uniform scale and source-object grouping change.
"""
from pathlib import Path
import json
import sys

import bpy
from mathutils import Matrix, Vector

PACKAGE = Path(__file__).resolve().parents[1]
arguments = sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else []
CACHE = Path(arguments[0]) if arguments else Path('/tmp/demi-understory')
OUTPUT = PACKAGE / 'assets/understory/models'
OUTPUT.mkdir(parents=True, exist_ok=True)
summary = []


def source_object(name):
    return next(obj for obj in bpy.data.objects if obj.type == 'MESH' and obj.name == name)


def plant_copy(source, height, offset=(0, 0)):
    obj = source.copy()
    obj.data = source.data.copy()
    bpy.context.collection.objects.link(obj)
    # Bake the imported world transform before centering source lineup variants.
    obj.data.transform(source.matrix_world)
    obj.matrix_world = Matrix.Identity(4)
    points = [vertex.co for vertex in obj.data.vertices]
    minimum = Vector(tuple(min(point[axis] for point in points) for axis in range(3)))
    maximum = Vector(tuple(max(point[axis] for point in points) for axis in range(3)))
    scale = height / (maximum.z - minimum.z)
    center = Vector(((minimum.x + maximum.x) / 2, (minimum.y + maximum.y) / 2, minimum.z))
    for vertex in obj.data.vertices:
        vertex.co = (vertex.co - center) * scale + Vector((offset[0], offset[1], 0))
    while len(obj.data.uv_layers) > 1:
        obj.data.uv_layers.remove(obj.data.uv_layers[-1])
    return obj


def export(name, parts):
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    if len(parts) > 1:
        bpy.ops.object.join()
    obj = bpy.context.view_layer.objects.active
    obj.name = name
    obj.data.calc_loop_triangles()
    path = OUTPUT / (name + '.glb')
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True,
                              export_materials='NONE', export_animations=False)
    points = [Vector((vertex.co.x, vertex.co.z, -vertex.co.y)) for vertex in obj.data.vertices]
    summary.append({'name': name, 'triangles': len(obj.data.loop_triangles),
                    'bytes': path.stat().st_size,
                    'bounds_min': [min(point[axis] for point in points) for axis in range(3)],
                    'bounds_max': [max(point[axis] for point in points) for axis in range(3)]})
    bpy.data.objects.remove(obj, do_unlink=True)


species = [
    ('grass_medium_01', 'grass_lush', 0.65,
     ['grass_medium_01_large_a_LOD0', 'grass_medium_01_large_b_LOD0']),
    ('shrub_03', 'shrub', 0.9, ['shrub_03_a', 'shrub_03_b', 'shrub_03_c']),
    ('shrub_sorrel_01', 'sorrel', 0.18,
     ['shrub_sorrel_01_d', 'shrub_sorrel_01_b', 'shrub_sorrel_01_a']),
    ('flower_gazania', 'wildflowers', 0.4,
     ['flower_gazania_h_LOD0', 'flower_gazania_f_LOD0', 'flower_gazania_c_LOD0']),
]
for source_id, name, height, variants in species:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(CACHE / source_id / (source_id + '_1k.gltf')))
    for variant, suffix in zip(variants, ['', '_medium', '_low']):
        export(name + suffix, [plant_copy(source_object(variant), height)])
    if name == 'grass_lush':
        # These are complete authored plants, not reskinned crossed billboards.
        # Broadly distributed bases preserve a clump footprint at distance.
        variants = ['grass_medium_01_tall_a_LOD0', 'grass_medium_01_tall_b_LOD0',
                    'grass_medium_01_tall_c_LOD0']
        offsets = [(-0.35, 0), (0.28, 0.25), (0.12, -0.32)]
        export(name + '_low', [plant_copy(source_object(variant), height, offset)
                              for variant, offset in zip(variants, offsets)])
(PACKAGE / 'understory-summary.json').write_text(json.dumps(summary, indent=2) + '\n')

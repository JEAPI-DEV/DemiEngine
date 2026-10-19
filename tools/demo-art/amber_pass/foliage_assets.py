"""Preserve complete photo-textured leaf cards and official grass patch LODs.
Run after environment_assets.py using an isolated background Blender process.
"""
from pathlib import Path
import json
import shutil
import numpy as np
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'examples/amber_pass_3d/assets/environment'
CACHE = Path('/tmp/demi-environment')
summary = json.loads((OUT / 'summary.json').read_text())

def export(obj, name, role):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    # Native geometry consumes only TEXCOORD_0.
    while len(obj.data.uv_layers) > 1:
        obj.data.uv_layers.remove(obj.data.uv_layers[-1])
    obj.data.calc_loop_triangles()
    path = OUT / (name + '.glb')
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True, export_materials='NONE', export_animations=False)
    points = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    points = [Vector((p.x, p.z, -p.y)) for p in points]
    entry = {'name': name, 'file': path.name, 'triangles': len(obj.data.loop_triangles), 'bytes': path.stat().st_size, 'material': role, 'bounds_min': [min(p[i] for p in points) for i in range(3)], 'bounds_max': [max(p[i] for p in points) for i in range(3)]}
    summary['models'] = [item for item in summary['models'] if item['name'] != name] + [entry]

# Recover connected original leaf surfaces before simplifying them into complete
# textured cards. Per-triangle thinning destroys a leaf's UV coverage.
folder = CACHE / 'tree_small_02'
document = json.loads((folder / 'tree_small_02_2k.gltf').read_text())
binary = (folder / 'tree_small_02.bin').read_bytes()
def accessor(index):
    item = document['accessors'][index]
    view = document['bufferViews'][item['bufferView']]
    width = {'SCALAR': 1, 'VEC2': 2, 'VEC3': 3}[item['type']]
    dtype = {5126: 'float32', 5125: 'uint32', 5123: 'uint16'}[item['componentType']]
    return np.frombuffer(binary, dtype=dtype, count=item['count'] * width, offset=view.get('byteOffset', 0) + item.get('byteOffset', 0)).reshape(-1, width)
primitive = document['meshes'][0]['primitives'][1]
positions = accessor(primitive['attributes']['POSITION'])
uvs = accessor(primitive['attributes']['TEXCOORD_0'])
triangles = accessor(primitive['indices']).reshape(-1, 3)
parents = list(range(len(positions)))
def find(index):
    while parents[index] != index:
        parents[index] = parents[parents[index]]
        index = parents[index]
    return index
for first, second, third in triangles.tolist():
    parent = find(first)
    parents[find(second)] = parent
    parents[find(third)] = parent
labels = np.array([find(index) for index in range(len(positions))])
_, labels = np.unique(labels, return_inverse=True)
order = np.argsort(labels, kind='stable')
starts = np.flatnonzero(np.r_[True, np.diff(labels[order]) != 0])
components = np.split(order, starts[1:])
# A fixed multiplicative permutation spreads retained complete leaves throughout
# the entire canopy, without selecting a single side or vertical band.
selection = sorted(range(len(components)), key=lambda index: (index * 2654435761) % 4294967296)
cards = []
for component_index in selection[:7000]:
    indices = components[component_index]
    coordinates = uvs[indices]
    lower, upper = coordinates.min(axis=0), coordinates.max(axis=0)
    design = np.column_stack((coordinates, np.ones(len(indices))))
    fit = np.linalg.lstsq(design, positions[indices], rcond=None)[0]
    quad_uv = np.array([(lower[0], lower[1]), (upper[0], lower[1]), (upper[0], upper[1]), (lower[0], upper[1])])
    quad = np.column_stack((quad_uv, np.ones(4))) @ fit
    center = quad.mean(axis=0)
    quad = center + (quad - center) * 1.85
    cards.append((quad, quad_uv))

bpy.ops.wm.read_factory_settings(use_empty=True)
for name, count in [('tree_leaves', 7000), ('tree_leaves_mid', 2000)]:
    vertices, faces, coordinates = [], [], []
    for quad, quad_uv in cards[:count]:
        offset = len(vertices)
        vertices.extend((float(p[0]), float(-p[2]), float(p[1])) for p in quad)
        faces.append((offset, offset + 1, offset + 2, offset + 3))
        coordinates.extend((float(uv[0]), float(1 - uv[1])) for uv in quad_uv)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    layer = mesh.uv_layers.new(name='UV0')
    for loop in mesh.loops:
        layer.data[loop.index].uv = coordinates[loop.vertex_index]
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    export(obj, name, 'leaves')
    if name.endswith('_mid'):
        bpy.data.objects.remove(obj, do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'autumn_canopy.blend'))

# Keep the editable tree source synchronized with the final complete-card canopy.
bpy.ops.wm.open_mainfile(filepath=str(OUT / 'tree_small_02.blend'))
for obj in list(bpy.data.objects):
    if obj.type == 'MESH' and obj.get('material_role') == 'leaves':
        bpy.data.objects.remove(obj, do_unlink=True)
bpy.ops.import_scene.gltf(filepath=str(OUT / 'tree_leaves.glb'))
leaves = bpy.context.selected_objects[0]
leaves['material_role'] = 'leaves'
material = next(mat for mat in bpy.data.materials if 'leaves' in mat.name)
for node in material.node_tree.nodes:
    if node.type == 'TEX_IMAGE' and node.image and 'leaves_diff' in node.image.name:
        node.image = bpy.data.images.load(str(OUT / 'textures/autumn_leaves.png'))
        node.image.pack()
leaves.data.materials.append(material)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'tree_small_02.blend'))

# Poly Haven supplies multiple game-ready patch variants. Use authored variants
# for each LOD rather than collapsing disconnected grass blades into fragments.
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(CACHE / 'grass_medium_01/grass_medium_01_1k.gltf'))
objects = [obj for obj in bpy.data.objects if obj.type == 'MESH']
variants = [('grass_patch', 'small_a'), ('grass_patch_mid', 'tall_a'), ('grass_patch_low', 'tiny_c')]
target_dimensions = None
kept = []
for name, variant in variants:
    obj = next(obj for obj in objects if ('_' + variant + '_') in obj.name)
    points = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
    low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
    high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
    obj.data.transform(obj.matrix_world)
    obj.matrix_world.identity()
    center = Vector(((low.x + high.x) / 2, (low.y + high.y) / 2, low.z))
    for vertex in obj.data.vertices:
        vertex.co -= center
    if target_dimensions is None:
        target_dimensions = high - low
    else:
        dimensions = high - low
        scales = tuple(target_dimensions[i] / max(.001, dimensions[i]) for i in range(3))
        for vertex in obj.data.vertices:
            for axis in range(3):
                vertex.co[axis] *= scales[axis]
    export(obj, name, 'grass_photo')
    kept.append(obj)
for obj in list(bpy.data.objects):
    if obj not in kept:
        bpy.data.objects.remove(obj, do_unlink=True)
for image in bpy.data.images:
    if image.has_data and not image.packed_file:
        image.pack()
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'grass_patches.blend'))
for path in (CACHE / 'grass_medium_01/textures').iterdir():
    shutil.copy2(path, OUT / 'textures' / path.name)

color = bpy.data.images.load(str(OUT / 'textures/grass_medium_01_diff_1k.jpg'))
alpha = bpy.data.images.load(str(OUT / 'textures/grass_medium_01_alpha_1k.png'))
alpha.colorspace_settings.name = 'Non-Color'
width, height = color.size
values = np.empty(width * height * 4, dtype=np.float32)
opacity = np.empty_like(values)
color.pixels.foreach_get(values)
alpha.pixels.foreach_get(opacity)
values = values.reshape(height, width, 4)
values[:,:,3] = opacity.reshape(height, width, 4)[:,:,0]
# Gently yellow the original photo color for late-season grass.
values[:,:,0] *= 1.15
values[:,:,1] *= .92
values[:,:,2] *= .75
image = bpy.data.images.new('Grass photo RGBA', width=width, height=height, alpha=True)
image.pixels.foreach_set(values.ravel())
image.filepath_raw = str(OUT / 'textures/grass_photo.png')
image.file_format = 'PNG'
image.save()
summary['materials']['grass_photo'] = {'albedo':'textures/grass_photo.png', 'normal':'textures/grass_medium_01_nor_gl_1k.jpg', 'arm':'textures/grass_medium_01_arm_1k.jpg', 'ao':'textures/grass_medium_01_ao_1k.png', 'roughness':'textures/grass_medium_01_roughness_1k.png', 'alpha_cutoff':.5, 'double_sided':True, 'source':'https://polyhaven.com/a/grass_medium_01'}
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
print('FOLIAGE_COMPLETE')

# Add complete photographed tuft clusters to the detailed native grass LODs.
import runpy
runpy.run_path(str(Path(__file__).with_name('grass_clusters.py')))

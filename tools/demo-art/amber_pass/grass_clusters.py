"""Build dense photo-grass clusters with authored 3D blades and complete tuft cards.
Run: blender -b --python tools/demo-art/amber_pass/grass_clusters.py
"""
from pathlib import Path
import json
import math
import numpy as np
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'examples/amber_pass_3d/assets/environment'
CACHE = Path('/tmp/demi-environment')
summary = json.loads((OUT / 'summary.json').read_text())
# Lift the original photographic colors in linear light, without baking lights
# or changing cutout coverage. This avoids accumulated exposure on rebuilds.
color = bpy.data.images.load(str(OUT / 'textures/grass_medium_01_diff_1k.jpg'))
alpha = bpy.data.images.load(str(OUT / 'textures/grass_medium_01_alpha_1k.png'))
alpha.colorspace_settings.name = 'Non-Color'
width, height = color.size
pixels = np.empty(width * height * 4, dtype=np.float32)
opacity = np.empty_like(pixels)
color.pixels.foreach_get(pixels)
alpha.pixels.foreach_get(opacity)
pixels = pixels.reshape(height, width, 4)
pixels[:,:,:3] = np.clip(pixels[:,:,:3] * np.array([3.15, 3.0, 2.55]), 0, 1)
pixels[:,:,3] = opacity.reshape(height, width, 4)[:,:,0]
bright = bpy.data.images.new('Grass photographic exposure', width=width, height=height, alpha=True)
bright.pixels.foreach_set(pixels.ravel())
bright.filepath_raw = str(OUT / 'textures/grass_photo.png')
bright.file_format = 'PNG'
bright.save()
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(CACHE / 'grass_medium_01/grass_medium_01_1k.gltf'))
source = next(obj for obj in bpy.data.objects if obj.type == 'MESH' and '_large_b_' in obj.name)
source.data.transform(source.matrix_world)
source.matrix_world.identity()
points = [vertex.co for vertex in source.data.vertices]
minimum = Vector(tuple(min(p[i] for p in points) for i in range(3)))
maximum = Vector(tuple(max(p[i] for p in points) for i in range(3)))
center = Vector(((minimum.x + maximum.x) / 2, (minimum.y + maximum.y) / 2, minimum.z))
scale = Vector((.28 / (maximum.x - minimum.x), .24 / (maximum.y - minimum.y), .16 / (maximum.z - minimum.z)))
for vertex in source.data.vertices:
    vertex.co = (vertex.co - center) * scale

# Source atlas pixels contain photographs of complete dense grass tufts. These
# cards preserve those entire cutouts instead of resampling isolated thin blades.
PATCHES = [(180, 780, 485, 925), (630, 775, 890, 900)]
def tuft_cards(name, angles):
    vertices, faces, coordinates = [], [], []
    tufts = [(-.045, -.02, 0, .095), (.045, .03, .7, .135)]
    for index, (center_x, center_y, phase, tuft_height) in enumerate(tufts):
        left, top, right, bottom = PATCHES[index % len(PATCHES)]
        uv = [(left / 1024, 1 - bottom / 1024), (right / 1024, 1 - bottom / 1024),
              (right / 1024, 1 - top / 1024), (left / 1024, 1 - top / 1024)]
        for angle in angles:
            theta = angle + phase
            half = Vector((math.cos(theta) * .105, math.sin(theta) * .105, 0))
            foot = Vector((center_x, center_y, 0))
            offset = len(vertices)
            vertices.extend((foot - half, foot + half, foot + half + Vector((.015, 0, tuft_height)), foot - half + Vector((.01, 0, tuft_height * .8))))
            faces.append((offset, offset + 1, offset + 2, offset + 3))
            coordinates.extend(uv)
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    layer = mesh.uv_layers.new(name='UV0')
    for loop in mesh.loops:
        layer.data[loop.index].uv = coordinates[loop.vertex_index]
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return obj

produced = []
for name, blade_budget, angles in [
    ('grass_patch', 1000, (0, math.pi / 2)),
    ('grass_patch_mid', 230, (0, math.pi / 2)),
    ('grass_patch_low', 0, (0, math.pi / 2)),
]:
    cards = tuft_cards(name, angles)
    bpy.ops.object.select_all(action='DESELECT')
    cards.select_set(True)
    bpy.context.view_layer.objects.active = cards
    if blade_budget:
        blades = source.copy()
        blades.data = source.data.copy()
        bpy.context.collection.objects.link(blades)
        blades.select_set(True)
        bpy.context.view_layer.objects.active = blades
        blades.data.calc_loop_triangles()
        modifier = blades.modifiers.new('Retain 3D blade silhouette', 'DECIMATE')
        modifier.ratio = blade_budget / len(blades.data.loop_triangles)
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
        # Both meshes explicitly use the original atlas as their UV0 source.
        while len(blades.data.uv_layers) > 1:
            blades.data.uv_layers.remove(blades.data.uv_layers[-1])
        blades.data.uv_layers.active.name = 'UV0'
        bpy.context.view_layer.objects.active = cards
        bpy.ops.object.join()
    # Keep every LOD on the same foot plane and exact coverage bounds.
    low = Vector(tuple(min(v.co[i] for v in cards.data.vertices) for i in range(3)))
    high = Vector(tuple(max(v.co[i] for v in cards.data.vertices) for i in range(3)))
    center = Vector(((low.x + high.x) / 2, (low.y + high.y) / 2, low.z))
    scales = Vector((.32 / (high.x - low.x), .32 / (high.y - low.y), .16 / (high.z - low.z)))
    for vertex in cards.data.vertices:
        vertex.co = (vertex.co - center) * scales
    cards.data.materials.clear()
    cards.data.calc_loop_triangles()
    path = OUT / (name + '.glb')
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB', use_selection=True, export_materials='NONE', export_animations=False)
    points = [Vector((v.co.x, v.co.z, -v.co.y)) for v in cards.data.vertices]
    entry = {'name': name, 'file': path.name, 'triangles': len(cards.data.loop_triangles), 'bytes': path.stat().st_size,
             'material': 'grass_photo', 'bounds_min': [min(p[i] for p in points) for i in range(3)],
             'bounds_max': [max(p[i] for p in points) for i in range(3)]}
    summary['models'] = [item for item in summary['models'] if item['name'] != name] + [entry]
    produced.append(cards)
for obj in list(bpy.data.objects):
    if obj not in produced:
        bpy.data.objects.remove(obj, do_unlink=True)
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'grass_patches.blend'))
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')

# Inspect the full clump with the exact RGBA atlas used by native rendering.
for obj in produced[1:]:
    obj.hide_render = True
mat = bpy.data.materials.new('Grass photo inspection')
mat.use_nodes = True
shader = mat.node_tree.nodes.get('Principled BSDF')
tex = mat.node_tree.nodes.new('ShaderNodeTexImage')
tex.image = bpy.data.images.load(str(OUT / 'textures/grass_photo.png'))
mat.node_tree.links.new(tex.outputs['Color'], shader.inputs['Base Color'])
cutout = mat.node_tree.nodes.new('ShaderNodeMath')
cutout.operation = 'GREATER_THAN'
cutout.inputs[1].default_value = .5
mat.node_tree.links.new(tex.outputs['Alpha'], cutout.inputs[0])
mat.node_tree.links.new(cutout.outputs[0], shader.inputs['Alpha'])
shader.inputs['Roughness'].default_value = .9
produced[0].data.materials.append(mat)
scene = bpy.context.scene
scene.world = bpy.data.worlds.new('Inspection world')
scene.world.color = (.3, .3, .3)
scene.render.engine = 'CYCLES'
scene.cycles.samples = 16
bpy.ops.object.camera_add(location=(.5, -.7, .35))
camera = bpy.context.object
camera.rotation_euler = (Vector((0, 0, .08)) - camera.location).to_track_quat('-Z', 'Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = .45
scene.camera = camera
bpy.ops.object.light_add(type='AREA', location=(.3, -.3, .7))
bpy.context.object.data.energy = 15
bpy.context.object.data.size = .5
scene.render.resolution_x = 800
scene.render.resolution_y = 600
scene.render.resolution_percentage = 100
scene.render.filepath = '/tmp/demi-environment/grass-cluster-preview.png'
bpy.ops.render.render(write_still=True)
print('GRASS_CLUSTERS_FINISHED')

"""Author the distant lush-grass LOD from four real near-mesh alpha views.

Run after build_understory.py and prepare_understory.py. This is one package's
source-art bake, not a runtime impostor system. Requires background Blender.
"""
from pathlib import Path
import json
import math

import bpy
import numpy as np
from mathutils import Vector

PACKAGE = Path(__file__).resolve().parents[1]
MODELS = PACKAGE / 'assets/understory/models'
BAKES = PACKAGE / 'previews/grass_lush_bake'
BAKES.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(MODELS / 'grass_lush.glb'))
plants = [obj for obj in bpy.context.selected_objects if obj.type == 'MESH']
assert len(plants) == 1
plant = plants[0]
material = bpy.data.materials.new('Original photograph, unlit alpha bake')
material.use_nodes = True
nodes = material.node_tree.nodes
nodes.clear()
texture = nodes.new('ShaderNodeTexImage')
texture.image = bpy.data.images.load(str(PACKAGE / 'assets/understory/textures/grass_lush/albedo.png'))
assert tuple(texture.image.size) == (1024, 1024), 'Regenerate original source maps before rebaking an atlas'
emission = nodes.new('ShaderNodeEmission')
transparent = nodes.new('ShaderNodeBsdfTransparent')
mix = nodes.new('ShaderNodeMixShader')
output = nodes.new('ShaderNodeOutputMaterial')
links = material.node_tree.links
links.new(texture.outputs['Color'], emission.inputs['Color'])
links.new(texture.outputs['Alpha'], mix.inputs[0])
links.new(transparent.outputs[0], mix.inputs[1])
links.new(emission.outputs[0], mix.inputs[2])
links.new(mix.outputs[0], output.inputs['Surface'])
plant.data.materials.clear()
plant.data.materials.append(material)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 32
scene.cycles.use_denoising = False
scene.cycles.transparent_max_bounces = 32
scene.cycles.max_bounces = 2
scene.render.resolution_x = 512
scene.render.resolution_y = 512
scene.render.resolution_percentage = 100
scene.render.film_transparent = True
scene.render.image_settings.file_format = 'PNG'
scene.render.image_settings.color_mode = 'RGBA'
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.view_settings.exposure = 0
scene.view_settings.gamma = 1
camera_data = bpy.data.cameras.new('Orthographic plant view')
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 2.1
camera = bpy.data.objects.new('Orthographic plant view', camera_data)
bpy.context.collection.objects.link(camera)
scene.camera = camera
center = Vector((0, 0, 0.325))
views = []
for index in range(5):
    angle = math.radians(45 * index)
    normal = Vector((math.sin(angle), math.cos(angle), 0)) if index < 4 else Vector((0, 0, 1))
    camera.location = center + normal * 4
    camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
    bpy.context.view_layer.update()
    scene.render.filepath = str(BAKES / f'view_{index}.png')
    bpy.ops.render.render(write_still=True)
    image = bpy.data.images.load(scene.render.filepath, check_existing=False)
    pixels = np.array(image.pixels[:], dtype=np.float32).reshape(512, 512, 4)
    rows, columns = np.where(pixels[:, :, 3] > 0.01)
    assert len(rows) > 100, f'Blank plant bake at view {index}'
    # Blender pixel rows and exported UVs both start at the lower edge.
    left, right = max(0, int(columns.min()) - 1), min(512, int(columns.max()) + 2)
    bottom, top = max(0, int(rows.min()) - 1), min(512, int(rows.max()) + 2)
    views.append({'index': index, 'yaw_degrees': 45 * index,
                  'right': list(camera.matrix_world.col[0].xyz),
                  'pixel_bounds_bottom_up': [left, bottom, right, top],
                  'alpha_pixels': int(len(rows)), 'ortho_scale': 2.1})

# Remap the original near/medium atlas coordinates without touching geometry.
for name in ['grass_lush', 'grass_lush_medium']:
    if name != 'grass_lush':
        bpy.ops.object.select_all(action='DESELECT')
        bpy.ops.import_scene.gltf(filepath=str(MODELS / (name + '.glb')))
        selected = [obj for obj in bpy.context.selected_objects if obj.type == 'MESH']
        assert len(selected) == 1
        obj = selected[0]
    else:
        obj = plant
    for loop in obj.data.uv_layers.active.data:
        loop.uv.x *= 0.5
        loop.uv.y = 0.5 + loop.uv.y * 0.5
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(filepath=str(MODELS / (name + '.glb')), export_format='GLB',
                              use_selection=True, export_materials='NONE', export_animations=False)

vertices, faces, coordinates = [], [], []
for view in views:
    index = view['index']
    left, bottom, right, top = view['pixel_bounds_bottom_up']
    horizontal = Vector(view['right'])
    for px, py in [(left, bottom), (right, bottom), (right, top), (left, top)]:
        point = horizontal * ((px / 512 - 0.5) * 2.1)
        if index < 4:
            point.z = 0.325 + (py / 512 - 0.5) * 2.1
        else:
            point.y = (py / 512 - 0.5) * 2.1
            point.z = 0.025
        vertices.append(point)
        # Original photograph occupies the left half. The four baked views
        # occupy top-left/top-right/bottom-left/bottom-right of the right half.
        column, row_from_top = index % 2, index // 2
        if index < 4:
            u = (1024 + column * 512 + px) / 2048
            v = (1536 - row_from_top * 512 + py) / 2048
        else:
            u = px / 2048
            v = (512 + py) / 2048
        coordinates.append((u, v))
    start = len(vertices) - 4
    faces.append((start, start + 1, start + 2, start + 3))
mesh = bpy.data.meshes.new('Four original-plant coverage views')
mesh.from_pydata(vertices, [], faces)
mesh.update()
# Distant ground cover responds to the ground-facing light hemisphere.
mesh.normals_split_custom_set_from_vertices([(0, 0, 1)] * len(vertices))
uv = mesh.uv_layers.new(name='UV0')
for loop in mesh.loops:
    uv.data[loop.index].uv = coordinates[loop.vertex_index]
obj = bpy.data.objects.new('grass_lush_low', mesh)
bpy.context.collection.objects.link(obj)
bpy.ops.object.select_all(action='DESELECT')
obj.select_set(True)
bpy.context.view_layer.objects.active = obj
bpy.ops.export_scene.gltf(filepath=str(MODELS / 'grass_lush_low.glb'), export_format='GLB',
                          use_selection=True, export_materials='NONE', export_animations=False)
(PACKAGE / 'grass-impostor-bake.json').write_text(json.dumps({
    'format_version': 1, 'source': 'grass_lush', 'source_triangles': 6422,
    'low_triangles': 10, 'atlas_size': [2048, 2048], 'views': views,
    'color': 'unlit source photograph, Standard sRGB view, transparent RGBA',
}, indent=2) + '\n')

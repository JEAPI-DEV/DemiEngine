"""Render the real near mesh beside its authored coverage LOD, without a window."""
from pathlib import Path
import bpy
from mathutils import Vector

PACKAGE = Path(__file__).resolve().parents[1]
bpy.ops.wm.read_factory_settings(use_empty=True)
material = bpy.data.materials.new('Unlit atlas comparison')
material.use_nodes = True
nodes = material.node_tree.nodes
nodes.clear()
texture = nodes.new('ShaderNodeTexImage')
texture.image = bpy.data.images.load(str(PACKAGE / 'assets/understory/textures/grass_lush/albedo.png'))
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
for name, x in [('grass_lush', -1.1), ('grass_lush_low', 1.1)]:
    bpy.ops.object.select_all(action='DESELECT')
    bpy.ops.import_scene.gltf(filepath=str(PACKAGE / 'assets/understory/models' / (name + '.glb')))
    for obj in bpy.context.selected_objects:
        if obj.type == 'MESH':
            obj.location.x += x
            obj.data.materials.clear()
            obj.data.materials.append(material)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.device = 'CPU'
scene.cycles.samples = 32
scene.cycles.use_denoising = False
scene.cycles.transparent_max_bounces = 32
scene.cycles.max_bounces = 2
scene.render.resolution_x = 1280
scene.render.resolution_y = 640
scene.render.resolution_percentage = 100
scene.render.film_transparent = False
scene.render.image_settings.color_mode = 'RGBA'
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
world = bpy.data.worlds.new('Neutral comparison background')
world.use_nodes = True
world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.055, 0.065, 0.05, 1)
world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1
scene.world = world
camera_data = bpy.data.cameras.new('Comparison camera')
camera_data.type = 'ORTHO'
camera_data.ortho_scale = 4.3
camera = bpy.data.objects.new('Comparison camera', camera_data)
bpy.context.collection.objects.link(camera)
scene.camera = camera
target = Vector((0, 0, 0.3))
for name, location in [('side', (1, -7, 2.4)), ('aerial', (1, -4, 6))]:
    camera.location = location
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    scene.render.filepath = str(PACKAGE / 'previews' / ('grass-lod-' + name + '.png'))
    bpy.ops.render.render(write_still=True)

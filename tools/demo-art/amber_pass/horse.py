"""Rebuild Amber Pass's CC0 horse using isolated Blender: blender -b --python this.py."""
from pathlib import Path
import math
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'examples/amber_pass_3d/assets/horse'
bpy.ops.wm.open_mainfile(filepath=str(OUT / 'source/riggedHorse.blend'))
rig = bpy.data.objects['Armature']
meshes = [obj for obj in bpy.data.objects if obj.type == 'MESH']
for obj in list(bpy.data.objects):
    if obj.type not in {'ARMATURE', 'MESH'}:
        bpy.data.objects.remove(obj, do_unlink=True)

# The native skinned path samples one albedo for every primitive. Preserve the
# original coat at 2K and place opaque hair/eyes in separate atlas rectangles.
# This is color-only source data, with no illumination baked into the atlas.
import numpy as np
atlas_width, atlas_height = 3072, 2048
pixels = np.zeros((atlas_height, atlas_width, 4), dtype=np.float32)
pixels[:, :, :3] = (0.035, 0.025, 0.018)
pixels[:, :, 3] = 1.0
patches = {
    'body': ('HorseMain4k00.png', 0, 0, 2048, 2048),
    'hair': ('Hair12Main2k.png', 2048, 0, 1024, 1024),
    'eye': ('eye_texture.bmp.001', 2048, 1024, 512, 512),
}
for name, (image_name, x, y, width, height) in patches.items():
    source = bpy.data.images[image_name]
    source_pixels = np.empty(source.size[0] * source.size[1] * 4, dtype=np.float32)
    source.pixels.foreach_get(source_pixels)
    source_pixels = source_pixels.reshape(source.size[1], source.size[0], 4)
    rows = np.minimum((np.arange(height) * source.size[1] / height).astype(int), source.size[1] - 1)
    columns = np.minimum((np.arange(width) * source.size[0] / width).astype(int), source.size[0] - 1)
    patch = source_pixels[rows[:, None], columns[None, :]].copy()
    if name == 'hair':
        # Opaque dark backing avoids unsupported native alpha-card assumptions.
        patch[:, :, :3] = patch[:, :, :3] * patch[:, :, 3:4] + np.array([.018, .012, .008]) * (1 - patch[:, :, 3:4])
    patch[:, :, 3] = 1.0
    pixels[y:y + height, x:x + width] = patch
atlas = bpy.data.images.new('HorseAlbedoAtlas', width=atlas_width, height=atlas_height, alpha=False)
atlas.pixels.foreach_set(pixels.ravel())
atlas.filepath_raw = str(OUT / 'textures/horse_albedo.png')
atlas.file_format = 'PNG'
atlas.save()
atlas.pack()
mat = bpy.data.materials.new('Horse atlas')
mat.use_nodes = True
shader = mat.node_tree.nodes.get('Principled BSDF')
shader.inputs['Roughness'].default_value = .82
texture = mat.node_tree.nodes.new('ShaderNodeTexImage')
texture.image = atlas
mat.node_tree.links.new(texture.outputs['Color'], shader.inputs['Base Color'])
for obj in meshes:
    patch_name = 'body' if obj.name == 'Plane' else 'eye' if obj.name.startswith('Sphere') else 'hair'
    _, x, y, width, height = patches[patch_name]
    if obj.data.uv_layers.active is None:
        uv_layer = obj.data.uv_layers.new(name='AtlasUV')
        for uv in uv_layer.data:
            uv.uv = (0.5, 0.5)
    for loop in obj.data.uv_layers.active.data:
        loop.uv.x = (x + 1 + min(1, max(0, loop.uv.x)) * (width - 2)) / atlas_width
        loop.uv.y = (y + 1 + min(1, max(0, loop.uv.y)) * (height - 2)) / atlas_height
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for polygon in obj.data.polygons:
        polygon.use_smooth = True
    if obj.name != 'Plane':
        # Missing accessory weights in the original are repaired explicitly.
        bone = 'Bone.002' if obj.name.startswith('Sphere') else 'Bone.004' if obj.name == 'BezierCurve.005' else 'Bone.001'
        group = obj.vertex_groups.new(name=bone)
        group.add(list(range(len(obj.data.vertices))), 1.0, 'REPLACE')
        modifier = obj.modifiers.new('Horse skeleton', 'ARMATURE')
        modifier.object = rig

# Root transforms affect every part uniformly; Blender -Y exports to glTF +Z.
root = bpy.data.objects.new('Horse', None)
bpy.context.collection.objects.link(root)
for obj in meshes + [rig]:
    world = obj.matrix_world.copy()
    obj.parent = root
    obj.matrix_world = world
feet = min((obj.matrix_world @ vertex.co).z for obj in meshes for vertex in obj.data.vertices)
withers = (rig.matrix_world @ rig.data.bones['Bone'].head_local).z
scale = 1.6 / (withers - feet)
root.scale = (scale,) * 3
root.location.z = -feet * scale
for obj in meshes:
    world = obj.matrix_world.copy()
    obj.parent = rig
    obj.matrix_world = world
    obj.data.validate(verbose=False)
for bone in rig.pose.bones:
    for constraint in list(bone.constraints):
        bone.constraints.remove(constraint)
rig.animation_data_create()

# Four-beat walk and diagonal-pair running trot, with flexion during swing.
for clip, duration, stride in [('Idle', 3.0, 0.0), ('Walk', 1.2, 0.34), ('Run', 0.66, 0.62)]:
    action = bpy.data.actions.new(clip)
    rig.animation_data.action = action
    frames = round(duration * 30)
    for frame in range(frames + 1):
        cycle = 2 * math.pi * frame / frames
        for bone in rig.pose.bones:
            bone.rotation_mode = 'XYZ'
            bone.rotation_euler = (0, 0, 0)
            bone.location = (0, 0, 0)
        root_bone = rig.pose.bones['Bone']
        root_bone.location.z = (0.012 if clip == 'Idle' else 0.025 if clip == 'Walk' else 0.065) / scale * (1 - math.cos(cycle * (1 if clip == 'Idle' else 2))) / 2
        root_bone.rotation_euler.x = 0.012 * math.sin(cycle * 2)
        rig.pose.bones['Bone.001'].rotation_euler.x = 0.025 * math.sin(cycle + 0.5)
        rig.pose.bones['Bone.004'].rotation_euler.z = 0.08 * math.sin(cycle)
        phases = [0, math.pi, math.pi * 1.5, math.pi * .5] if clip == 'Walk' else [0, math.pi, math.pi, 0]
        for (upper, middle, lower), phase in zip([('Bone_L','Bone_L.001','Bone_L.002'),('Bone_R','Bone_R.001','Bone_R.002'),('Bone_L.003','Bone_L.004','Bone_L.005'),('Bone_R.003','Bone_R.004','Bone_R.005')], phases):
            angle = cycle + phase
            swing = max(0, math.sin(angle))
            rig.pose.bones[upper].rotation_euler.x = stride * math.cos(angle)
            rig.pose.bones[middle].rotation_euler.x = stride * 1.1 * swing
            rig.pose.bones[lower].rotation_euler.x = -stride * .65 * swing
        for bone in rig.pose.bones:
            bone.keyframe_insert('rotation_euler', frame=frame)
            bone.keyframe_insert('location', frame=frame)
    track = rig.animation_data.nla_tracks.new()
    track.name = clip
    track.strips.new(clip, 0, action)
    track.mute = True
rig.animation_data.action = None
bpy.context.scene.render.fps = 30
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'horse.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT / 'horse.glb'), export_format='GLB', export_animations=True, export_animation_mode='ACTIONS', export_frame_range=False, export_force_sampling=True, export_yup=True)
print('HORSE_SCALE', scale, 'SEAT_GLTF', [0, 1.67, 0.42])

# Preview is deliberately outside the source tree.
rig.animation_data.action = bpy.data.actions['Idle']
bpy.context.scene.frame_set(0)
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 16
scene.world.color = (0.35, 0.35, 0.35)
bpy.ops.object.camera_add(location=(4.2, -5.5, 3.0))
camera = bpy.context.object
camera.rotation_euler = (Vector((0, 0, 1.1)) - camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 4.3
scene.camera = camera
bpy.ops.object.light_add(type='AREA', location=(2, -3, 5))
bpy.context.object.data.energy = 600
bpy.context.object.data.shape = 'DISK'
bpy.context.object.data.size = 4
scene.render.resolution_x = 800
scene.render.resolution_y = 700
scene.render.resolution_percentage = 100
scene.render.filepath = '/tmp/demi-riding-assets/horse-preview.png'
bpy.ops.render.render(write_still=True)

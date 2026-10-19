"""Original Amber Pass chapel gate, generated in isolated background Blender.

Run: blender -b --python tools/demo-art/amber_pass/ruins.py
Blender -Y exports as glTF +Z. Origin is ground level at the gate passage.
The 4 m clear arch, asymmetric towers and breached chapel walls are static art;
scene-authored colliders own gameplay collision rather than this visual mesh.
"""
from pathlib import Path
import math
import random
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'examples/amber_pass_3d/assets/ruins'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
random.seed(1816)

stones = []
for index in range(8):
    value = .42 + index * .023
    mat = bpy.data.materials.new('Limestone weathering ' + str(index))
    mat.diffuse_color = (value * 1.04, value * 1.015, value * .93, 1)
    mat.use_nodes = True
    shader = mat.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = mat.diffuse_color
    shader.inputs['Roughness'].default_value = .86
    stones.append(mat)


def finish(obj, name):
    obj.name = name
    obj.data.materials.append(random.choice(stones))
    # Flat cut faces retain masonry weight; the narrow bevel catches edge light.
    bevel = obj.modifiers.new('Worn arrises', 'BEVEL')
    bevel.width = random.uniform(.035, .075)
    bevel.segments = 1
    obj.modifiers.new('Stone face normals', 'WEIGHTED_NORMAL')
    return obj


def block(name, position, dimensions, rotation=0):
    bpy.ops.mesh.primitive_cube_add(size=1, location=position)
    obj = bpy.context.object
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    obj.rotation_euler.z = rotation
    return finish(obj, name)


def wedge(name, center_x, center_z, radius, thickness, depth, start, end, y=0):
    vertices = []
    for side_y in [y-depth*.5, y+depth*.5]:
        for ring in [radius, radius+thickness]:
            for angle in [start, end]:
                vertices.append((center_x+math.cos(angle)*ring, side_y,
                                 center_z+math.sin(angle)*ring))
    faces = [(0,1,3,2), (4,6,7,5), (0,4,5,1),
             (2,3,7,6), (0,2,6,4), (1,5,7,3)]
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.collection.objects.link(obj)
    return finish(obj, name)


# Load-bearing gate piers. Course offsets and small gaps reveal individual stones.
course_height = .82
for side in [-1, 1]:
    for row in range(6):
        for depth_index in range(2):
            block('Gate pier ashlar', (side*2.64, (depth_index-.5)*1.15, (row+.5)*course_height),
                  (1.22-random.uniform(0,.05), 1.10, .78))
    block('Gate spring capital', (side*2.58, 0, 4.98), (1.48, 2.52, .30))

# True wedge stones form a clear 4 m opening with a 7 m inner apex.
for index in range(17):
    start = math.pi*index/17 + .007
    end = math.pi*(index+1)/17 - .007
    wedge('Central arch voussoir', 0, 5.0, 2.0, .72, 2.35, start, end)
    wedge('Carved outer archivolt', 0, 5.0, 2.75, .18, 2.52, start, end)
# The keystone projects forward without closing the opening.
wedge('Projecting arch keystone', 0, 5.0, 2.0, .95, 2.70, math.pi*.473, math.pi*.527)


def tower(center_x, height, name):
    """Hollow squared tower with arrow windows and a jagged breached crown."""
    courses = round(height/course_height)
    for row in range(courses):
        z = (row+.5)*course_height
        for face in range(4):
            for index in range(3):
                along = (index-1)*1.27
                # Windows are real missing masonry, visible from both sides.
                window = index == 1 and 4.8 < z < 7.8 and face in [0,2]
                high_window = index == 1 and 10.3 < z < 12.7 and face in [0,1]
                crown_loss = row >= courses-3 and (face+index)%4 in [0,1]
                breach = face == 3 and index == 1 and row >= courses-6
                if window or high_window or crown_loss or breach:
                    continue
                jitter = random.uniform(-.035, .035)
                if face in [0,2]:
                    position = (center_x+along, (-1 if face == 0 else 1)*1.74, z)
                    dimensions = (1.22, .72, .78)
                else:
                    position = (center_x+(-1 if face == 1 else 1)*1.74, along, z)
                    dimensions = (.72, 1.22, .78)
                block(name+' wall course', position, dimensions, jitter*.12)
        if row in [4,10,courses-4]:
            # A discontinuous projecting belt breaks the otherwise straight shaft.
            for face_y in [-1.78,1.78]:
                block(name+' stringcourse', (center_x,face_y,z+.35), (4.05,.83,.18))
    for side in [-1,1]:
        for row in range(4):
            block(name+' splayed base', (center_x+side*1.82,0,(row+.5)*.72),
                  (1.10-row*.10,4.0-row*.12,.68))
    # Window lintels retain the carved ecclesiastical silhouette.
    for y in [-1.78,1.78]:
        for index in range(7):
            wedge(name+' window hood',center_x,7.75,.49,.23,.79,
                  math.pi*index/7+.01,math.pi*(index+1)/7-.01,y)


tower(-5.1,12.3,'West broken bell tower')
tower(5.1,17.2,'East broken watch tower')

# Broken chapel side walls extend behind the gate, leaving a walkable nave.
for side in [-1,1]:
    for section in range(5):
        y = 2.5+section*1.28
        height = [5,4,2,3,1][section] + (1 if side == 1 else 0)
        for row in range(height):
            block('Breached chapel side wall', (side*7.2,y,(row+.5)*.82),
                  (.78,1.22,.78))
    for row in range(4):
        block('Outer chapel buttress', (side*(7.45+.14*(3-row)),3.0,(row+.5)*.80),
              (1.03,1.45-row*.14,.75))
    # A surviving flying fragment frames the empty sky above the roofless nave.
    for index in range(6):
        wedge('Broken transept arch',side*5.1,3.4,1.6,.42,.72,
              math.pi*index/10+.02,math.pi*(index+1)/10-.02,5.2)

# Loose blocks are part of the authored silhouette, never across the clear lane.
for index in range(35):
    side = -1 if index%2 else 1
    x = side*random.uniform(3.8,8.5)
    y = random.uniform(-4.6,8.0)
    width = random.uniform(.4,1.0)
    block('Fallen masonry', (x,y,.20), (width,.5,.4), random.uniform(-.8,.8))

# Bake modifiers and compose one model without lights or camera export.
bpy.ops.object.select_all(action='DESELECT')
for obj in list(bpy.context.scene.objects):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    for modifier in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    obj.select_set(False)
bpy.ops.object.select_all(action='SELECT')
bpy.context.view_layer.objects.active = bpy.context.selected_objects[0]
bpy.ops.object.join()
ruin = bpy.context.object
ruin.name = 'Amber Pass broken chapel gate'
bpy.context.scene.cursor.location = (0,0,0)
bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT/'editable.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT/'ruin.glb'),export_format='GLB',
                         use_selection=True,export_materials='EXPORT')
triangles = sum(len(polygon.vertices)-2 for polygon in ruin.data.polygons)
print(f'AMBER_RUIN triangles={triangles} bounds={tuple(ruin.dimensions)}')

# Temporary inspection view shows the gate opening and broken upper silhouette.
bpy.ops.object.camera_add(location=(25,-35,23))
camera = bpy.context.object
camera.rotation_euler = (Vector((0,1,6))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 29
bpy.context.scene.camera = camera
for location,energy,size in [((0,-12,25),3800,12),((-15,4,15),2700,10),((15,10,22),4500,10)]:
    bpy.ops.object.light_add(type='AREA',location=location)
    light = bpy.context.object
    light.data.energy = energy
    light.data.size = size
    light.rotation_euler = (Vector((0,0,6))-light.location).to_track_quat('-Z','Y').to_euler()
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 24
scene.world = bpy.data.worlds.new('Inspection studio')
scene.world.color = (.18,.18,.18)
scene.render.resolution_x = 1400
scene.render.resolution_y = 1200
scene.render.resolution_percentage = 100
scene.render.filepath = '/tmp/amber_pass_ruin_preview.png'
bpy.ops.render.render(write_still=True)

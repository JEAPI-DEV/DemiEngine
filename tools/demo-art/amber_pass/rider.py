"""Original Amber Pass mounted warden; rebuild with blender -b --python rider.py.

Blender -Y becomes glTF +Z. The local origin is the seat, not the ground.
All colors are unlit material factors; there are no external texture dependencies.
"""
from pathlib import Path
import math
import random
import bpy
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'examples/amber_pass_3d/assets/rider'
OUT.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.read_factory_settings(use_empty=True)
random.seed(618)


def material(name, color, metallic=0, roughness=.65):
    result = bpy.data.materials.new(name)
    result.diffuse_color = (*color, 1)
    result.use_nodes = True
    shader = result.node_tree.nodes.get('Principled BSDF')
    shader.inputs['Base Color'].default_value = (*color, 1)
    shader.inputs['Metallic'].default_value = metallic
    shader.inputs['Roughness'].default_value = roughness
    return result


steel = material('Blue grey weathered steel', (.27, .33, .35), .8, .45)
edge = material('Worn steel edges', (.48, .50, .46), .75, .36)
bronze = material('Old bronze fittings', (.38, .23, .075), .7, .55)
leather = material('Oiled umber leather', (.115, .057, .028))
leather_light = material('Worn saddle leather', (.24, .115, .047))
cloth = material('Deep teal wool', (.025, .145, .145), 0, .9)
cloth_light = material('Teal fold highlights', (.035, .19, .18), 0, .9)
shadow = material('Visor darkness', (.018, .023, .023))
skin = material('Weathered skin', (.39, .24, .16))
wood = material('Shield oak', (.19, .105, .043))
linen = material('Saddle blanket', (.31, .27, .17))


def finish(obj, name, mat):
    obj.name = name
    obj.data.materials.append(mat)
    if obj.type == 'MESH':
        for polygon in obj.data.polygons:
            polygon.use_smooth = True
    return obj


def ellipsoid(name, center, scale, mat, segments=16, rings=8):
    bpy.ops.mesh.primitive_uv_sphere_add(segments=segments, ring_count=rings, location=center)
    obj = bpy.context.object
    obj.scale = scale
    return finish(obj, name, mat)


def limb(name, start, end, radius, depth, mat):
    midpoint = (Vector(start) + Vector(end)) * .5
    direction = Vector(end) - Vector(start)
    obj = ellipsoid(name, midpoint, (radius, depth, direction.length * .58), mat)
    obj.rotation_mode = 'QUATERNION'
    obj.rotation_quaternion = Vector((0, 0, 1)).rotation_difference(direction)
    return obj


def strap(name, points, width, mat):
    curve = bpy.data.curves.new(name, 'CURVE')
    curve.dimensions = '3D'
    curve.resolution_u = 12
    curve.bevel_depth = width
    curve.bevel_resolution = 2
    spline = curve.splines.new('BEZIER')
    spline.bezier_points.add(len(points) - 1)
    for point, coordinate in zip(spline.bezier_points, points):
        point.co = coordinate
        point.handle_left_type = 'AUTO'
        point.handle_right_type = 'AUTO'
    obj = bpy.data.objects.new(name, curve)
    bpy.context.collection.objects.link(obj)
    return finish(obj, name, mat)


def rounded_box(name, center, dimensions, mat, bevel=.025):
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
    modifier = obj.modifiers.new('Soft worked edges', 'BEVEL')
    modifier.width = bevel
    modifier.segments = 3
    obj.modifiers.new('Weighted normals', 'WEIGHTED_NORMAL')
    return finish(obj, name, mat)


# Saddle sits around the seat with a raised cantle and a narrower front pommel.
ellipsoid('Wool saddle pad', (0, .03, -.10), (.44, .49, .105), linen)
ellipsoid('Leather saddle seat', (0, .02, -.035), (.32, .35, .075), leather)
ellipsoid('Raised cantle', (0, .31, .015), (.34, .09, .13), leather_light)
ellipsoid('Front pommel', (0, -.30, .025), (.30, .075, .12), leather_light)
for side in [-1, 1]:
    x = side * .40
    ellipsoid('Saddle skirt', (x, .04, -.19), (.075, .35, .27), leather_light)
    strap('Girth strap', [(side*.37, -.03, -.13), (side*.46, -.01, -.44),
                         (side*.26, .0, -.66)], .025, leather)
    rounded_box('Travel satchel', (side*.46, .32, -.20), (.23, .27, .32), leather_light, .06)
    rounded_box('Satchel flap', (side*.46, .30, -.045), (.25, .29, .055), leather, .02)
    strap('Satchel belt', [(side*.46, .15, -.03), (side*.46, .15, -.30)], .015, leather)
    rounded_box('Satchel buckle', (side*.46, .14, -.16), (.055, .018, .065), bronze, .006)

# Seated pelvis and gently leaning torso. Limbs use rounded anatomical volumes.
ellipsoid('Seated hips', (0, .005, .12), (.25, .19, .20), leather)
ellipsoid('Quilted arming coat', (0, -.015, .41), (.265, .17, .32), cloth)
ellipsoid('Forged breastplate', (0, -.085, .44), (.25, .135, .27), steel)
ellipsoid('Steel backplate', (0, .07, .46), (.24, .12, .24), steel)
for z in [.22, .27]:
    strap('Waist armor lame', [(-.23, -.095, z), (0, -.16, z-.02), (.23, -.095, z)], .022, edge)
strap('Cross chest belt', [(-.19, -.145, .65), (0, -.218, .45), (.20, -.13, .22)], .025, leather)
rounded_box('Chest belt buckle', (.025, -.22, .43), (.075, .025, .065), bronze, .008)

for side in [-1, 1]:
    hip = (side*.17, -.015, .08)
    knee = (side*.43, -.27, -.30)
    ankle = (side*.43, -.09, -.79)
    limb('Riding breeches thigh', hip, knee, .135, .135, leather)
    ellipsoid('Steel knee cop', knee, (.13, .13, .13), steel)
    limb('Tall leather boot', knee, ankle, .105, .095, leather)
    limb('Boot shin guard', (side*.43, -.33, -.34), (side*.43, -.19, -.68), .085, .035, steel)
    ellipsoid('Boot toe', (side*.43, -.21, -.80), (.105, .20, .09), leather)
    for z in [-.46, -.64]:
        strap('Boot strap', [(side*.35, -.16, z), (side*.43, -.22, z), (side*.51, -.16, z)], .015, leather_light)
    strap('Stirrup leather', [(side*.36, -.23, -.08), (side*.53, -.18, -.45), (side*.53, -.15, -.80)], .017, leather)
    strap('Iron stirrup', [(side*.53, -.15, -.65), (side*.61, -.15, -.84),
                           (side*.40, -.15, -.86), (side*.40, -.15, -.70)], .014, edge)
    shoulder = (side*.25, -.005, .65)
    elbow = (side*.37, -.22, .42)
    hand = (side*.20, -.49, .43)
    limb('Upper sleeve', shoulder, elbow, .10, .10, cloth)
    ellipsoid('Layered shoulder pauldron', shoulder, (.17, .16, .12), steel)
    ellipsoid('Pauldron lower plate', (side*.29, -.015, .59), (.145, .14, .08), edge)
    for offset in [-.09, 0, .09]:
        ellipsoid('Pauldron bronze rivet', (side*.31, offset-.035, .712),
                  (.012, .012, .008), bronze, 8, 4)
    strap('Pauldron engraved ridge', [(side*.17, -.10, .69),
          (side*.27, -.14, .70), (side*.36, -.10, .67)], .007, bronze)
    limb('Leather forearm', elbow, hand, .077, .07, leather)
    limb('Bracer', (side*.34, -.26, .42), (side*.24, -.43, .43), .083, .075, steel)
    ellipsoid('Rein glove', hand, (.068, .09, .062), leather)
    strap('Rein', [hand, (side*.22, -.75, .18), (side*.18, -1.17, -.12)], .009, leather)

ellipsoid('Neck collar', (0, -.025, .75), (.11, .11, .09), leather)
ellipsoid('Lower face', (0, -.08, .88), (.105, .105, .14), skin)
ellipsoid('Helmet dome', (0, -.015, .99), (.155, .145, .18), steel, 32, 16)
ellipsoid('Helmet brow rim', (0, -.025, .985), (.166, .151, .045), edge)
rounded_box('Visor slit', (0, -.158, .963), (.20, .016, .023), shadow, .009)
rounded_box('Nasal guard', (0, -.171, .92), (.029, .025, .14), edge, .009)
for side in [-1, 1]:
    cheek = ellipsoid('Helmet cheek plate', (side*.105, -.095, .89), (.055, .075, .105), steel)
    cheek.rotation_euler.y = side * -.22
    for z in [.87, .90, .93]:
        ellipsoid('Helmet breathing hole', (side*.132, -.144, z), (.008, .006, .008), shadow, 8, 4)
strap('Helmet crest seam', [(0, -.15, 1.01), (0, -.07, 1.16), (0, .07, 1.14), (0, .13, 1.01)], .012, bronze)

# A hand-shaped pleated cloak: width opens toward its asymmetric ragged hem.
vertices = []
faces = []
columns, rows = 25, 13
for row in range(rows):
    t = row / (rows - 1)
    width = .23 + .16*t
    for column in range(columns):
        u = column/(columns-1)
        x = (u*2-1)*width
        y = .13 + .32*t + .045*math.cos(u*math.pi*8)*(t+.2)
        z = .71 - 1.20*t + (.045*math.sin(u*31) if row == rows-1 else 0)
        vertices.append((x, y, z))
for row in range(rows-1):
    for column in range(columns-1):
        index = row*columns+column
        faces.append((index, index+1, index+columns+1, index+columns))
mesh = bpy.data.meshes.new('Pleated wool cloak')
mesh.from_pydata(vertices, [], faces)
cloak = bpy.data.objects.new('Ragged teal traveling cloak', mesh)
bpy.context.collection.objects.link(cloak)
finish(cloak, cloak.name, cloth)
cloak.data.materials.append(cloth_light)
for polygon in cloak.data.polygons:
    polygon.material_index = 1 if polygon.index % (columns-1) in [5, 11, 17] else 0
solid = cloak.modifiers.new('Heavy wool thickness', 'SOLIDIFY')
solid.thickness = .012

# Round shield hangs over the cloak. The bronze boss and rim face backward.
center = Vector((-.035, .32, .43))
def shield_disc(name, radius, depth, offset, mat, vertices=48):
    bpy.ops.mesh.primitive_cylinder_add(vertices=vertices, radius=radius, depth=depth,
                                      location=center+Vector((0, offset, 0)), rotation=(math.pi/2, 0, 0))
    return finish(bpy.context.object, name, mat)
shield_disc('Oak round shield', .325, .045, 0, wood)
for index in range(-2, 3):
    x = index*.105
    half = math.sqrt(.30*.30-x*x)
    strap('Shield plank seam', [(center.x+x, center.y+.027, center.z-half),
                               (center.x+x, center.y+.027, center.z+half)], .004, leather)
for radius, thickness in [(.325, .018), (.283, .007)]:
    bpy.ops.mesh.primitive_torus_add(major_radius=radius, minor_radius=thickness,
                                  major_segments=48, minor_segments=8,
                                  location=center+Vector((0,.03,0)), rotation=(math.pi/2,0,0))
    finish(bpy.context.object, 'Bronze shield binding', bronze)
for angle in [0, math.pi / 2, math.pi, math.pi * 1.5]:
    radial_center = center + Vector((math.sin(angle) * .18, .044, math.cos(angle) * .18))
    band = rounded_box('Shield radial bronze band', radial_center, (.035, .016, .23), bronze, .006)
    band.rotation_euler.y = angle
    for radius in [.14, .25]:
        rivet_position = center + Vector((math.sin(angle) * radius, .06, math.cos(angle) * radius))
        ellipsoid('Shield band rivet', rivet_position, (.009, .009, .009), edge, 8, 4)
ellipsoid('Shield central boss', center+Vector((0,.055,0)), (.105,.065,.105), bronze)
for index in range(12):
    angle = index*math.tau/12
    ellipsoid('Shield rim rivet', center+Vector((math.cos(angle)*.302,.052,math.sin(angle)*.302)),
              (.011,.009,.011), edge, 8, 4)

# Convert curves and modifiers once, then export physical material families.
# Every family has exactly the same seat-local origin for native composition.
bpy.ops.object.select_all(action='DESELECT')
groups = {'armor': [], 'cloth': [], 'leather': [], 'shield': []}
for obj in list(bpy.context.scene.objects):
    bpy.context.view_layer.objects.active = obj
    obj.select_set(True)
    if obj.type == 'CURVE':
        bpy.ops.object.convert(target='MESH')
    for modifier in list(obj.modifiers):
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    mat = obj.data.materials[0]
    if 'shield' in obj.name.lower():
        group = 'shield'
    elif mat in [cloth, cloth_light, linen]:
        group = 'cloth'
    elif mat in [leather, leather_light, skin, shadow]:
        group = 'leather'
    else:
        group = 'armor'
    groups[group].append(obj)
    obj.select_set(False)

bpy.context.scene.cursor.location = (0, 0, 0)
family_meshes = []
for group, objects in groups.items():
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.object.join()
    mesh = bpy.context.object
    mesh.name = 'Rider ' + group
    bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.ops.export_scene.gltf(filepath=str(OUT / ('rider_' + group + '.glb')),
                             export_format='GLB', use_selection=True, export_materials='EXPORT')
    family_meshes.append(mesh)

bpy.ops.object.select_all(action='DESELECT')
for mesh in family_meshes:
    mesh.select_set(True)
bpy.context.view_layer.objects.active = family_meshes[0]
bpy.ops.object.join()
rider = bpy.context.object
rider.name = 'Amber Pass mounted warden and tack'
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(OUT / 'editable.blend'))
bpy.ops.export_scene.gltf(filepath=str(OUT / 'rider.glb'), export_format='GLB',
                         use_selection=True, export_materials='EXPORT')
triangles = sum(len(polygon.vertices)-2 for polygon in rider.data.polygons)
print(f'AMBER_RIDER triangles={triangles} bounds=' + str(tuple(rider.dimensions)))

# The preview is deliberately temporary; it is not a shipped texture or asset.
bpy.ops.object.camera_add(location=(3,-4,2.4))
camera = bpy.context.object
camera.rotation_euler = (Vector((0,0,.15))-camera.location).to_track_quat('-Z','Y').to_euler()
camera.data.type = 'ORTHO'
camera.data.ortho_scale = 2.8
bpy.context.scene.camera = camera
for location, energy, size in [((2,-3,5),900,4),((-3,-1,2),600,3),((1,4,3),1100,3)]:
    bpy.ops.object.light_add(type='AREA', location=location)
    light = bpy.context.object
    light.data.energy = energy
    light.data.shape = 'DISK'
    light.data.size = size
    light.rotation_euler=(Vector((0,0,.2))-light.location).to_track_quat('-Z','Y').to_euler()
scene = bpy.context.scene
scene.render.engine = 'CYCLES'
scene.cycles.samples = 24
scene.world=bpy.data.worlds.new('Studio')
scene.world.color=(.13,.13,.13)
scene.render.resolution_x = 1000
scene.render.resolution_y = 1000
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.filepath = '/tmp/amber_pass_rider_preview.png'
bpy.ops.render.render(write_still=True)

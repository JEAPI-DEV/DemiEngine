"""Build a reusable grass patch and bake its distant cards in Blender.

Run with Blender's Python after importing Poly Haven grass_medium_01 into
Demi_Alpine_GrassReview. All source objects and the user's active scene remain
unchanged. Downloads and CC0 provenance are recorded beside imported assets.
"""
import bpy
import json
import math
import random
from pathlib import Path
from mathutils import Vector

output = Path(globals().get('output_directory', '/tmp/demi-alpine-source/grass_cluster'))
output.mkdir(parents=True, exist_ok=True)
source_scene = bpy.data.scenes['Demi_Alpine_GrassReview']
previous_scene = bpy.context.window.scene
scene = bpy.data.scenes.new('Demi_Alpine_GrassBake')
bpy.context.window.scene = scene
try:
    randomizer = random.Random(74201)
    variants = [o for o in source_scene.objects if '_tall_' in o.name]
    plants = []
    for index in range(25):
        original = variants[index % len(variants)]
        plant = original.copy()
        plant.data = original.data.copy()
        scene.collection.objects.link(plant)
        minimum = Vector((min(v.co.x for v in plant.data.vertices),
                          min(v.co.y for v in plant.data.vertices),
                          min(v.co.z for v in plant.data.vertices)))
        maximum = Vector((max(v.co.x for v in plant.data.vertices),
                          max(v.co.y for v in plant.data.vertices),
                          max(v.co.z for v in plant.data.vertices)))
        center = Vector(((minimum.x + maximum.x) / 2,
                         (minimum.y + maximum.y) / 2, minimum.z))
        for vertex in plant.data.vertices:
            vertex.co -= center
        angle = randomizer.uniform(0, math.tau)
        radius = math.sqrt(randomizer.random()) * .65
        plant.location = (math.cos(angle) * radius, math.sin(angle) * radius, 0)
        plant.rotation_euler = (0, 0, randomizer.uniform(0, math.tau))
        scale = randomizer.uniform(1.25, 2.05)
        plant.scale = (scale, scale, scale)
        plant.select_set(True)
        plants.append(plant)
    bpy.context.view_layer.objects.active = plants[0]
    bpy.ops.object.join()
    high = bpy.context.object
    high.name = 'alpine_grass_patch'
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    bpy.context.view_layer.update()
    bpy.ops.export_scene.gltf(filepath=str(output / 'grass_patch.gltf'),
                             export_format='GLTF_SEPARATE', use_selection=True, use_active_scene=True)
    high_path = output / 'grass_patch.gltf'
    high_document = json.loads(high_path.read_text())
    for entry in high_document['materials']:
        entry.update(alphaMode='MASK', alphaCutoff=.35, doubleSided=True)
    high_path.write_text(json.dumps(high_document, indent=2) + '\n')
    high_triangles = sum(len(p.vertices) - 2 for p in high.data.polygons)

    # Bake the actual scanned foliage silhouette without baked directional
    # lighting; the runtime supplies its own light and leaves grass shadows off.
    original_materials = list(high.data.materials)
    bake_materials = []
    for original in original_materials:
        emission = original.copy()
        nodes, links = emission.node_tree.nodes, emission.node_tree.links
        surface = next(node for node in nodes if node.type == 'BSDF_PRINCIPLED')
        destination = next(node for node in nodes if node.type == 'OUTPUT_MATERIAL')
        shader = nodes.new('ShaderNodeEmission')
        color = surface.inputs['Base Color']
        if color.is_linked:
            links.new(color.links[0].from_socket, shader.inputs['Color'])
        else:
            shader.inputs['Color'].default_value = color.default_value
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        mix = nodes.new('ShaderNodeMixShader')
        alpha = surface.inputs['Alpha']
        if alpha.is_linked:
            links.new(alpha.links[0].from_socket, mix.inputs[0])
        else:
            mix.inputs[0].default_value = alpha.default_value
        links.new(transparent.outputs[0], mix.inputs[1])
        links.new(shader.outputs[0], mix.inputs[2])
        links.new(mix.outputs[0], destination.inputs['Surface'])
        bake_materials.append(emission)
    for slot, material in zip(high.material_slots, bake_materials):
        slot.material = material
    camera_data = bpy.data.cameras.new('Demi grass bake camera')
    camera = bpy.data.objects.new('Demi grass bake camera', camera_data)
    scene.collection.objects.link(camera)
    camera.location = (0, -4, .36)
    target = Vector((0, 0, .36))
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = 1.8
    scene.camera = camera
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = 512
    scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    scene.render.filepath = str(output / 'grass_card.png')
    bpy.ops.render.render(write_still=True, scene=scene.name)
    for slot, material in zip(high.material_slots, original_materials):
        slot.material = material
    high.hide_render = True
    high.select_set(False)

    # Three crossed cards preserve a patch silhouette at oblique views. UVs
    # include the transparent margin from the orthographic bake exactly.
    vertices, faces = [], []
    for angle in (0, math.pi / 3, 2 * math.pi / 3):
        right = Vector((math.cos(angle), math.sin(angle), 0)) * .9
        first = len(vertices)
        vertices.extend([tuple(-right + Vector((0, 0, -.54))),
                         tuple(right + Vector((0, 0, -.54))),
                         tuple(right + Vector((0, 0, 1.26))),
                         tuple(-right + Vector((0, 0, 1.26)))])
        faces.append((first, first + 1, first + 2, first + 3))
    mesh = bpy.data.meshes.new('Alpine grass distant cards')
    mesh.from_pydata(vertices, [], faces)
    # Upward aggregate normals keep distant grass lit like its ground cover.
    # These are vegetation impostors, not upright solid walls.
    mesh.normals_split_custom_set([(0, 0, 1)] * len(mesh.loops))
    uv = mesh.uv_layers.new()
    for polygon in mesh.polygons:
        for loop, coordinate in zip(polygon.loop_indices, ((0, 0), (1, 0), (1, 1), (0, 1))):
            uv.data[loop].uv = coordinate
    card = bpy.data.objects.new('alpine_grass_distant', mesh)
    scene.collection.objects.link(card)
    material = bpy.data.materials.new('Alpine grass card')
    material.use_nodes = True
    principled = material.node_tree.nodes.get('Principled BSDF')
    principled.inputs['Roughness'].default_value = 1
    texture = material.node_tree.nodes.new('ShaderNodeTexImage')
    texture.image = bpy.data.images.load(str(output / 'grass_card.png'))
    material.node_tree.links.new(texture.outputs['Color'], principled.inputs['Base Color'])
    material.node_tree.links.new(texture.outputs['Alpha'], principled.inputs['Alpha'])
    mesh.materials.append(material)
    card.select_set(True)
    bpy.context.view_layer.objects.active = card
    bpy.ops.export_scene.gltf(filepath=str(output / 'grass_distant.gltf'),
                             export_format='GLTF_SEPARATE', use_selection=True, use_active_scene=True)
    path = output / 'grass_distant.gltf'
    document = json.loads(path.read_text())
    for entry in document['materials']:
        entry.update(alphaMode='MASK', alphaCutoff=.35, doubleSided=True)
    path.write_text(json.dumps(document, indent=2) + '\n')
    result = {'output': str(output), 'near_triangles': high_triangles,
              'distant_triangles': 6, 'scene': scene.name}
finally:
    bpy.context.window.scene = previous_scene

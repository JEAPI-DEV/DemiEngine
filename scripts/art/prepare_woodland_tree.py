"""Build near/mid meshes and a crossed-card distant LOD from woodland source art.

Run in a separate Blender background process. Source package files are read-only.
The output is authored glTF source for the normal engine asset importer.
"""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector


def arguments():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    return parser.parse_args(sys.argv[sys.argv.index('--') + 1:])


def material_for(package, name, registry):
    definition = json.loads((package / 'assets/materials' / (name + '.material.json')).read_text())
    material = bpy.data.materials.new(name)
    material.use_nodes = True
    material.use_backface_culling = False
    principled = material.node_tree.nodes.get('Principled BSDF')
    principled.inputs['Roughness'].default_value = definition['parameters']['roughness']
    for slot, reference in definition['textures'].items():
        image = bpy.data.images.load(str(registry[reference]), check_existing=True)
        texture = material.node_tree.nodes.new('ShaderNodeTexImage')
        texture.image = image
        if slot == 'albedo':
            material.node_tree.links.new(texture.outputs['Color'], principled.inputs['Base Color'])
            if definition['render_state'].get('alpha_cutoff'):
                material.node_tree.links.new(texture.outputs['Alpha'], principled.inputs['Alpha'])
        elif slot == 'normal':
            image.colorspace_settings.name = 'Non-Color'
            normal = material.node_tree.nodes.new('ShaderNodeNormalMap')
            normal.inputs['Strength'].default_value = definition['parameters'].get('normal_strength', 1)
            material.node_tree.links.new(texture.outputs['Color'], normal.inputs['Color'])
            material.node_tree.links.new(normal.outputs['Normal'], principled.inputs['Normal'])
        elif slot == 'roughness':
            image.colorspace_settings.name = 'Non-Color'
            material.node_tree.links.new(texture.outputs['Color'], principled.inputs['Roughness'])
    return material


def export(objects, path):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLTF_SEPARATE',
                             use_selection=True, export_normals=True)
    document = json.loads(path.read_text())
    for material in document.get('materials', []):
        material['doubleSided'] = True
        if material['name'] != 'trunk':
            material['alphaMode'] = 'MASK'
            material['alphaCutoff'] = 0.5
    path.write_text(json.dumps(document, indent=2) + '\n')


def main():
    args = arguments()
    package, output = args.package.resolve(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    registry = {}
    for manifest in package.rglob('*.asset.json'):
        data = json.loads(manifest.read_text())
        registry[data['id']] = manifest.parent / data['source']
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    materials = {name: material_for(package, name, registry)
                 for name in ('trunk', 'branches', 'leaves')}
    versions = {}
    for version, suffix in [('near', ''), ('mid', '_mid')]:
        objects = []
        for part in materials:
            before = set(bpy.data.objects)
            bpy.ops.import_scene.gltf(filepath=str(package / 'assets/environment' /
                                                  ('tree_' + part + suffix + '.glb')))
            meshes = [obj for obj in bpy.data.objects if obj not in before and obj.type == 'MESH']
            for obj in meshes:
                obj.data.materials.clear()
                obj.data.materials.append(materials[part])
                obj.hide_render = version == 'mid'
            objects.extend(meshes)
        export(objects, output / ('tree_' + version + '.gltf'))
        versions[version] = objects
    near = versions['near']
    points = [obj.matrix_world @ Vector(corner) for obj in near for corner in obj.bound_box]
    low = Vector(tuple(min(p[i] for p in points) for i in range(3)))
    high = Vector(tuple(max(p[i] for p in points) for i in range(3)))
    center = (low + high) / 2
    edge = max(high - low) * 1.05
    # Unlit albedo with preserved alpha. Runtime light/shadows are applied once.
    for material in materials.values():
        nodes, links = material.node_tree.nodes, material.node_tree.links
        principled = nodes.get('Principled BSDF')
        color = principled.inputs['Base Color'].links[0].from_socket
        emission = nodes.new('ShaderNodeEmission')
        links.new(color, emission.inputs['Color'])
        result = emission.outputs[0]
        if principled.inputs['Alpha'].is_linked:
            transparent = nodes.new('ShaderNodeBsdfTransparent')
            mix = nodes.new('ShaderNodeMixShader')
            links.new(principled.inputs['Alpha'].links[0].from_socket, mix.inputs[0])
            links.new(transparent.outputs[0], mix.inputs[1])
            links.new(result, mix.inputs[2])
            result = mix.outputs[0]
        links.new(result, nodes.get('Material Output').inputs['Surface'])
    scene = bpy.context.scene
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = scene.render.resolution_y = 512
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    camera_data = bpy.data.cameras.new('Bake camera')
    camera = bpy.data.objects.new('Bake camera', camera_data)
    scene.collection.objects.link(camera)
    scene.camera = camera
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = edge
    # Render the three views into one atlas directly. No resampling/compositing
    # step is needed, and every far tree can share one material/draw batch.
    for obj in near:
        obj.hide_render = True
    for index in range(3):
        angle = index * math.pi / 3
        rotate = Matrix.Translation(center) @ Matrix.Rotation(angle, 4, 'Z') @ Matrix.Translation(-center)
        offset = Matrix.Translation(Vector(((index - 1) * edge, 0, 0)))
        for obj in near:
            copy = obj.copy()
            copy.data = obj.data
            scene.collection.objects.link(copy)
            copy.matrix_world = offset @ rotate @ obj.matrix_world
            copy.hide_render = False
    camera.location = center + Vector((0, -edge * 3, 0))
    camera.rotation_euler = (center - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.ortho_scale = edge * 3
    scene.render.resolution_x = 1536
    atlas_path = output / 'tree_atlas.png'
    scene.render.filepath = str(atlas_path)
    bpy.ops.render.render(write_still=True)
    material = bpy.data.materials.new('Tree impostor atlas')
    material.use_nodes = True
    surface = material.node_tree.nodes.get('Principled BSDF')
    surface.inputs['Roughness'].default_value = 1
    texture = material.node_tree.nodes.new('ShaderNodeTexImage')
    texture.image = bpy.data.images.load(str(atlas_path))
    material.node_tree.links.new(texture.outputs['Color'], surface.inputs['Base Color'])
    material.node_tree.links.new(texture.outputs['Alpha'], surface.inputs['Alpha'])
    cards = []
    for index in range(3):
        angle = index * math.pi / 3
        right = Vector((math.cos(angle), math.sin(angle), 0)) * edge / 2
        up = Vector((0, 0, edge / 2))
        mesh = bpy.data.meshes.new('Tree silhouette')
        mesh.from_pydata([tuple(center - right - up), tuple(center + right - up),
                         tuple(center + right + up), tuple(center - right + up)], [], [(0, 1, 2, 3)])
        mesh.normals_split_custom_set([(0, 0, 1)] * 4)
        uv = mesh.uv_layers.new()
        left, right_uv = index / 3, (index + 1) / 3
        for loop, point in zip(mesh.polygons[0].loop_indices,
                               [(left, 0), (right_uv, 0), (right_uv, 1), (left, 1)]):
            uv.data[loop].uv = point
        mesh.materials.append(material)
        obj = bpy.data.objects.new('Tree card ' + str(index), mesh)
        scene.collection.objects.link(obj)
        cards.append(obj)
    bpy.ops.object.select_all(action='DESELECT')
    for obj in cards:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = cards[0]
    bpy.ops.object.join()
    cards = [bpy.context.view_layer.objects.active]
    export(cards, output / 'tree_distant.gltf')
    print('Prepared tree source LODs in', output)


if __name__ == '__main__':
    main()

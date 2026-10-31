"""Bake an alpha-preserving albedo impostor from source glTF in background Blender.

Example: blender --background --factory-startup --python this_script.py --
  --source model.gltf --output /tmp/card.png --center-height 0.36 --width 1.8
No directional light, AO, or normal-map shading is baked into the image.
"""
import argparse
from pathlib import Path
import sys

import bpy
from mathutils import Vector


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--center-height', type=float, required=True)
    parser.add_argument('--width', type=float, required=True)
    parser.add_argument('--resolution', type=int, default=1024)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if args.width <= 0 or args.resolution <= 0:
        parser.error('Width and resolution must be positive')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(args.source.resolve()))
    for material in bpy.data.materials:
        if not material.use_nodes:
            continue
        nodes, links = material.node_tree.nodes, material.node_tree.links
        surface = next((node for node in nodes if node.type == 'BSDF_PRINCIPLED'), None)
        output = next((node for node in nodes if node.type == 'OUTPUT_MATERIAL'), None)
        if surface is None or output is None:
            continue
        emission = nodes.new('ShaderNodeEmission')
        color = surface.inputs['Base Color']
        if color.is_linked:
            links.new(color.links[0].from_socket, emission.inputs['Color'])
        else:
            emission.inputs['Color'].default_value = color.default_value
        transparent = nodes.new('ShaderNodeBsdfTransparent')
        mix = nodes.new('ShaderNodeMixShader')
        alpha = surface.inputs['Alpha']
        if alpha.is_linked:
            links.new(alpha.links[0].from_socket, mix.inputs[0])
        else:
            mix.inputs[0].default_value = alpha.default_value
        links.new(transparent.outputs[0], mix.inputs[1])
        links.new(emission.outputs[0], mix.inputs[2])
        links.new(mix.outputs[0], output.inputs['Surface'])
    scene = bpy.context.scene
    camera_data = bpy.data.cameras.new('Albedo bake camera')
    camera = bpy.data.objects.new('Albedo bake camera', camera_data)
    scene.collection.objects.link(camera)
    target = Vector((0, 0, args.center_height))
    camera.location = target + Vector((0, -args.width * 3, 0))
    camera.rotation_euler = (target - camera.location).to_track_quat('-Z', 'Y').to_euler()
    camera_data.type = 'ORTHO'
    camera_data.ortho_scale = args.width
    scene.camera = camera
    scene.render.engine = 'BLENDER_EEVEE'
    scene.render.resolution_x = scene.render.resolution_y = args.resolution
    scene.render.resolution_percentage = 100
    scene.render.film_transparent = True
    scene.render.image_settings.file_format = 'PNG'
    scene.render.image_settings.color_mode = 'RGBA'
    scene.view_settings.view_transform = 'Standard'
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    scene.render.filepath = str(args.output.resolve())
    bpy.ops.render.render(write_still=True)


if __name__ == '__main__':
    main()

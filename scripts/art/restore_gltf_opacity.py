"""Connect an authored opacity map and export static glTF with packed RGBA albedo.

Run in background Blender. Input geometry stays unchanged; the output belongs to
normal source asset import/reimport, never to a cooked-output directory.
"""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, required=True)
    parser.add_argument('--opacity', type=Path, required=True)
    parser.add_argument('--material', required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--cutoff', type=float, default=0.5)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    if not math.isfinite(args.cutoff) or not 0 <= args.cutoff <= 1:
        parser.error('Cutoff must be finite and between zero and one')
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    bpy.ops.import_scene.gltf(filepath=str(args.source.resolve()))
    material = bpy.data.materials.get(args.material)
    if material is None or not material.use_nodes:
        raise ValueError('The requested source material does not exist')
    surface = next(node for node in material.node_tree.nodes if node.type == 'BSDF_PRINCIPLED')
    opacity = material.node_tree.nodes.new('ShaderNodeTexImage')
    opacity.image = bpy.data.images.load(str(args.opacity.resolve()))
    opacity.image.colorspace_settings.name = 'Non-Color'
    material.node_tree.links.new(opacity.outputs['Color'], surface.inputs['Alpha'])
    args.output.resolve().parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(args.output.resolve()), export_format='GLTF_SEPARATE',
                             export_image_format='AUTO', use_selection=False)
    # Connecting a raw opacity map can make Blender export BLEND. Preserve a
    # cutout contract so the result remains depth-writing, instanced foliage.
    document = json.loads(args.output.read_text())
    for exported in document.get('materials', []):
        if exported.get('name') == args.material:
            exported['alphaMode'] = 'MASK'
            exported['alphaCutoff'] = args.cutoff
    args.output.write_text(json.dumps(document, indent=2) + '\n')


if __name__ == '__main__':
    main()

"""Prepare maps/manifests after build_understory.py; requires Pillow.

Run from any directory: python prepare_understory.py /tmp/demi-understory
Source records contain verified official Poly Haven URLs and MD5 checksums.
RGB photograph bytes and alpha are retained without autumn recoloring/exposure.
Packed map channels are separated as bytes, never gamma-corrected.
"""
from pathlib import Path
import hashlib
import json
import shutil
import sys

from PIL import Image

PACKAGE = Path(__file__).resolve().parents[1]
CACHE = Path(sys.argv[1]) if len(sys.argv) > 1 else Path('/tmp/demi-understory')
SPECIES = [('grass_medium_01', 'grass_lush', 'Rico Cilliers and Rob Tuytel'),
           ('shrub_03', 'shrub', 'Rico Cilliers'),
           ('shrub_sorrel_01', 'sorrel', 'Rico Cilliers'),
           ('flower_gazania', 'wildflowers', 'James Ray Cock and Jenelle van Heerden')]


def save(path, document):
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(document, indent=2) + '\n')


def source_hash(path):
    value = 14695981039346656037
    for byte in path.name.encode() + b'\0' + path.read_bytes():
        value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
    return f'fnv1a64:{value:016x}'


def manifest(path, identifier, kind, importer, settings=None, dependencies=None):
    target = path.with_suffix('.asset.json')
    save(target, {'format_version': 1, 'id': identifier, 'type': kind,
                  'importer': importer, 'importer_version': 1, 'source': path.name,
                  'source_hash': source_hash(path), 'settings': settings or {},
                  'dependencies': sorted(dependencies or [])})


sources = []
for original, name, author in SPECIES:
    folder = CACHE / original
    record = json.loads((folder / 'files.json').read_text())
    gltf_record = record['gltf']['1k']['gltf']
    inputs = {original + '_1k.gltf': {key: value for key, value in gltf_record.items() if key != 'include'}}
    inputs.update(gltf_record['include'])
    inputs['textures/' + original + '_alpha_1k.png'] = record['Alpha']['1k']['png']
    for relative, metadata in inputs.items():
        assert hashlib.md5((folder / relative).read_bytes()).hexdigest() == metadata['md5'], relative
    sources.append({'asset': original, 'author': author, 'license': 'CC0-1.0',
                    'source': 'https://polyhaven.com/a/' + original, 'inputs': inputs})
    textures = PACKAGE / 'assets/understory/textures' / name
    textures.mkdir(parents=True, exist_ok=True)
    diffuse = Image.open(folder / 'textures' / (original + '_diff_1k.jpg')).convert('RGB')
    alpha = Image.open(folder / 'textures' / (original + '_alpha_1k.png')).convert('L')
    assert diffuse.size == alpha.size == (1024, 1024)
    rgba = diffuse.convert('RGBA')
    rgba.putalpha(alpha)
    rgba.save(textures / 'albedo.png')
    shutil.copy2(folder / 'textures' / (original + '_nor_gl_1k.jpg'), textures / 'normal.jpg')
    packed = Image.open(folder / 'textures' / (original + '_arm_1k.jpg')).convert('RGB')
    packed.getchannel('R').save(textures / 'ambient_occlusion.png')
    packed.getchannel('G').save(textures / 'roughness.png')
    texture_ids = {}
    for role, filename in [('albedo', 'albedo.png'), ('normal', 'normal.jpg'),
                           ('ambient_occlusion', 'ambient_occlusion.png'), ('roughness', 'roughness.png')]:
        identifier = f'asset://demi/woodland/understory/{name}/{role}'
        manifest(textures / filename, identifier, 'Texture2D', 'image',
                 {'filter': 'trilinear', 'mipmaps': True, 'wrap': 'clamp'})
        texture_ids[role] = identifier
    material = PACKAGE / 'assets/understory/materials' / (name + '.material.json')
    save(material, {'format_version': 1, 'shader': 'builtin://lit', 'mapping': 'uv',
                    'normal_map_convention': 'opengl', 'textures': texture_ids,
                    'parameters': {'roughness': 0.9, 'normal_strength': 0.65,
                                   'wind_strength': 0.025 if name != 'grass_lush' else 0.06,
                                   'wind_height': 0.9 if name == 'shrub' else 0.65,
                                   'wind_frequency': 1.1},
                    'render_state': {'cull': 'none', 'blend': 'opaque', 'alpha_cutoff': 0.35}})
    manifest(material, f'asset://demi/woodland/understory/{name}/material', 'Material',
             'material', dependencies=texture_ids.values())
    # Material sources use *.material.asset.json rather than *.asset.asset.json.
    for suffix in ['', '_medium', '_low']:
        model = PACKAGE / 'assets/understory/models' / (name + suffix + '.glb')
        manifest(model, f'asset://demi/woodland/understory/{name}{suffix}', 'Model3D',
                 'gltf-model', {'model_import': {'format_version': 1, 'preset': 'static_prop',
                                               'source_up': '+y', 'source_forward': '+z',
                                               'meters_per_unit': 1, 'root_node': 'preserve',
                                               'material_policy': 'import', 'import_animations': False,
                                               'optimize_meshes': True}})
    material_id = f'asset://demi/woodland/understory/{name}/material'
    model_id = f'asset://demi/woodland/understory/{name}'
    save(PACKAGE / 'prefabs' / (name + '.prefab.json'),
         {'format_version': 1, 'id': f'prefab://demi/woodland/{name}',
          'name': name.replace('_', ' ').title(),
          'entities': [{'id': 'plant', 'name': name.replace('_', ' ').title(),
                        'components': {'Transform3D': {}, 'MeshRenderer': {
                            'model': model_id, 'medium_lod_model': model_id + '_medium',
                            'medium_lod_distance': 35, 'low_lod_model': model_id + '_low',
                            'low_lod_distance': 90, 'cull_distance': 220,
                            'material': material_id}}}]})
save(PACKAGE / 'sources-understory.json', sources)

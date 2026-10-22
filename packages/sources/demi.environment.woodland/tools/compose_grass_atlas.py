"""Compose the authored grass LOD atlas after bake_grass_lod.py; Pillow only."""
from pathlib import Path
import json
import struct

from PIL import Image

PACKAGE = Path(__file__).resolve().parents[1]
TEXTURES = PACKAGE / 'assets/understory/textures/grass_lush'
BAKES = PACKAGE / 'previews/grass_lush_bake'
original = Image.open(TEXTURES / 'albedo.png').convert('RGBA')
assert original.size == (1024, 1024), 'Regenerate original source maps before composing'
albedo = Image.new('RGBA', (2048, 2048), (0, 0, 0, 0))
albedo.paste(original, (0, 0))
for index in range(5):
    view = Image.open(BAKES / f'view_{index}.png').convert('RGBA')
    assert view.size == (512, 512)
    assert view.getchannel('A').getbbox(), 'Empty baked silhouette'
    position = (1024 + (index % 2) * 512, (index // 2) * 512) if index < 4 else (0, 1024)
    albedo.paste(view, position)
albedo.save(TEXTURES / 'albedo.png')
normal = Image.new('RGB', (2048, 2048), (128, 128, 255))
normal.paste(Image.open(TEXTURES / 'normal.jpg').convert('RGB'), (0, 0))
normal.save(TEXTURES / 'normal.png')
(TEXTURES / 'normal.jpg').unlink()
for name, neutral in [('ambient_occlusion', 255), ('roughness', 230)]:
    atlas = Image.new('L', (2048, 2048), neutral)
    atlas.paste(Image.open(TEXTURES / (name + '.png')).convert('L'), (0, 0))
    atlas.save(TEXTURES / (name + '.png'))
manifest = TEXTURES / 'normal.asset.json'
document = json.loads(manifest.read_text())
document['source'] = 'normal.png'
manifest.write_text(json.dumps(document, indent=2) + '\n')
# The material remains shared by every LOD: near UVs select the original left
# image, far UVs select the four baked views at right.
for folder in [TEXTURES, PACKAGE / 'assets/understory/models']:
    for manifest in folder.glob('grass_lush*.asset.json' if folder.name == 'models' else '*.asset.json'):
        document = json.loads(manifest.read_text())
        source = manifest.parent / document['source']
        value = 14695981039346656037
        for byte in source.name.encode() + b'\0' + source.read_bytes():
            value = ((value ^ byte) * 1099511628211) & ((1 << 64) - 1)
        document['source_hash'] = f'fnv1a64:{value:016x}'
        manifest.write_text(json.dumps(document, indent=2) + '\n')
summary_path = PACKAGE / 'understory-summary.json'
summary = json.loads(summary_path.read_text())
for entry in summary:
    if entry['name'] == 'grass_lush_low':
        entry['triangles'] = 10
        binary = (PACKAGE / 'assets/understory/models/grass_lush_low.glb').read_bytes()
        length = struct.unpack_from('<I', binary, 12)[0]
        model = json.loads(binary[20:20 + length])
        position_index = model['meshes'][0]['primitives'][0]['attributes']['POSITION']
        position = model['accessors'][position_index]
        entry['bounds_min'] = position['min']
        entry['bounds_max'] = position['max']
        entry['representation'] = 'Four side-view cards plus one top coverage quad, baked from the full near plant'
    if entry['name'].startswith('grass_lush'):
        entry['bytes'] = (PACKAGE / 'assets/understory/models' / (entry['name'] + '.glb')).stat().st_size
summary_path.write_text(json.dumps(summary, indent=2) + '\n')

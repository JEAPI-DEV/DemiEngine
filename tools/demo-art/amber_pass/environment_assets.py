"""Prepare Poly Haven CC0 environment, using verified downloads in /tmp/demi-environment.
Run: blender -b --python tools/demo-art/amber_pass/environment_assets.py
Download URLs and hashes are recorded alongside exported source assets.
"""
from pathlib import Path
import json
import shutil
import bpy
import numpy as np
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'examples/amber_pass_3d/assets/environment'
CACHE = Path('/tmp/demi-environment')
OUT.mkdir(parents=True, exist_ok=True)
(OUT / 'textures').mkdir(exist_ok=True)
# Rehydrate only missing source downloads from the recorded official URLs.
import hashlib
import urllib.request
records = json.loads((OUT / 'downloads.json').read_text())
for record in records:
    destination = CACHE / record['path']
    if not destination.exists():
        destination.parent.mkdir(parents=True, exist_ok=True)
        request = urllib.request.Request(record['url'], headers={'User-Agent': 'DemiEngine CC0 demo preparation'})
        destination.write_bytes(urllib.request.urlopen(request).read())
    if hashlib.md5(destination.read_bytes()).hexdigest() != record['md5']:
        raise RuntimeError('Source hash mismatch: ' + str(destination))
summary = {'license': 'CC0-1.0', 'source': 'https://polyhaven.com/license', 'models': []}

def triangle_count(obj):
    obj.data.calc_loop_triangles()
    return len(obj.data.loop_triangles)

def thin_leaf_triangles(obj, budget):
    # Disconnected leaf cards have a collapse floor. Thin their remaining
    # triangles deterministically when the modifier cannot meet the budget.
    if triangle_count(obj) > budget:
        import bmesh
        bm = bmesh.new()
        bm.from_mesh(obj.data)
        bmesh.ops.triangulate(bm, faces=list(bm.faces))
        faces = list(bm.faces)
        discard = [face for index, face in enumerate(faces) if (index * budget // len(faces)) == ((index + 1) * budget // len(faces))]
        bmesh.ops.delete(bm, geom=discard, context='FACES')
        bm.to_mesh(obj.data)
        bm.free()

def export(obj, name, budget):
    bpy.ops.object.select_all(action='DESELECT')
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    copy = obj.copy()
    copy.data = obj.data.copy()
    bpy.context.collection.objects.link(copy)
    obj.select_set(False)
    copy.select_set(True)
    bpy.context.view_layer.objects.active = copy
    triangles = triangle_count(copy)
    if triangles > budget:
        modifier = copy.modifiers.new('Measured triangle budget', 'DECIMATE')
        modifier.ratio = budget / triangles
        modifier.use_collapse_triangulate = True
        bpy.ops.object.modifier_apply(modifier=modifier.name)
    if name.startswith('tree_leaves'):
        thin_leaf_triangles(copy, budget)
    bpy.ops.export_scene.gltf(filepath=str(OUT / (name + '.glb')), export_format='GLB', use_selection=True, export_animations=False, export_materials='NONE')
    points = [copy.matrix_world @ Vector(corner) for corner in copy.bound_box]
    # Convert Blender coordinates into exported glTF coordinates.
    points = [Vector((p.x, p.z, -p.y)) for p in points]
    summary['models'].append({'name': name, 'file': name + '.glb', 'triangles': triangle_count(copy), 'bytes': (OUT / (name + '.glb')).stat().st_size, 'bounds_min': [min(p[i] for p in points) for i in range(3)], 'bounds_max': [max(p[i] for p in points) for i in range(3)], 'material': obj.get('material_role', obj.data.materials[0].name if obj.data.materials else '')})
    bpy.data.objects.remove(copy, do_unlink=True)

def prepare_material_maps():
    """Split linear packed channels and retain neutral rider color modulation."""
    material_summary = json.loads((OUT / 'summary.json').read_text())
    for role, material in material_summary['materials'].items():
        if 'arm' not in material:
            continue
        image = bpy.data.images.load(str(OUT / material['arm']), check_existing=False)
        image.colorspace_settings.name = 'Non-Color'
        width, height = image.size
        values = np.empty(width * height * 4, dtype=np.float32)
        image.pixels.foreach_get(values)
        values = values.reshape(height, width, 4)
        for channel, name in [(0, 'ao'), (1, 'roughness')]:
            result = bpy.data.images.new(role + '_' + name, width=width, height=height, alpha=False)
            result.colorspace_settings.name = 'Non-Color'
            output = np.ones((height, width, 4), dtype=np.float32)
            output[:,:,:3] = values[:,:,channel:channel+1]
            result.pixels.foreach_set(output.ravel())
            relative_path = 'textures/' + Path(material['arm']).stem.replace('_arm_', '_' + name + '_') + '.png'
            result.filepath_raw = str(OUT / relative_path)
            result.file_format = 'PNG'
            result.save()
            material[name] = relative_path
    for role, asset, color in [('rider_cloth', 'fabric_pattern_07', 'col_1'), ('rider_leather', 'fabric_leather_01', 'diff'), ('rider_metal', 'metal_plate_02', 'diff')]:
        for path in (CACHE / 'textures').glob(asset + '*'):
            shutil.copy2(path, OUT / 'textures' / path.name)
        source_path = next((OUT / 'textures').glob(asset + '_' + color + '_1k.*'))
        source = bpy.data.images.load(str(source_path), check_existing=False)
        width, height = source.size
        values = np.empty(width * height * 4, dtype=np.float32)
        source.pixels.foreach_get(values)
        values = values.reshape(height, width, 4)
        luminance = values[:,:,:3].mean(axis=2)
        # Neutral, restrained variation lets existing rider vertex colors lead.
        luminance = np.clip(.72 + .22 * (luminance - luminance.mean()), .55, .9)
        values[:,:,:3] = luminance[:,:,None]
        values[:,:,3] = 1
        output = bpy.data.images.new(role + '_neutral', width=width, height=height, alpha=False)
        output.pixels.foreach_set(values.ravel())
        relative_path = 'textures/' + role + '_neutral.png'
        output.filepath_raw = str(OUT / relative_path)
        output.file_format = 'PNG'
        output.save()
        material_summary['materials'][role] = {
            'albedo': relative_path,
            'normal': 'textures/' + asset + '_nor_gl_1k.png',
            'roughness': 'textures/' + asset + '_rough_1k.png',
            'source': 'https://polyhaven.com/a/' + asset,
        }
    (OUT / 'summary.json').write_text(json.dumps(material_summary, indent=2) + '\n')

import sys
if '--materials-only' in sys.argv:
    prepare_material_maps()
    raise SystemExit(0)

for asset in ['rock_moss_set_01', 'coastal_cliff_02', 'tree_small_02']:
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.gltf(filepath=str(CACHE / asset / (asset + '_2k.gltf')))
    for path in (CACHE / asset / 'textures').iterdir():
        shutil.copy2(path, OUT / 'textures' / path.name)
    meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    if asset == 'tree_small_02':
        obj = meshes[0]
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.mesh.separate(type='MATERIAL')
        bpy.ops.object.mode_set(mode='OBJECT')
        meshes = [o for o in bpy.data.objects if o.type == 'MESH']
    for index, obj in enumerate(sorted(meshes, key=lambda o:o.name)):
        if asset == 'rock_moss_set_01':
            name = 'rock_' + str(index + 1).zfill(2)
            # Source set is laid out in a row. Give each reusable rock its own foot origin.
            points = [obj.matrix_world @ Vector(corner) for corner in obj.bound_box]
            center = Vector(((min(p.x for p in points)+max(p.x for p in points))/2, (min(p.y for p in points)+max(p.y for p in points))/2, min(p.z for p in points)))
            obj.location -= center
            budget, mid = 6000, 2000
        elif asset == 'coastal_cliff_02':
            name, budget, mid = 'coastal_cliff', 25000, 6000
        else:
            material_name = obj.data.materials[0].name
            role = 'leaves' if 'leaves' in material_name else 'branches' if 'branches' in material_name else 'trunk'
            name = 'tree_' + role
            obj['material_role'] = role
            budget, mid = {'leaves': (14000, 4000), 'branches': (3500, 1000), 'trunk': (2500, 1000)}[role]
        export(obj, name, budget)
        export(obj, name + '_mid', mid)
        # Keep only the production-resolution mesh in the editable source file.
        bpy.context.view_layer.objects.active = obj
        triangles = triangle_count(obj)
        if triangles > budget:
            mod = obj.modifiers.new('Production mesh budget', 'DECIMATE')
            mod.ratio = budget / triangles
            mod.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=mod.name)
        if asset == 'tree_small_02' and obj.get('material_role') == 'leaves':
            thin_leaf_triangles(obj, budget)
    for image in bpy.data.images:
        if image.has_data and not image.packed_file:
            image.pack()
    bpy.ops.wm.save_as_mainfile(filepath=str(OUT / (asset + '.blend')))

# Derive autumn color while retaining the source diffuse's transparency.
path = OUT / 'textures/tree_small_02_leaves_diff_2k.jpg'
if not path.exists():
    path = next((OUT/'textures').glob('tree_small_02_leaves_diff_2k.*'))
image = bpy.data.images.load(str(path), check_existing=False)
width, height = image.size
pixels = np.empty(width * height * 4, dtype=np.float32)
image.pixels.foreach_get(pixels)
pixels = pixels.reshape(height, width, 4)
luminance = .2126 * pixels[:,:,0] + .7152 * pixels[:,:,1] + .0722 * pixels[:,:,2]
pixels[:,:,0] = np.minimum(1, luminance * 2.7)
pixels[:,:,1] = np.minimum(1, luminance * 1.25)
pixels[:,:,2] = luminance * .14
alpha_image = bpy.data.images.load(str(OUT / 'textures/tree_small_02_leaves_alpha_2k.png'))
alpha_image.colorspace_settings.name = 'Non-Color'
alpha_pixels = np.empty(width * height * 4, dtype=np.float32)
alpha_image.pixels.foreach_get(alpha_pixels)
pixels[:,:,3] = alpha_pixels.reshape(height, width, 4)[:,:,0]
# JPEG diffuse sources have no alpha channel, so create an RGBA output explicitly.
autumn = bpy.data.images.new('Autumn leaves RGBA', width=width, height=height, alpha=True)
autumn.pixels.foreach_set(pixels.ravel())
image = autumn
image.filepath_raw = str(OUT / 'textures/autumn_leaves.png')
image.file_format = 'PNG'
image.save()

sky = bpy.data.images.load(str(CACHE / 'sky.hdr'))
sky.scale(2048, 1024)
scene = bpy.context.scene
scene.view_settings.view_transform = 'AgX'
scene.view_settings.look = 'AgX - Medium High Contrast'
scene.view_settings.exposure = -.5
sky.save_render(str(OUT / 'textures/kloppenheim_06_puresky.png'), scene=scene)
summary['materials'] = {
    'rock_moss_set_01': {'albedo':'textures/rock_moss_set_01_diff_2k.jpg','normal':'textures/rock_moss_set_01_nor_gl_2k.jpg','roughness':'textures/rock_moss_set_01_rough_2k.jpg'},
    'coastal_cliff_02': {'albedo':'textures/coastal_cliff_02_diff_2k.jpg','normal':'textures/coastal_cliff_02_nor_gl_2k.jpg','arm':'textures/coastal_cliff_02_arm_2k.jpg'},
    'trunk': {'albedo':'textures/tree_small_02_diff_2k.jpg','normal':'textures/tree_small_02_nor_gl_2k.jpg','arm':'textures/tree_small_02_arm_2k.jpg'},
    'branches': {'albedo':'textures/tree_small_02_branch_diff_2k.jpg','normal':'textures/tree_small_02_branch_nor_gl_2k.jpg','arm':'textures/tree_small_02_branch_arm_2k.jpg'},
    'leaves': {'albedo':'textures/autumn_leaves.png','normal':'textures/tree_small_02_leaves_nor_gl_2k.jpg','arm':'textures/tree_small_02_leaves_arm_2k.jpg','alpha_cutoff':0.5,'double_sided':True},
}
summary['sky'] = 'textures/kloppenheim_06_puresky.png'
(OUT / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
shutil.copy2(CACHE / 'downloads.json', OUT / 'downloads.json')
print('ENVIRONMENT_FINISHED', json.dumps(summary))

prepare_material_maps()

# Finish with intact foliage cards and authored photo-grass patch variants.
import runpy
runpy.run_path(str(Path(__file__).with_name('foliage_assets.py')))
prepare_material_maps()

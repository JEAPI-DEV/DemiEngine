"""Author Amber Pass: deterministic scene layout and engine-native terrain.

Run after horse.py, rider.py, ruins.py and environment_assets.py. These are
source authoring tools; generated/cooked runtime outputs remain engine-owned.
"""
from pathlib import Path
import json
import math
import random
import re
import subprocess
import terrain_landscape
import loading_screen


ROOT = Path(__file__).resolve().parents[3]
PROJECT = ROOT / 'examples/amber_pass_3d'
ASSETS = PROJECT / 'assets'


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    text = json.dumps(value, indent=2)
    numeric_array = r"\[\n\s+((?:[-+0-9.eE]+,\n\s+)*[-+0-9.eE]+)\n\s+\]"
    text = re.sub(numeric_array, lambda match: "[" + ", ".join(
        part.strip() for part in match.group(1).split(",")) + "]", text)
    path.write_text(text + "\n")


def asset_id(name):
    return 'asset://amber_pass/' + name


def hash_file(path):
    value = 14695981039346656037
    for byte in path.read_bytes():
        value = ((value ^ byte) * 1099511628211) & 0xffffffffffffffff
    return f"fnv1a64:{value:016x}"


def manifest(source, name, kind, importer, settings=None, dependencies=None):
    output = source.with_suffix('.asset.json')
    write_json(output, {
        'format_version': 1,
        'id': asset_id(name),
        'type': kind,
        'source': source.name,
        'importer': importer,
        'importer_version': 1,
        'source_hash': hash_file(source),
        'settings': settings or {},
        'dependencies': dependencies or [],
    })
    return asset_id(name)


def model(path, name, animated=False):
    return manifest(path, 'models/' + name, 'Model3D', 'gltf-model', {
        'model_import': {
            'format_version': 1,
            'preset': 'animated_character' if animated else 'static_prop',
            'source_up': '+y',
            'source_forward': '+z',
            'meters_per_unit': 1,
            'root_node': 'preserve',
            'material_policy': 'import',
            'import_animations': animated,
            'optimize_meshes': not animated,
        },
        'filter': 'trilinear',
        'mipmaps': True,
    })


def texture(path, name):
    return manifest(path, 'textures/' + name, 'Texture2D', 'image', {
        'filter': 'trilinear',
        'mipmaps': True,
        'wrap': 'repeat',
    })


def material(name, maps=None, parameters=None, mapping='uv', extra=None):
    value = {
        'format_version': 1,
        'shader': 'builtin://lit',
        'mapping': mapping,
        'parameters': parameters or {
            'roughness': 0.9,
        },
    }
    if maps:
        value['textures'] = maps
    if extra:
        value.update(extra)
    path = ASSETS / 'materials' / (name + '.material.json')
    write_json(path, value)
    dependencies = list((maps or {}).values())
    return manifest(
        path, 'materials/' + name, 'Material', 'material',
        dependencies=dependencies,
    )

terrain_height = terrain_landscape.terrain_height


def trail_x(z):
    return 7 * math.sin(z / 28)


def entity(identifier, components, children=None, name=None):
    value = {
        'id': identifier,
        'name': name or identifier.replace('_', ' ').title(),
        'components': components,
    }
    if children:
        value['children'] = children
    return value


def transform(position, scale=1, yaw=0):
    return {
        'position': position,
        'rotation': [0, yaw, 0],
        'scale': [scale, scale, scale] if isinstance(scale, (float, int)) else scale,
    }


def mesh(name, material_name=None, lod=False, tint=None):
    value = {
        'model': 'asset://demi/animals/horse' if name == 'horse' else asset_id('models/' + name),
    }
    if material_name:
        value['material'] = asset_id('materials/' + material_name)
    if lod:
        value.update(medium_lod_model=asset_id('models/' + name + '_mid'), medium_lod_distance=35)
    if tint:
        value['color'] = tint
    return value


def build_assets():
    for part in ['armor', 'cloth', 'leather', 'shield']:
        model(ASSETS / 'rider' / ('rider_' + part + '.glb'), 'rider_' + part)
    model(ASSETS / 'ruins/ruin.glb', 'ruin')
    environment = ASSETS / 'environment'
    summary = json.loads((environment / 'summary.json').read_text())
    for entry in summary['models']:
        model(environment / entry['file'], entry['name'])
    surface_maps = {}
    for name, source_maps in summary['materials'].items():
        maps = {}
        for slot in ['albedo', 'normal', 'roughness', 'ambient_occlusion']:
            source_slot = 'ao' if slot == 'ambient_occlusion' else slot
            if source_slot in source_maps:
                maps[slot] = texture(environment / source_maps[source_slot], name + '/' + slot)
        surface_maps[name] = maps
        if name.startswith('rider_'):
            continue
        parameters = {
            'roughness': 0.88,
            'normal_strength': 0.7,
        }
        extra = {
            'render_state': {
                'cull': 'none',
                'blend': 'opaque',
            },
        }
        if name in ['leaves', 'grass_photo']:
            parameters.update(wind_strength=0.08, wind_height=5.0, wind_frequency=0.7)
            extra['render_state']['alpha_cutoff'] = 0.5
        material(name, maps, parameters, extra=extra)
    mountain_maps = {
        slot: 'asset://polyhaven/terrain/textures/marble_cliff_05/' + slot
        for slot in ['albedo', 'normal', 'roughness']
    }
    material('mountain_rock', mountain_maps, {
        'roughness': 0.9,
        'normal_strength': 0.7,
        'tiling': 0.05,
    }, 'triplanar')
    texture(environment / summary['sky'], 'sky')
    package = 'asset://polyhaven/terrain/textures/'
    terrain_materials = [
        ('grass', 'grass_ground', [0.9, 0.95, 0.8, 1]),
        ('trail', 'rocky_gravel', [0.75, 0.78, 0.68, 1]),
        ('masonry', 'marble_cliff_05', [0.63, 0.64, 0.54, 1]),
    ]
    for name, source, tint in terrain_materials:
        maps = {slot: package + source + '/' + slot for slot in ['albedo', 'normal', 'roughness']}
        material(name, maps, {
            'base_color': tint,
            'roughness': 0.95,
            'normal_strength': 0.65,
            'tiling': 0.6,
        }, 'triplanar')
    material('armor', surface_maps['rider_metal'], {
        'roughness': 0.6,
        'metallic': 0.3,
        'normal_strength': 0.25,
        'tiling': 5,
    }, 'triplanar')
    material('cloth', surface_maps['rider_cloth'], {
        'roughness': 1.0,
        'normal_strength': 0.35,
        'tiling': 7,
    }, 'triplanar')
    material('leather', surface_maps['rider_leather'], {
        'roughness': 0.84,
        'normal_strength': 0.4,
        'tiling': 4,
    }, 'triplanar')
    material('shield', surface_maps['rider_leather'], {
        'roughness': 0.7,
        'metallic': 0.25,
        'normal_strength': 0.2,
        'tiling': 3,
    }, 'triplanar')


def build_terrain():
    terrain_landscape.write_terrain(PROJECT, write_json, manifest)


def mounted_traveller():
    horse_states = {}
    for name, duration in [('Idle', 3), ('Walk', 1.2), ('Run', 2 / 3)]:
        horse_states[name.lower()] = {
            'model_clip_name': name,
            'duration': duration,
            'loop': True,
        }
    children = [entity('horse', {
        'Transform3D': {},
        'MeshRenderer': mesh('horse'),
        'AnimationPlayer3D': {
            'clip_name': 'Idle',
            'loop': True,
        },
        'AnimationStateMachine': {
            'initial_state': 'idle',
            'states': horse_states,
        },
    })]
    for part in ['armor', 'cloth', 'leather', 'shield']:
        children.append(entity('rider_' + part, {
            'Transform3D': {
                'position': [0, 1.67, 0.42],
            },
            'MeshRenderer': mesh('rider_' + part, part),
        }))
    return entity('mount', {
        'Transform3D': {
            'position': [0, 0.8, 0],
        },
        'CapsuleCollider3D': {
            'radius': 0.6,
            'height': 2,
            'offset': [0, 1, 0],
            'layer': 'player',
        },
        'CharacterController3D': {
            'gravity': -22,
            'step_height': 0.45,
            'slope_limit': 48,
        },
        'LuaScript': {
            'module': 'script://scripts/riding_controller.lua',
        },
    }, children, 'Mounted traveller')


def camera_and_environment():
    return [entity('camera', {
        'Transform3D': {
            'position': [0, 3.6, -7.3],
        },
        'Camera3D': {
            'target_offset': [0, 0, 1],
            'fov': 56,
            'near_clip': 0.1,
            'far_clip': 500,
            'clear_color': [0.42, 0.43, 0.34, 1],
        },
        'PostProcessStack': {
            'contrast': 1.04,
            'saturation': 0.92,
            'vignette': 0.12,
        },
    }), entity('sun', {
        'DirectionalLight': {
            'direction': [-0.4, -0.75, -0.6],
            'color': [1, 0.94, 0.82, 1],
            'intensity': 1.8,
            'casts_shadows': True,
        },
    }), entity('environment', {
        'Environment3D': {
            'sky_texture': asset_id('textures/sky'),
            'ambient_color': [0.83, 0.84, 0.82, 1],
            'ambient_intensity': 0.85,
            'shadow_cascades': 3,
            'shadow_distance': 110,
            'shadow_resolution': 2048,
            'shadow_bias': 0.015,
            'msaa_samples': 4,
        },
    })]


def valley_entity():
    return entity('valley', {
        'Transform3D': {
            'position': list(terrain_landscape.WORLD_ORIGIN),
        },
        'Terrain3D': {
            'asset': asset_id('valley'),
            **terrain_landscape.TERRAIN_COMPONENT_SETTINGS,
        },
    })


def chapel_entity():
    gate_z = 52
    gate_x = trail_x(gate_z)
    position = [gate_x, terrain_height(gate_x, gate_z), gate_z]
    return entity('chapel', {
        'Transform3D': transform(position),
        'MeshRenderer': mesh('ruin', 'masonry'),
        'ModelCollider3D': {
            'asset': asset_id('colliders/ruin'),
            'layer': 'world',
        },
    })


def place_rocks(rng):
    rocks = []
    for index in range(65):
        z = rng.uniform(-10, 185)
        side = -1 if index % 2 else 1
        x = trail_x(z) + side * rng.uniform(6, 40)
        scale = rng.uniform(0.8, 2.5)
        part = f'rock_{index % 6 + 1:02}'
        position = [x, terrain_height(x, z) - 0.14, z]
        yaw = rng.uniform(0, math.tau)
        rocks.append(entity(f'boulder_{index:02}', {
            'Transform3D': transform(position, scale, yaw),
            'MeshRenderer': mesh(part, 'rock_moss_set_01', True),
            'ModelCollider3D': {
                'asset': asset_id('colliders/' + part),
                'layer': 'world',
            },
        }))
    return rocks


def place_trees(rng):
    trees = []
    for index in range(80):
        z = rng.uniform(-3, 185)
        side = -1 if index % 2 else 1
        x = trail_x(z) + side * rng.uniform(9, 38)
        scale = rng.uniform(1.4, 2.4)
        parts = []
        for part in ['trunk', 'branches', 'leaves']:
            parts.append(entity(f'tree_{index}_{part}', {
                'Transform3D': {},
                'MeshRenderer': mesh('tree_' + part, part, True),
            }))
        position = [x, terrain_height(x, z) - 0.08, z]
        yaw = rng.uniform(0, math.tau)
        trees.append(entity(f'tree_{index:02}', {
            'Transform3D': transform(position, scale, yaw),
            'CapsuleCollider3D': {
                'radius': 0.13,
                'height': 2.8,
                'offset': [0, 1.4, 0],
                'layer': 'world',
            },
        }, parts))
    return trees


def place_grass(rng):
    instances = {}
    for index in range(35000):
        z = rng.uniform(-15, 175)
        x = trail_x(z) + rng.uniform(-30, 30)
        if abs(x - trail_x(z)) < 3.7:
            continue
        scale = rng.uniform(2.0, 3.5)
        position = [x, terrain_height(x, z), z]
        yaw = rng.uniform(0, math.tau)
        instances[f'tuft_{index:04}'] = transform(position, scale, yaw)
    return entity('meadow_grass', {
        'Transform3D': {},
        'MeshRenderer': {
            'model': asset_id('models/grass_patch'),
            'medium_lod_model': asset_id('models/grass_patch_mid'),
            'medium_lod_distance': 10,
            'low_lod_model': asset_id('models/grass_patch_low'),
            'low_lod_distance': 35,
            'cull_distance': 160,
            'material': asset_id('materials/grass_photo'),
        },
        'MeshInstances3D': {
            'transforms': instances,
        },
    })


def build_scene():
    mounted = mounted_traveller()
    write_json(PROJECT / 'prefabs/amber_pass/mounted_traveller.prefab.json', {
        'format_version': 1,
        'id': 'prefab://amber_pass/mounted_traveller',
        'entities': [mounted],
    })
    # Keep the editable hierarchy identical to the reusable mounted prefab.
    entities = [mounted]
    entities.extend(camera_and_environment())
    entities.append(valley_entity())
    entities.append(chapel_entity())
    # One seeded sequence preserves stable placement identities and layout.
    rng = random.Random(731)
    entities.extend(place_rocks(rng))
    entities.extend(place_trees(rng))
    entities.append(place_grass(rng))
    write_json(PROJECT / 'scenes/main.scene.json', {
        'format_version': 1,
        'id': 'scene://amber_pass/main',
        'name': 'Amber Pass — the old road',
        'hud': '../hud/riding.hud.json',
        'entities': entities,
    })


def build_project_and_hud():
    write_json(PROJECT / 'demi.project.json', {
        'format_version': 1,
        'name': 'Amber Pass',
        'main_scene': 'scene://amber_pass/loading',
        'assets': [loading_screen.BACKGROUND_ID],
        'scenes': [{'id': 'scene://amber_pass/loading'}, {
            'id': 'scene://amber_pass/main',
        }],
        'package_registry': '../../packages',
        'packages': {
            'polyhaven.materials.terrain': '^1.2.0',
            'demi.animals.horse': '^0.1.0',
        },
        'input': {
            'actions': {
                'riding_move': {
                    'type': 'vector2',
                    'bindings': [{
                        'input': 'key:w',
                        'vector': [0, -1],
                    }, {
                        'input': 'key:s',
                        'vector': [0, 1],
                    }, {
                        'input': 'key:a',
                        'vector': [-1, 0],
                    }, {
                        'input': 'key:d',
                        'vector': [1, 0],
                    }, {
                        'input': 'virtual:riding_move',
                    }],
                },
                'riding_gallop': {
                    'type': 'button',
                    'bindings': [{
                        'input': 'key:left_shift',
                    }],
                },
                'riding_reset': {
                    'type': 'button',
                    'bindings': [{
                        'input': 'key:r',
                    }],
                },
            },
        },
        'scripting': {
            'language': 'lua54',
            'modules': [],
        },
    })
    write_json(ASSETS / 'groups/showcase.asset-group.json', {
        'format_version': 1,
        'id': 'asset-group://amber_pass/showcase',
        'roots': ['scene://amber_pass/main'],
        'budget': {
            'resident_mb': 384,
            'decoded_mb': 256,
            'upload_ms_per_frame': 3,
        },
    })
    write_json(PROJECT / 'hud/riding.hud.json', {
        'format_version': 1,
        'canvas_size': [1280, 720],
        'children': [{
            'id': 'title',
            'type': 'label',
            'text': 'AMBER PASS',
            'at': [38, 28],
            'size': [320, 36],
            'font_size': 26,
            'color': '#F2DFB8FF',
        }, {
            'id': 'subtitle',
            'type': 'label',
            'text': 'THE OLD ROAD  /  DEMI ENGINE',
            'at': [40, 66],
            'size': [360, 24],
            'font_size': 12,
            'color': '#D3C9B6DD',
        }, {
            'id': 'controls',
            'type': 'label',
            'text': 'WASD Ride   •   Shift Gallop   •   RMB Look   •   R Return',
            'dock': 'bottom',
            'size': [600, 26],
            'at': [320, -22],
            'font_size': 14,
            'color': '#F2DFB8DD',
        }, {
            'id': 'touch_toggle',
            'type': 'button',
            'text': 'Touch controls',
            'at': [40, 680],
            'size': [130, 28],
            'font_size': 12,
            'background_color': '#171C1F55',
            'color': '#E0CD99AA',
        }, {
            'id': 'riding_stick',
            'type': 'virtual_stick',
            'control': 'riding_move',
            'visible': False,
            'at': [40, 566],
            'size': [110, 110],
            'radius': 55,
            'deadzone': 0.12,
            'background_color': '#171C1F55',
            'color': '#E0CD9955',
        }, {
            'id': 'riding_forward',
            'type': 'label',
            'text': '↑',
            'visible': False,
            'at': [85, 566],
            'size': [20, 20],
            'font_size': 17,
            'color': '#E0CD99AA',
        }],
    })

def import_authored_assets(engine):
    # Importers own source hashes and dependency discovery.
    subprocess.run(
        [str(engine), 'package', 'install', '--project', str(PROJECT)],
        check=True,
    )
    for path in sorted(ASSETS.rglob('*.asset.json')):
        subprocess.run([str(engine), 'asset', 'reimport', str(path)], check=True)


def create_missing_colliders(engine):
    collision_models = [('coastal_cliff_mid', 'cliff', .5)]
    collision_models += [
        (f'rock_{index:02}_mid', f'rock_{index:02}', .6)
        for index in range(1, 7)
    ]
    for model_name, collider_name, detail in collision_models:
        collider = ASSETS / 'amber_pass/colliders' / collider_name / 'collider.asset.json'
        if collider.exists():
            continue
        source = ASSETS / 'environment' / (model_name + '.asset.json')
        command = [
            str(engine), 'asset', 'collider', str(source),
            '--project', str(PROJECT),
            '--id', asset_id('colliders/' + collider_name),
            '--body', 'static',
            '--detail', str(detail),
        ]
        subprocess.run(command, check=True)

    ruin_collider = ASSETS / 'amber_pass/colliders/ruin/collider.asset.json'
    if not ruin_collider.exists():
        command = [
            str(engine), 'asset', 'collider', str(ASSETS / 'ruins/ruin.asset.json'),
            '--project', str(PROJECT),
            '--id', asset_id('colliders/ruin'),
            '--body', 'static',
            '--detail', '1',
        ]
        subprocess.run(command, check=True)


if __name__ == '__main__':
    build_assets()
    build_terrain()
    build_scene()
    build_project_and_hud()
    loading_screen.write_documents(PROJECT, write_json, manifest)

    engine = ROOT / 'build/linux-release/demi'
    import_authored_assets(engine)
    create_missing_colliders(engine)

"""Configure the existing animation example as a matched live/frozen crowd."""
import json
import math
from split_skin_fixture import split_skin


WORKLOADS = ('animated', 'frozen', 'mixed')


def configure(project, settings, count, workload, seconds, rig_layout='original', visual_rate=0, visual_distance=30):
    if workload not in WORKLOADS or not 1 <= count <= 2000:
        raise ValueError('Crowd requires animated/frozen and 1..2000 characters')
    settings['main_scene'] = 'scene://animation_3d/crowd'
    if rig_layout == 'split':
        manifest_path = project / 'assets/AnimationLib/ual1_standard.asset.json'
        manifest = json.loads(manifest_path.read_text())
        split_skin(manifest_path.parent / manifest['source'],
                   manifest_path.parent / 'UAL1_Split.glb')
        manifest['source'] = 'UAL1_Split.glb'
        manifest_path.write_text(json.dumps(manifest, indent=2) + '\n')
    elif rig_layout != 'original':
        raise ValueError('Unknown crowd rig layout')
    scene_path = project / 'scenes/crowd.scene.json'
    scene = json.loads(scene_path.read_text())
    entities = {entity['id']: entity['components'] for entity in scene['entities']}
    entities['crowd']['LuaScript']['properties'] = {
        'count': count, 'frozen': workload == 'frozen', 'duration_seconds': seconds}
    if workload == 'mixed': entities['crowd']['LuaScript']['properties']['mixed'] = True
    if visual_rate > 0:
        entities['crowd']['LuaScript']['properties'].update(visual_rate=visual_rate, visual_distance=visual_distance)
    # Match camera and geometry between live/frozen runs at each population.
    span = 2 * math.ceil(math.sqrt(count))
    position = [0, span * 1.2 + 5, span * 1.5 + 5]
    entities['camera']['Transform3D']['position'] = position
    entities['camera']['Camera3D']['target_offset'] = [-v for v in position]
    scene_path.write_text(json.dumps(scene, indent=2) + '\n')


def qualify(result, count, workload, visual_rate=0):
    visible = result.get('Renderer3D.meshes_visible')
    result['complete_crowd_visible'] = bool(visible and visible['min'] == count + 1)
    calls = result.get('Renderer3D.animation_rebuild.calls')
    characters = (count + 1) // 2 if workload == 'mixed' else count
    if visual_rate > 0 and workload != 'frozen':
        calls = result.get('Renderer3D.animation_accounted')
    result['playback_verified'] = bool(calls and
        (calls['max'] == 0 if workload == 'frozen' else calls['min'] == calls['max'] == characters))
    if workload == 'mixed':
        bodies = result.get('Physics3D.bodies')
        active = result.get('Physics3D.active_bodies')
        contacts = result.get('Physics3D.contact_pairs')
        result['collision_population_verified'] = bool(bodies and bodies['min'] == bodies['max'] == count + 1
            and active and active['min'] == active['max'] == count
            and contacts and contacts['max'] > count)
        result['valid_capture'] &= result['collision_population_verified']
    result['valid_capture'] &= result['complete_crowd_visible'] and result['playback_verified']


def qualify_skinning(result, count, mode):
    result['skinning'] = mode
    if mode == 'auto':
        return
    scope = 'Renderer3D.gpu_skinned_meshes' if mode == 'gpu' else 'Renderer3D.cpu_skinned_meshes'
    population = result.get(scope)
    result['skinning_verified'] = bool(population and population['min'] == population['max'] == count)
    result['valid_capture'] &= result['skinning_verified']

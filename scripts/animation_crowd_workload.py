"""Configure the existing animation example as a matched live/frozen crowd."""
import json
import math


WORKLOADS = ('animated', 'frozen')


def configure(project, settings, count, workload, seconds):
    if workload not in WORKLOADS or not 1 <= count <= 2000:
        raise ValueError('Crowd requires animated/frozen and 1..2000 characters')
    settings['main_scene'] = 'scene://animation_3d/crowd'
    scene_path = project / 'scenes/crowd.scene.json'
    scene = json.loads(scene_path.read_text())
    entities = {entity['id']: entity['components'] for entity in scene['entities']}
    entities['crowd']['LuaScript']['properties'] = {
        'count': count, 'frozen': workload == 'frozen', 'duration_seconds': seconds}
    # Match camera and geometry between live/frozen runs at each population.
    span = 2 * math.ceil(math.sqrt(count))
    position = [0, span * 1.2 + 5, span * 1.5 + 5]
    entities['camera']['Transform3D']['position'] = position
    entities['camera']['Camera3D']['target_offset'] = [-v for v in position]
    scene_path.write_text(json.dumps(scene, indent=2) + '\n')


def qualify(result, count, workload):
    visible = result.get('Renderer3D.meshes_visible')
    result['complete_crowd_visible'] = bool(visible and visible['min'] == count + 1)
    calls = result.get('Renderer3D.animation_rebuild.calls')
    result['playback_verified'] = bool(
        calls and (calls['min'] == count if workload == 'animated' else calls['max'] == 0))
    result['valid_capture'] &= result['complete_crowd_visible'] and result['playback_verified']


def qualify_skinning(result, count, mode):
    result['skinning'] = mode
    if mode == 'auto':
        return
    scope = 'Renderer3D.gpu_skinned_meshes' if mode == 'gpu' else 'Renderer3D.cpu_skinned_meshes'
    population = result.get(scope)
    result['skinning_verified'] = bool(population and population['min'] == population['max'] == count)
    result['valid_capture'] &= result['skinning_verified']

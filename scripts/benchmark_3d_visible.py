#!/usr/bin/env python3
"""Visible 3D timing runs: real deltas, fixed-duration workloads, per-frame evidence."""
import argparse
import csv
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile

import animation_crowd_workload as crowd


def distribution(values):
    if not values:
        return None
    values = sorted(values)
    return {'samples': len(values), 'p50_ms': values[math.ceil(.50 * len(values)) - 1],
            'p95_ms': values[math.ceil(.95 * len(values)) - 1],
            'p99_ms': values[math.ceil(.99 * len(values)) - 1], 'max_ms': values[-1]}


def summarize(path, warmup_seconds, width, height):
    frames = {}
    with path.open() as stream:
        for row in csv.DictReader(stream):
            frames.setdefault(int(row['frame']), {})[row['scope']] = row
    def metric(frame, scope, column='total_ms'):
        value = frame.get(scope, {}).get(column, '')
        return float(value) if value != '' else None
    elapsed = 0.0
    warm = []
    for _, frame in sorted(frames.items()):
        elapsed += (metric(frame, 'Frame.interval') or 0) / 1000
        if elapsed >= warmup_seconds:
            warm.append(frame)
    names = ['Frame.interval', 'Frame.update', 'Physics3D.step', 'Render.prepare_cpu',
             'Graphics.frame_advance', 'Graphics.render_thread', 'Graphics.wait_render',
             'Graphics.wait_submit', 'Graphics.gpu', 'AnimationStateMachine.update',
             'Renderer3D.animation_rebuild', 'Renderer3D.skin_cpu', 'Renderer3D.skin_upload_cpu']
    names += ['Renderer3D.animation_prepare_wall', 'Renderer3D.mesh_vertices_cpu',
              'Renderer3D.mesh_buffer_update_cpu', 'Renderer3D.skin_palette_cpu']
    names += ['Vulkan.memory_budget', 'Vulkan.acquire_image', 'Vulkan.present', 'Vulkan.wait_fence',
              'Vulkan.submit', 'Vulkan.command_alloc', 'Vulkan.acquire_total']
    names += ['Bgfx.api_frame', 'Bgfx.render_frame', 'Bgfx.flip', 'Bgfx.commands_pre',
              'Bgfx.commands_post', 'Bgfx.render_submit', 'Bgfx.sort', 'Bgfx.dedup_bind']
    names += ['Vulkan.uniform_flush', 'Vulkan.timer_end', 'Vulkan.timer_begin', 'Vulkan.staging_flush', 'Vulkan.command_kick']
    names += ['Vulkan.swapchain_create', 'Vulkan.swapchain_update', 'Vulkan.framebuffer_pre_reset']
    names += ['Vulkan.suboptimal_usable']
    result = {'frames': len(frames), 'measured_frames': len(warm), 'measured_wall_seconds': 0,
              'metrics': {name: distribution([value for frame in warm
                           if (value := metric(frame, name)) is not None]) for name in names}}
    result['measured_wall_seconds'] = sum(metric(f, 'Frame.interval') or 0 for f in warm) / 1000
    # A frame without a fixed step has zero physics work, not an unavailable
    # timing. GPU samples, in contrast, must never be filled in with zeroes.
    result['metrics']['Physics3D.step'] = distribution([metric(f, 'Physics3D.step') or 0 for f in warm])
    gpu_floor = metric(warm[0], 'Graphics.submitted_frame', 'gauge') if warm else None
    result['metrics']['Graphics.gpu'] = distribution([
        value for f in warm if (value := metric(f, 'Graphics.gpu')) is not None
        and (gpu_frame := metric(f, 'Graphics.gpu_frame', 'gauge')) is not None
        and (gpu_floor is None or gpu_frame >= gpu_floor)])
    for name, scope in [('dropped_fixed_ms', 'Simulation.dropped_fixed_ms'),
                        ('clamped_wall_ms', 'Simulation.clamped_wall_ms'),
                        ('simulated_ms', 'Simulation.advanced_ms'),
                        ('scaled_input_ms', 'Simulation.scaled_input_ms')]:
        result[name] = sum(metric(f, scope, 'gauge') or 0 for f in warm)
        result['all_frames_' + name] = sum(metric(f, scope, 'gauge') or 0 for f in frames.values())
    for name, scope in [('widths', 'Graphics.backbuffer_width'), ('heights', 'Graphics.backbuffer_height'),
                        ('vendor_ids', 'Graphics.vendor_id'), ('device_ids', 'Graphics.device_id')]:
        result[name] = sorted({metric(f, scope, 'gauge') for f in warm if metric(f, scope, 'gauge') is not None})
    result['minimized_frames'] = sum(metric(f, 'Window.minimized', 'gauge') == 1 for f in warm)
    result['unfocused_frames'] = sum(metric(f, 'Window.focused', 'gauge') == 0 for f in warm)
    result['overridden_delta_frames'] = sum(metric(f, 'Frame.delta_override', 'gauge') == 1 for f in warm)
    result['interrupted_frames'] = sum(metric(f, 'Window.capture_interrupted', 'gauge') == 1 for f in warm)
    # Cumulative per-world count includes startup and every catch-up step.
    # Missing telemetry is unknown, not evidence of a complete simulation.
    errors = [v for f in frames.values()
              if (v := metric(f, 'Physics3D.update_error_steps', 'gauge')) is not None]
    result['physics_update_error_steps'] = max(errors) if errors else None
    for scope in ['Simulation.fixed_steps', 'Renderer3D.meshes_visible', 'Renderer3D.meshes_culled',
                  'Renderer3D.directional_shader',
                  'Renderer3D.batches', 'Physics3D.active_bodies', 'Physics3D.bodies', 'Physics3D.contact_pairs',
                  'Renderer3D.animation_deferred',
                  'Renderer3D.animation_batch_vertices', 'Renderer3D.animation_batch_meshes',
                  'Renderer3D.animation_workers_available']:
        values = [v for f in warm if (v := metric(f, scope, 'gauge')) is not None]
        result[scope] = {'min': min(values), 'max': max(values), 'last': values[-1]} if values else None
    for scope in ['Renderer3D.gpu_skinned_meshes', 'Renderer3D.cpu_skinned_meshes']:
        values = [v for f in warm if (v := metric(f, scope, 'gauge')) is not None]
        result[scope] = {'min': min(values), 'max': max(values)} if values else None
    rebuilds = [metric(f, 'Renderer3D.animation_rebuild', 'calls') or 0 for f in warm]
    result['Renderer3D.animation_rebuild.calls'] = {
        'min': min(rebuilds), 'max': max(rebuilds)} if rebuilds else None
    accounted = [(metric(f, 'Renderer3D.animation_rebuild', 'calls') or 0) +
                 (metric(f, 'Renderer3D.animation_deferred', 'gauge') or 0) for f in warm]
    result['Renderer3D.animation_accounted'] = {'min': min(accounted), 'max': max(accounted)} if accounted else None
    result['valid_capture'] = bool(warm and result['widths'] == [width] and result['heights'] == [height]
                                   and result['metrics']['Graphics.gpu'] is not None
                                   and not result['minimized_frames'] and not result['overridden_delta_frames']
                                   and not result['interrupted_frames']
                                   and result['physics_update_error_steps'] == 0)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--counts', type=int, nargs='+', default=[250, 2000])
    parser.add_argument('--geometry', choices=['primitives', 'barrel'], default='primitives')
    parser.add_argument('--rig-layout', choices=['original', 'split'], default='original')
    parser.add_argument('--skinning', choices=['auto', 'cpu', 'gpu'], default='auto',
                        help='GPU requires every crowd character to use the GPU path')
    parser.add_argument('--workloads', choices=['mesh', 'rigid', 'pile', *crowd.WORKLOADS], nargs='+', default=['rigid', 'pile'])
    parser.add_argument('--vsync', choices=['on', 'off'], nargs='+', default=['on', 'off'])
    parser.add_argument('--seconds', type=float, default=12)
    parser.add_argument('--warmup-seconds', type=float, default=2)
    parser.add_argument('--repeats', type=int, default=1)
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--visual-rate', type=float, default=0, help='Far visual poses per second; zero preserves full rate')
    parser.add_argument('--visual-distance', type=float, default=30, help='Nearer poses remain full rate (world units)')
    args = parser.parse_args()
    if not (0 <= args.visual_rate <= 240) or not (0 <= args.visual_distance < float('inf')):
        parser.error('Visual rate must be 0..240 and distance finite/nonnegative')
    if not (0 <= args.warmup_seconds < args.seconds <= 600) or args.repeats < 1:
        parser.error('Require 0 <= warmup < seconds <= 600 and positive repeats')
    if any(n < 1 or n > 5000 for n in args.counts) or not (1 <= args.width <= 65535 and 1 <= args.height <= 65535):
        parser.error('Counts must be 1..5000 and dimensions 1..65535')
    if any(w in crowd.WORKLOADS for w in args.workloads) and (max(args.counts) > 2000 or args.geometry != 'primitives'):
        parser.error('Character workloads require counts <= 2000 and no barrel geometry override')
    if args.rig_layout != 'original' and any(w not in crowd.WORKLOADS for w in args.workloads):
        parser.error('Split rig layout requires character workloads')
    binary = args.binary.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ)
    for name in ['DEMI_HEADLESS', 'DEMI_FIXED_DELTA_SECONDS']:
        environment.pop(name, None)
    environment['DEMI_PROFILE_SLOW_MS'] = '1000000'
    environment['DEMI_GPU_SKINNING'] = '0' if args.skinning == 'cpu' else '1'
    with binary.open('rb') as source:
        digest = hashlib.file_digest(source, 'sha256').hexdigest()
    (output / 'machine.json').write_text(json.dumps({
        'format_version': 1, 'platform': platform.platform(), 'binary': str(binary),
        'binary_sha256': digest, 'seconds': args.seconds, 'warmup_seconds': args.warmup_seconds,
        'requested_pixels': [args.width, args.height],
        'workloads': args.workloads, 'counts': args.counts, 'repeats': args.repeats,
        'sdl_video_driver': environment.get('SDL_VIDEO_DRIVER', 'automatic'),
        'geometry': args.geometry,
        'skinning': args.skinning,
        'rig_layout': args.rig_layout,
        'visual_rate': args.visual_rate, 'visual_distance': args.visual_distance,
        'cpu': subprocess.run(['lscpu'], capture_output=True, text=True, check=True).stdout,
    }, indent=2) + '\n')
    for count in args.counts:
        for workload in args.workloads:
            for vsync in args.vsync:
                for repeat in range(1, args.repeats + 1):
                    name = f'{workload}-{count}-{vsync}-{repeat}'
                    with tempfile.TemporaryDirectory(prefix='demi-visible-lab-') as temporary:
                        project = Path(temporary) / 'project'
                        is_crowd = workload in crowd.WORKLOADS
                        example = 'animation_3d' if is_crowd else 'performance_3d_lab'
                        shutil.copytree(root / 'examples' / example, project,
                                        ignore=shutil.ignore_patterns('generated', 'build'))
                        project_path = project / 'demi.project.json'
                        settings = json.loads(project_path.read_text())
                        settings['display'] = {'vsync': vsync == 'on'}
                        if is_crowd:
                            crowd.configure(project, settings, count, workload, args.seconds, args.rig_layout,
                                            args.visual_rate, args.visual_distance)
                        else:
                            scene_path = project / 'scenes/main.scene.json'
                            scene = json.loads(scene_path.read_text())
                            lab = next(entity for entity in scene['entities'] if entity['id'] == 'lab')
                            lab['components']['LuaScript']['properties'] = {'count': count, 'workload': workload,
                                'varied': False, 'geometry': args.geometry, 'duration_seconds': args.seconds}
                            scene_path.write_text(json.dumps(scene, indent=2) + '\n')
                        project_path.write_text(json.dumps(settings, indent=2) + '\n')
                        trace = output / f'{name}.frames.csv'
                        with (output / f'{name}.log').open('w') as log:
                            if is_crowd and args.rig_layout == 'split':
                                subprocess.run([str(binary), 'asset', 'reimport',
                                    str(project / 'assets/AnimationLib/ual1_standard.asset.json')],
                                    stdout=log, stderr=subprocess.STDOUT, timeout=60, check=True)
                            subprocess.run([str(binary), 'validate', str(project)],
                                stdout=log, stderr=subprocess.STDOUT, timeout=60, check=True)
                            subprocess.run([str(binary), 'run', '--project', str(project_path),
                                '--window-size', f'{args.width}x{args.height}', '--max-frames', '120000',
                                '--profile-report', str(output / f'{name}.csv'), '--profile-frames', str(trace)],
                                env=environment, stdout=log, stderr=subprocess.STDOUT,
                                timeout=max(60, args.seconds * 4), check=True)
                        result = summarize(trace, args.warmup_seconds, args.width, args.height)
                        result['completed_duration'] = result['measured_wall_seconds'] >= (args.seconds - args.warmup_seconds) * .9
                        result['valid_capture'] &= result['completed_duration']
                        if is_crowd:
                            crowd.qualify(result, count, workload, args.visual_rate)
                            crowd.qualify_skinning(result, (count + 1) // 2 if workload == 'mixed' else count, args.skinning)
                        result.update(workload=workload, count=count, geometry='ual1_standard' if is_crowd else args.geometry, vsync=vsync, repeat=repeat)
                        (output / f'{name}.json').write_text(json.dumps(result, indent=2) + '\n')
                        print(name + ': ' + json.dumps(result['metrics']) + f' valid={result["valid_capture"]} dropped_ms={result["dropped_fixed_ms"]:.3f}', flush=True)


if __name__ == '__main__':
    main()

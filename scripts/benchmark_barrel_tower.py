#!/usr/bin/env python3
"""Visible barrel-tower impact qualification using the real UI/test harness."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from benchmark_3d_visible import summarize


def phase_frames(trace, body_count):
    with trace.open() as stream:
        reader = csv.DictReader(stream)
        fields = reader.fieldnames
        rows = list(reader)
    shot = next((int(row['frame']) for row in rows
                 if row['scope'] == 'Physics3D.bodies'
                 and float(row['gauge']) >= body_count + 2), None)
    return fields, rows, shot


def impact_burst_end(rows, shot, body_count):
    if shot is None:
        return None
    active = [int(row['frame']) for row in rows if row['scope'] == 'Physics3D.active_bodies'
              and int(row['frame']) >= shot and float(row['gauge']) >= body_count / 2]
    # Poll-to-poll interval includes the preceding frame's work.
    return max(active) + 1 if active else None


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--columns', type=int, default=8)
    parser.add_argument('--levels', type=int, default=16)
    parser.add_argument('--velocity-steps', type=int, default=64)
    parser.add_argument('--position-steps', type=int, default=16)
    parser.add_argument('--settle-seconds', type=float, default=12)
    parser.add_argument('--after-shot-seconds', type=float, default=12)
    parser.add_argument('--warmup-seconds', type=float, default=2)
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--width', type=int, default=1920)
    parser.add_argument('--height', type=int, default=1080)
    parser.add_argument('--window-width', type=int)
    parser.add_argument('--window-height', type=int)
    args = parser.parse_args()
    if not (2 <= args.columns <= 8 and 2 <= args.levels <= 64 and args.repeats > 0
            and 0 <= args.velocity_steps <= 128 and 0 <= args.position_steps <= 128
            and 0 <= args.warmup_seconds < args.settle_seconds <= 60
            and 2 <= args.after_shot_seconds <= 60):
        parser.error('Invalid population, solver quality, or duration')
    window = [args.width if args.window_width is None else args.window_width,
              args.height if args.window_height is None else args.window_height]
    if any(n < 1 or n > 65535 for n in [*window, args.width, args.height]):
        parser.error('Dimensions must be 1..65535')
    binary = args.binary.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    environment = dict(os.environ)
    for name in ['DEMI_HEADLESS', 'DEMI_FIXED_DELTA_SECONDS']:
        environment.pop(name, None)
    environment['DEMI_PROFILE_SLOW_MS'] = '1000000'
    count = args.columns * args.columns * args.levels
    with binary.open('rb') as stream:
        digest = hashlib.file_digest(stream, 'sha256').hexdigest()
    (output / 'machine.json').write_text(json.dumps({
        'format_version': 1, 'binary': str(binary), 'binary_sha256': digest,
        'count': count, 'columns': args.columns, 'levels': args.levels,
        'solver_velocity_steps': args.velocity_steps, 'solver_position_steps': args.position_steps,
        'settle_seconds': args.settle_seconds, 'after_shot_seconds': args.after_shot_seconds,
        'warmup_seconds': args.warmup_seconds, 'window_request': window,
        'expected_pixels': [args.width, args.height],
        'VK_DRIVER_FILES': environment.get('VK_DRIVER_FILES'),
    }, indent=2) + '\n')
    test = '''return { tests = {{ name = "tower impact qualification", func = function()
      Test.wait(0.1)
      Hud.set_text("tower_help", "AUTOMATED TEST - please do not interact")
      Test.wait(SETTLE)
      local tops = Entity.query({ tags = { "tower_top" } })
      Test.expect(#Entity.query({ tags = { "tower_barrel" } }) == BARREL_COUNT, "All barrels must exist")
      Test.expect(#tops == TOP_COUNT, "All top barrels must exist")
      for _, id in ipairs(tops) do
        local _, y = Transform3D.get_position(id)
        Test.expect(y and y > TOP_HEIGHT - 0.5, "Tower fell before projectile impact")
      end
      Test.touch("tower_fire")
      Test.wait(0.2)
      Test.expect(Entity.exists("tower_projectile_1"), "Projectile must spawn")
      Test.wait(AFTER)
      local fallen = 0
      for _, id in ipairs(tops) do
        local _, y = Transform3D.get_position(id)
        if y and y < TOP_HEIGHT - 2 then fallen = fallen + 1 end
      end
      print("TOWER_FALLEN " .. fallen)
      Test.expect(fallen > 0, "Impact must make upper barrels fall")
    end }} }
'''.replace('SETTLE', str(args.settle_seconds)).replace('AFTER', str(args.after_shot_seconds))
    test = test.replace('TOP_COUNT', str(args.columns ** 2)).replace(
        'TOP_HEIGHT', str(-.025 + (args.levels - 1) * .95))
    test = test.replace('BARREL_COUNT', str(count))
    all_passed = True
    for repeat in range(1, args.repeats + 1):
        name = f'tower-{count}-{repeat}'
        trace = output / f'{name}.frames.csv'
        with tempfile.TemporaryDirectory(prefix='demi-tower-benchmark-') as temporary:
            project = Path(temporary) / 'project'
            shutil.copytree(root / 'examples/performance_3d_lab', project,
                            ignore=shutil.ignore_patterns('build'))
            project_path = project / 'demi.project.json'
            settings = json.loads(project_path.read_text())
            settings['main_scene'] = 'scene://performance_3d_lab/tower'
            settings['display'] = {'vsync': True}
            project_path.write_text(json.dumps(settings, indent=2) + '\n')
            scene_path = project / 'scenes/tower.scene.json'
            scene = json.loads(scene_path.read_text())
            tower = next(e for e in scene['entities'] if e['id'] == 'tower')
            tower['components']['LuaScript']['properties'].update(
                columns=args.columns, levels=args.levels,
                solver_velocity_steps=args.velocity_steps, solver_position_steps=args.position_steps)
            scene_path.write_text(json.dumps(scene, indent=2) + '\n')
            (project / 'scripts/tests/e2e.lua').write_text(test)
            with (output / f'{name}.log').open('w') as log:
                result = subprocess.run([str(binary), 'run', '--project', str(project_path),
                    '--e2e-tests', '--max-frames', '120000', '--window-size', f'{window[0]}x{window[1]}',
                    '--profile-report', str(output / f'{name}.csv'), '--profile-frames', str(trace)],
                    env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=180)
        log_text = (output / f'{name}.log').read_text()
        if not trace.is_file() or trace.stat().st_size == 0:
            raise RuntimeError(f'No frame trace was produced; see {output / (name + ".log")}')
        scenario_passed = result.returncode == 0 and '[test] SUMMARY passed=1 failed=0' in log_text
        fields, rows, shot = phase_frames(trace, count)
        burst_end = impact_burst_end(rows, shot, count)
        phases = {}
        for phase in ['standing', 'impact', 'active_burst']:
            if shot is None and phase == 'impact':
                continue
            if phase == 'active_burst':
                if burst_end is None:
                    continue
                selected = [row for row in rows if shot <= int(row['frame']) <= burst_end]
            else:
                selected = [row for row in rows if shot is None or
                            (int(row['frame']) < shot) == (phase == 'standing')]
            path = output / f'{name}.{phase}.frames.csv'
            with path.open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                writer.writerows(selected)
            summary = summarize(path, args.warmup_seconds if phase == 'standing' else 0,
                                args.width, args.height)
            summary['budget_passed'] = bool(summary['valid_capture'] and summary['metrics']['Frame.interval']
                and summary['metrics']['Frame.interval']['p95_ms'] <= 16.7
                and summary['metrics']['Frame.interval']['p99_ms'] <= 25
                and summary['dropped_fixed_ms'] < .001 and summary['clamped_wall_ms'] < .001
                and summary['unfocused_frames'] == 0)
            if phase == 'standing':
                summary['budget_passed'] &= bool(summary['Renderer3D.meshes_visible'] and
                    summary['Renderer3D.meshes_visible']['min'] == count + 1)
            if phase == 'active_burst':
                summary['budget_passed'] &= bool(summary['Physics3D.active_bodies'] and
                    summary['Physics3D.active_bodies']['max'] >= count and
                    summary['measured_wall_seconds'] >= 1)
            phases[phase] = summary
        fallen = re.search(r'TOWER_FALLEN (\d+)', log_text)
        passed = scenario_passed and len(phases) == 3 and all(p['budget_passed'] for p in phases.values())
        report = {'format_version': 1, 'scenario_passed': scenario_passed, 'budget_passed': passed,
                  'shot_frame': shot, 'fallen_top_barrels': int(fallen[1]) if fallen else None,
                  'phases': phases}
        (output / f'{name}.json').write_text(json.dumps(report, indent=2) + '\n')
        print(name, 'scenario=', scenario_passed, 'budget=', passed,
              {k: (v['metrics']['Frame.interval'], v['dropped_fixed_ms']) for k, v in phases.items()}, flush=True)
        all_passed &= passed
    return 0 if all_passed else 1


if __name__ == '__main__':
    raise SystemExit(main())

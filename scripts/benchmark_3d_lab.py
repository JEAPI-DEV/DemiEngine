#!/usr/bin/env python3
"""Run isolated headless lab variants; never modify the checked-in example."""
import argparse
import csv
import hashlib
import json
import os
from pathlib import Path
import platform
import shutil
import subprocess
import tempfile
import time


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True, help='New report directory')
    parser.add_argument('--counts', type=int, nargs='+', default=[250, 500, 1000, 2000])
    parser.add_argument('--workloads', choices=['mesh', 'rigid', 'pile'], nargs='+', default=['mesh', 'rigid', 'pile'])
    parser.add_argument('--frames', type=int, default=720, help='Last 600 scope calls form the percentile window')
    parser.add_argument('--repeats', type=int, default=3)
    parser.add_argument('--varied', action='store_true')
    parser.add_argument('--geometry', choices=['primitives', 'barrel'], default='primitives')
    args = parser.parse_args()
    if args.frames < 1 or args.repeats < 1 or any(n < 1 or n > 5000 for n in args.counts):
        parser.error('Frames/repeats must be positive and counts between 1 and 5000')
    binary = args.binary.resolve(strict=True)
    root = Path(__file__).resolve().parents[1]
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    cache = binary.parent / 'CMakeCache.txt'
    build_type = 'unknown'
    if cache.is_file():
        for line in cache.read_text().splitlines():
            if line.startswith('CMAKE_BUILD_TYPE:STRING='):
                build_type = line.split('=', 1)[1]
    with binary.open('rb') as executable:
        executable_hash = hashlib.file_digest(executable, 'sha256').hexdigest()
    metadata = {
        'format_version': 1, 'platform': platform.platform(), 'cpu_count': os.cpu_count(),
        'build_type': build_type, 'binary': str(binary),
        'binary_sha256': executable_hash,
        'headless': True, 'gpu_measured': False, 'frames': args.frames,
        'repeats': args.repeats, 'varied': args.varied, 'geometry': args.geometry,
        'fixed_timestep': 0.016666667, 'percentile_window_calls': 600,
        'cpu_info': subprocess.run(['lscpu'], capture_output=True, text=True, check=True).stdout,
    }
    (output / 'machine.json').write_text(json.dumps(metadata, indent=2) + '\n')
    with (output / 'summary.csv').open('w', newline='') as summary:
        writer = csv.writer(summary)
        writer.writerow(['workload', 'count', 'repeat', 'process_seconds', 'frame_p50_ms', 'frame_p95_ms', 'frame_p99_ms', 'frame_max_ms', 'active_bodies'])
        for workload in args.workloads:
            for count in args.counts:
                for repeat in range(1, args.repeats + 1):
                    name = f'{workload}-{count}-{repeat}'
                    with tempfile.TemporaryDirectory(prefix='demi-perf-lab-') as temporary:
                        project = Path(temporary) / 'project'
                        shutil.copytree(root / 'examples/performance_3d_lab', project)
                        scene_path = project / 'scenes/main.scene.json'
                        scene = json.loads(scene_path.read_text())
                        lab = next(entity for entity in scene['entities'] if entity['id'] == 'lab')
                        lab['components']['LuaScript']['properties'] = {'count': count, 'workload': workload, 'varied': args.varied, 'geometry': args.geometry}
                        scene_path.write_text(json.dumps(scene, indent=2) + '\n')
                        report = output / f'{name}.csv'
                        start = time.perf_counter()
                        with (output / f'{name}.log').open('w') as log:
                            subprocess.run([str(binary), 'run', '--project', str(project / 'demi.project.json'),
                                            '--max-frames', str(args.frames), '--profile-report', str(report)],
                                           env={**os.environ, 'DEMI_HEADLESS': '1'}, stdout=log, stderr=subprocess.STDOUT,
                                           timeout=300, check=True)
                        elapsed = time.perf_counter() - start
                        with report.open() as stream:
                            next(stream)  # Human-readable title precedes CSV header.
                            scopes = {row['scope']: row for row in csv.DictReader(stream)}
                        frame = scopes['Frame.total']
                        writer.writerow([workload, count, repeat, round(elapsed, 3),
                                         frame['p50_ms'], frame['p95_ms'], frame['p99_ms'], frame['max_ms'],
                                         scopes.get('Physics3D.active_bodies', {}).get('gauge', '')])
                        summary.flush()
                        print(f'{name}: CPU frame p95={frame["p95_ms"]} ms (headless, {build_type})', flush=True)


if __name__ == '__main__':
    main()

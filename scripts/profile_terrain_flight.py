#!/usr/bin/env python3
"""Profile the Alpine example on a repeatable route through resident vegetation.

Runs a maximized Release editor with a private project copy and user-state copy.
The source project, saved camera and user's dock layout are never modified.
Requires the X11 capture dependencies documented by capture_editor_window.py.
"""
import argparse
import csv
import statistics
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile

from capture_editor_window import (
    checked_command, settle_editor, stop_editor, verify_editor_window,
    wait_for_window,
)


def summarize_capture(path):
    frames = {}
    with path.open() as capture:
        for row in csv.DictReader(capture):
            if row.get('calls') is None:
                continue
            frame = frames.setdefault(int(row['frame']), {})
            frame[row['scope']] = float(row['gauge'] or row['total_ms'])
    phases = {}
    view_sizes = set()
    for phase in ('opening', 'outbound', 'settled', 'return', 'returned'):
        samples = [frame for frame in frames.values()
                   if 'Bench.flight.' + phase in frame and 'GPU.game_view' in frame]
        if not samples:
            raise RuntimeError('Missing rendered phase: ' + phase)
        view_sizes.update((frame.get('Renderer3D.viewport_width', 0),
                           frame.get('Renderer3D.viewport_height', 0))
                          for frame in samples)
        def distribution(values):
            ordered = sorted(values)
            return {'median': statistics.median(ordered),
                    'p95': ordered[int((len(ordered) - 1) * .95)],
                    'maximum': ordered[-1]}
        phases[phase] = {
            'frames': len(samples),
            'gpu_ms': distribution([frame['GPU.game_view'] for frame in samples]),
            'editor_frame_ms': distribution([frame.get('Editor.frame', 0) for frame in samples]),
            'view_size': [statistics.median([frame.get('Renderer3D.viewport_width', 0)
                                             for frame in samples]),
                          statistics.median([frame.get('Renderer3D.viewport_height', 0)
                                             for frame in samples])],
            'game_cpu_ms': distribution([frame.get('Frame.update', 0) +
                                         frame.get('Render.submit', 0) for frame in samples]),
            'instance_candidates': distribution([
                max((value for name, value in frame.items()
                     if name.endswith('.instances.copies_examined')), default=0)
                for frame in samples]),
            'vegetation_cells': distribution([
                frame.get('Terrain.region.vegetation_cells', 0) for frame in samples]),
        }
    if len(view_sizes) != 1 or any(width <= 0 or height <= 0
                                   for width, height in view_sizes):
        raise RuntimeError('Game View size changed during flight; discard this capture')
    if (phases['outbound']['vegetation_cells']['median'] <= 0 or
            phases['outbound']['instance_candidates']['median'] <= 0):
        raise RuntimeError('Flight did not retain vegetation; discard this capture')
    return phases


def main():
    repository = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=repository / 'build/linux-release/demi-editor')
    parser.add_argument('--output', type=Path, required=True,
                        help='Output stem for .csv, .png, .log and .json artifacts')
    parser.add_argument('--seconds', type=int, default=130)
    parser.add_argument('--layout', type=Path,
                        help='Frozen workspace directory from the baseline capture')
    args = parser.parse_args()
    if args.seconds < 100:
        parser.error('Allow at least 100 seconds for preparation and the complete route')
    binary = args.binary.resolve()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    original_data = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    with tempfile.TemporaryDirectory(prefix='demi-ground-flight-') as scratch:
        scratch = Path(scratch)
        project = scratch / 'project'
        subprocess.run(['cp', '-a', '--reflink=auto',
                        str(repository / 'examples/terrain_3d'), str(project)], check=True)
        project_file = project / 'demi.project.json'
        document = json.loads(project_file.read_text())
        document['package_registry'] = str(repository / 'packages')
        project_file.write_text(json.dumps(document, indent=2) + '\n')
        shutil.copyfile(repository / 'scripts/probes/terrain_ground_flight.lua',
                        project / 'scripts/alpine_flight.lua')
        env = os.environ.copy()
        env['SDL_VIDEODRIVER'] = 'x11'
        for key in ('XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME'):
            directory = scratch / key
            directory.mkdir()
            env[key] = str(directory)
        layout = args.layout or original_data / 'demi-editor/workspace'
        if layout.exists():
            frozen_layout = output.parent / (output.name + '-workspace')
            shutil.copytree(layout, frozen_layout, dirs_exist_ok=True)
            shutil.copytree(frozen_layout, Path(env['XDG_DATA_HOME']) / 'demi-editor/workspace')
        with output.with_suffix('.log').open('w') as log:
            process = subprocess.Popen([
                str(binary), '--project', str(project), '--play',
                '--profile-frames', str(output.with_suffix('.csv')),
                '--max-seconds', str(args.seconds),
            ], env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                window = wait_for_window(process, binary, 60)
                checked_command(['xdotool', 'windowactivate', '--sync', window])
                display = checked_command(['xrandr', '--current'])
                primary = re.search(r'connected primary (\d+)x(\d+)\+(\d+)\+(\d+)', display)
                if primary:
                    checked_command(['xdotool', 'windowmove', window,
                                     str(int(primary[3]) + 100), str(int(primary[4]) + 100)])
                for state in ('MAXIMIZED_VERT', 'MAXIMIZED_HORZ'):
                    checked_command(['xdotool', 'windowstate', '--add', state, window])
                geometry = checked_command(['xdotool', 'getwindowgeometry', window])
                print(geometry, flush=True)
                settle_editor(process, min(80, args.seconds - 15))
                verify_editor_window(window, process, binary)
                checked_command(['import', '-window', window, str(output.with_suffix('.png'))])
                process.wait(timeout=args.seconds + 30)
                if process.returncode:
                    raise RuntimeError('Editor exited unsuccessfully; inspect the log')
                output.with_suffix('.json').write_text(json.dumps({
                    'binary': str(binary), 'configuration': 'Release (verify binary build)',
                    'window': geometry, 'eye_height_metres': 3,
                    'layout_source': str(layout),
                    'route_end_xz': [1200, 800], 'seconds': args.seconds,
                    'phase_scopes': 'Bench.flight.{opening,outbound,settled,return,returned}',
                    'phases': summarize_capture(output.with_suffix('.csv')),
                }, indent=2) + '\n')
            finally:
                stop_editor(process)


if __name__ == '__main__':
    main()

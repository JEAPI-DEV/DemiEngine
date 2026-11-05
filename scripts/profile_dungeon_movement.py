"""Compare stationary/moving Dungeon CPU scopes in an isolated Release project."""
import argparse
import collections
import csv
import json
import os
import re
from pathlib import Path
import shutil
import statistics
import subprocess
import tempfile
import time


def summarize(path):
    frames = collections.defaultdict(dict)
    with path.open() as source:
        for row in csv.DictReader(source):
            frames[int(row['frame'])][row['scope']] = float(row['total_ms'])
    result = {}
    for phase in ('stationary', 'moving', 'settled'):
        samples = [frame for frame in frames.values() if 'Bench.dungeon.' + phase in frame]
        if not samples:
            raise RuntimeError('Missing probe phase: ' + phase)
        scopes = ('Frame.update', 'Render.submit', 'Render.prepare_cpu', 'Frame.fixed_update',
                  'Visibility.fog2d', 'Visibility.rays2d', 'Visibility.mask2d', 'Visibility.publish2d',
                  'Lua.on_update', 'Lua.on_fixed_update', 'GPU.game_view',
                  'HotReload.poll', 'Frame.interval', 'Render.total', 'Bench.navigation_driver',
                  'Graphics.gpu', 'Graphics.gpu_view.0.2D')
        summary = {'frames': len(samples)}
        active = [frame for frame in samples if frame.get('Visibility.fog2d', 0) > 0]
        if active:
            values = sorted(frame.get('Frame.update', 0) + frame.get('Render.submit', frame.get('Render.prepare_cpu', 0)) for frame in active)
            summary['visibility_update_frames'] = {'count': len(active), 'cpu_median_ms': statistics.median(values), 'cpu_p95_ms': values[int((len(values) - 1) * .95)]}
        for scope in ('Game CPU', 'CPU including hot reload') + scopes:
            values = []
            for frame in samples:
                if scope in ('Game CPU', 'CPU including hot reload'):
                    value = frame.get('Frame.update', 0) + frame.get('Render.submit', frame.get('Render.prepare_cpu', 0))
                    if scope == 'CPU including hot reload':
                        value += frame.get('HotReload.poll', 0)
                else:
                    value = frame.get(scope, 0)
                values.append(value)
            ordered = sorted(values)
            summary[scope] = {'median_ms': statistics.median(ordered),
                              'p95_ms': ordered[int((len(ordered) - 1) * .95)],
                              'p99_ms': ordered[int((len(ordered) - 1) * .99)],
                              'max_ms': max(ordered)}
        result[phase] = summary
    return result


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path)
    parser.add_argument('--editor', action='store_true')
    parser.add_argument('--explore', action='store_true', help='Traverse rooms for 60 seconds with a bounded engine driver')
    parser.add_argument('--dev', action='store_true', help='Include development-mode hot reload')
    parser.add_argument('--window-size', help='Optional fixed size; otherwise maximized windowed')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    if args.dev and args.editor:
        parser.error('--dev profiles the standalone runtime; use --editor separately')
    if args.binary is None:
        args.binary = repo / 'build/linux-release' / ('demi-editor' if args.editor else 'demi')
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='demi-dungeon-profile-') as temp:
        scratch = Path(temp)
        project = scratch / 'project'
        shutil.copytree(repo / 'examples/Dungeon', project, ignore=shutil.ignore_patterns('build'))
        settings_path = project / 'demi.project.json'
        settings = json.loads(settings_path.read_text())
        build = settings.setdefault('build', {})
        build.setdefault('application_id', 'dev.demiengine.dungeonprofile')
        build.setdefault('window', {})['mode'] = 'windowed'
        if not args.window_size:
            build['window'].update(width=3840, height=2022)
        settings_path.write_text(json.dumps(settings, indent=2) + '\n')
        probe = 'dungeon_exploration.lua' if args.explore else 'dungeon_movement.lua'
        shutil.copyfile(repo / 'scripts/probes' / probe, project / 'scripts/movement_probe.lua')
        scene = project / 'scenes/floor1.scene.json'
        document = json.loads(scene.read_text())
        document['entities'].append({'id': 'movement_probe', 'components': {
            'LuaScript': {'module': 'script://scripts/movement_probe.lua'}}})
        scene.write_text(json.dumps(document, indent=2) + '\n')
        env = os.environ.copy()
        env['SDL_VIDEODRIVER'] = 'x11'
        for key in ('XDG_DATA_HOME', 'XDG_CONFIG_HOME', 'XDG_CACHE_HOME'):
            env[key] = str(scratch / key)
            Path(env[key]).mkdir()
        command = [str(args.binary.resolve())]
        command += ['--play', '--max-seconds', '90' if args.explore else '60'] if args.editor else ['dev' if args.dev else 'run']
        if args.window_size and not args.editor:
            command += ['--window-size', args.window_size]
        command += ['--project', str(project), '--profile-frames', str(output.with_suffix('.csv'))]
        with output.with_suffix('.log').open('w') as log:
            process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT)
            try:
                if not args.window_size and not args.editor:
                    deadline = time.monotonic() + 15
                    while process.poll() is None and time.monotonic() < deadline:
                        windows = subprocess.run(['xdotool', 'search', '--onlyvisible', '--pid', str(process.pid)],
                                                 capture_output=True, text=True).stdout.split()
                        if windows:
                            for state in ('MAXIMIZED_VERT', 'MAXIMIZED_HORZ'):
                                subprocess.run(['xdotool', 'windowstate', '--add', state, windows[-1]],
                                               check=True, capture_output=True)
                            break
                        time.sleep(.05)
                if process.wait(timeout=90) != 0:
                    raise RuntimeError('Dungeon probe failed; inspect ' + str(output.with_suffix('.log')))
            finally:
                if process.poll() is None:
                    process.terminate()
                    process.wait(timeout=5)
        initialization = re.search(r'backend ([^,]+), (\d+)x(\d+)', output.with_suffix('.log').read_text())
        viewport = re.search(r'BENCH viewport=(\d+)x(\d+)', output.with_suffix('.log').read_text())
        report = {'binary': str(args.binary.resolve()), 'editor': args.editor, 'dev': args.dev, 'explore': args.explore,
                  'window': list(map(int, viewport.groups())) if viewport else None,
                  'backend': initialization.group(1) if initialization else None,
                  'phases': summarize(output.with_suffix('.csv'))}
        output.with_suffix('.json').write_text(json.dumps(report, indent=2) + '\n')
        print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()

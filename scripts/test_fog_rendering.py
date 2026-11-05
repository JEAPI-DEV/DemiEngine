"""Pixel assertions against the real GPU fog shader in a maximized window.

Run on an X11 desktop. Requires
ImageMagick import, xdotool and Pillow; generated captures stay under --output.
"""
import argparse
import json
import math
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import time
from PIL import Image


def main():
    repo = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--binary', type=Path, default=repo / 'build/linux-release/demi')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='demi-fog-render-') as temp:
        project = Path(temp)
        (project / 'scenes').mkdir()
        (project / 'scripts').mkdir()
        shutil.copyfile(repo / 'scripts/probes/fog_render.lua', project / 'scripts/probe.lua')
        (project / 'demi.project.json').write_text(json.dumps({
            'format_version': 1, 'name': 'Fog GPU regression',
            'build': {'application_id': 'dev.demi.fogprobe',
                      'window': {'width': 1280, 'height': 720, 'mode': 'windowed'}},
            'main_scene': 'scene://fog/main', 'scenes': [{'id': 'scene://fog/main'}]}))
        (project / 'scenes/main.scene.json').write_text(json.dumps({
            'format_version': 1, 'id': 'scene://fog/main', 'entities': [
                {'id': 'camera', 'components': {'Transform2D': {}, 'Camera2D': {
                    'orthographic_size': 12, 'clear_color': [1, 1, 1, 1]}}},
                {'id': 'wall', 'components': {'Transform2D': {'position': [1, 3]},
                    'BoxCollider2D': {'size': [2, 2], 'layer': 'wall', 'debug_visible': False}}},
                *({'id': f'corridor_{index}', 'enabled': False, 'components': {
                    'Transform2D': {'position': center},
                    'BoxCollider2D': {'size': [1, 1], 'layer': 'wall', 'debug_visible': False}}}
                  for index, center in enumerate([[2, -.5], [3, -.5], [4, -.5],
                                                  [4, .5], [4, 1.5], [4, 2.5]])),
                *({'id': actor, 'components': {
                    'Transform2D': {'position': position},
                    'BoxCollider2D': {'size': [0.6, 0.6], 'layer': 'actor', 'debug_visible': False}}}
                  for actor, position in [('player', [0, 0]), ('enemy', [2, 0])]),
                {'id': 'probe', 'components': {'LuaScript': {'module': 'script://scripts/probe.lua'}}}
            ]}))
        env = os.environ.copy()
        env['SDL_VIDEODRIVER'] = 'x11'
        process = subprocess.Popen(['stdbuf', '-oL', str(args.binary.resolve()), 'run',
            '--project', str(project), '--max-frames', '600'],
            stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, env=env)
        # Window management only; all gameplay/control input stays in Lua.
        deadline = time.monotonic() + 15
        window = None
        while process.poll() is None and time.monotonic() < deadline:
            windows = subprocess.run(['xdotool', 'search', '--pid', str(process.pid),
                '--name', 'Fog GPU regression'], capture_output=True, text=True).stdout.split()
            if windows:
                window = windows[-1]
                subprocess.run(['xdotool', 'windowactivate', '--sync', window],
                               check=True, capture_output=True)
                for state in ('MAXIMIZED_VERT', 'MAXIMIZED_HORZ'):
                    subprocess.run(['xdotool', 'windowstate', '--add', state, window],
                                   check=True, capture_output=True)
                break
            time.sleep(.05)
        if window is None:
            process.terminate()
            raise RuntimeError('Cannot locate the probe window')
        with (output / 'runtime.log').open('w') as log:
            for line in process.stdout:
                log.write(line)
                if 'FOG_CAPTURE_' in line:
                    state = line.split('FOG_CAPTURE_', 1)[1].strip().lower()
                    subprocess.run(['import', '-window', window, str(output / (state + '.png'))], check=True)
            if process.wait(timeout=30):
                raise RuntimeError('Renderer failed; inspect runtime.log')
    closed = Image.open(output / 'closed.png').convert('RGB')
    opened = Image.open(output / 'open.png').convert('RGB')
    memory = Image.open(output / 'memory.png').convert('RGB')
    shifted = Image.open(output / 'shifted.png').convert('RGB')
    corridor = Image.open(output / 'corridor.png').convert('RGB')
    assert closed.size == opened.size == memory.size == corridor.size == shifted.size, 'Drawable changed size'
    def sample(image, x, y):
        width, height = image.size
        return image.getpixel((int(width / 2 + x * height / 24),
                               int(height / 2 - y * height / 24)))[0]
    assert sample(closed, 5.5, 5) > 245, 'Visible side of pillar shadow is dark'
    assert sample(closed, 4.5, 5) < 10, 'Pillar shadow leaks light'
    assert sample(closed, 1, 1.8) > 245, 'Wall halo returned'
    assert 105 < sample(closed, 10.3, 0) < 150, 'Range falloff changed'
    assert 110 < sample(closed, 1.5, 6.5) < 170, 'Discovery memory is missing'
    assert sample(closed, 2.5, 6.5) < 10, 'Memory fixture was already revealed'
    assert 110 < sample(memory, 2.5, 6.5) < 170, 'Discovery update required rebuilding sight geometry'
    assert sample(opened, 4.5, 5) > 245, 'Disabled blocker still shadows'
    # An empty view must be bright in every direction, including both sides
    # of the polygon's zero-angle seam. Sparse pillar checks missed a wedge.
    for image, focus in ((opened, (0, 0)), (shifted, (0.37, -0.19))):
        for radius in (0.5, 2, 5, 8):
            for step in range(1440):
                angle = step * math.tau / 1440
                x = focus[0] + radius * math.cos(angle)
                y = focus[1] + radius * math.sin(angle)
                assert sample(image, x, y) > 245, (
                    f'Empty view casts a shadow at focus {focus}, radius {radius}, angle {angle}')
    width, height = closed.size
    partial = [closed.getpixel((x, y))[0]
               for y in range(int(height * .28), int(height * .35))
               for x in range(int(width / 2 + height * .14),
                              int(width / 2 + height * .22))]
    assert any(10 < value < 245 for value in partial), 'No antialiased edge coverage'
    pixel_world_size = 24 / height
    for x in (2, 2.5, 3):
        assert sample(corridor, x, -2 * pixel_world_size) < 10, 'Horizontal wall seam leaks light'
        assert sample(corridor, x, 2 * pixel_world_size) > 245, 'Visible corridor floor is dark'
    report = {'drawable': list(closed.size), 'pixel_world_size': pixel_world_size,
              'phases': ['closed', 'memory', 'open', 'corridor', 'shifted']}
    (output / 'report.json').write_text(json.dumps(report, indent=2) + '\n')
    print('PASS GPU fog: shadow geometry, wall contact, edge coverage, range fade, discovery and disabled blockers')


if __name__ == '__main__':
    main()

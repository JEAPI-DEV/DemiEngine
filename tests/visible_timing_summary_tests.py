import csv
from pathlib import Path
import sys
import tempfile
import unittest
import subprocess
import json
import shutil
import struct

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from benchmark_3d_visible import summarize, distribution
from benchmark_barrel_tower import phase_frames, impact_burst_end
import animation_crowd_workload as crowd
from split_skin_fixture import split_skin


class SummaryTests(unittest.TestCase):
    def test_mixed_and_budgeted_capture_requires_all_work_and_collisions(self):
        result={'valid_capture':True, 'Renderer3D.meshes_visible':{'min':41},
                'Renderer3D.animation_rebuild.calls':{'min':0,'max':20},
                'Renderer3D.animation_accounted':{'min':20,'max':20},
                'Physics3D.bodies':{'min':41,'max':41},
                'Physics3D.active_bodies':{'min':40,'max':40},
                'Physics3D.contact_pairs':{'max':60}}
        crowd.qualify(result,40,'mixed',15)
        self.assertTrue(result['valid_capture'])
        result['Physics3D.contact_pairs']={'max':0}
        crowd.qualify(result,40,'mixed',15)
        self.assertFalse(result['valid_capture'])

    def test_split_fixture_preserves_geometry_and_animation_bytes(self):
        source = Path(__file__).resolve().parents[1] / 'examples/animation_3d/assets/AnimationLib/UAL1_Standard.glb'
        original = source.read_bytes()
        def unpack(raw):
            length = struct.unpack_from('<I', raw, 12)[0]
            return json.loads(raw[20:20 + length]), raw[20 + length:]
        with tempfile.TemporaryDirectory(prefix='demi-skin-fixture-') as folder:
            output = Path(folder) / 'split.glb'
            split_skin(source, output)
            before, original_binary = unpack(original)
            after, split_binary = unpack(output.read_bytes())
            self.assertEqual(original_binary, split_binary)
            for key in ('animations', 'accessors', 'bufferViews', 'buffers'):
                self.assertEqual(before[key], after[key])
            self.assertEqual(len(after['skins']), len(before['skins']) + 1)
            self.assertEqual(after['meshes'][0]['primitives'] + after['meshes'][-1]['primitives'],
                             before['meshes'][0]['primitives'])
            with self.assertRaises(ValueError):
                split_skin(source, output)
            with self.assertRaises(ValueError):
                split_skin(source, source)
        self.assertEqual(original, source.read_bytes())

    def test_skinning_mode_requires_explicit_population_evidence(self):
        for mode, scope in [('gpu', 'Renderer3D.gpu_skinned_meshes'), ('cpu', 'Renderer3D.cpu_skinned_meshes')]:
            for population, valid in [(None, False), ({'min':63, 'max':64}, False), ({'min':64, 'max':64}, True)]:
                result = {'valid_capture':True, scope:population}
                crowd.qualify_skinning(result, 64, mode)
                self.assertEqual(result['valid_capture'], valid)

    def test_crowd_configuration_preserves_source_and_matches_control(self):
        source = Path(__file__).resolve().parents[1] / 'examples/animation_3d/scenes/crowd.scene.json'
        original = source.read_bytes()
        with tempfile.TemporaryDirectory(prefix='demi-crowd-test-') as folder:
            project = Path(folder)
            (project / 'scenes').mkdir()
            shutil.copy2(source, project / 'scenes/crowd.scene.json')
            settings = {}
            configurations = []
            for workload in ('animated', 'frozen'):
                crowd.configure(project, settings, 64, workload, 6)
                configurations.append(json.loads((project / 'scenes/crowd.scene.json').read_text()))
            self.assertEqual(settings['main_scene'], 'scene://animation_3d/crowd')
            live, frozen = configurations
            self.assertEqual(live['entities'][:3], frozen['entities'][:3])
            props = frozen['entities'][3]['components']['LuaScript']['properties']
            self.assertEqual(props, {'count': 64, 'frozen': True, 'duration_seconds': 6})
            with self.assertRaises(ValueError):
                crowd.configure(project, settings, 2001, 'animated', 6)
        self.assertEqual(source.read_bytes(), original)

    def test_crowd_requires_visible_population_and_playback(self):
        def result(visible=65, calls=64):
            return {'valid_capture': True, 'Renderer3D.meshes_visible': {'min': visible},
                    'Renderer3D.animation_rebuild.calls': {'min': calls, 'max': calls}}
        for workload, calls in [('animated', 64), ('frozen', 0)]:
            capture = result(calls=calls)
            crowd.qualify(capture, 64, workload)
            self.assertTrue(capture['valid_capture'])
        for capture in [result(visible=64), result(calls=0)]:
            crowd.qualify(capture, 64, 'animated')
            self.assertFalse(capture['valid_capture'])

    def capture(self, gpu=True, width=1920, interrupted=False, errors=0, startup_only=False):
        with tempfile.TemporaryDirectory(prefix='demi-timing-test-') as folder:
            path = Path(folder) / 'frames.csv'
            with path.open('w', newline='') as output:
                writer = csv.writer(output)
                writer.writerow(['frame', 'scope', 'total_ms', 'calls', 'gauge'])
                for frame in range(6):
                    writer.writerow([frame, 'Frame.interval', 20, 1, ''])
                    writer.writerow([frame, 'Graphics.submitted_frame', 0, 0, frame + 10])
                    for scope, value in [('Graphics.backbuffer_width', width), ('Graphics.backbuffer_height', 1080),
                                         ('Simulation.dropped_fixed_ms', 10 if frame == 0 else 0),
                                         ('Simulation.advanced_ms', 0 if frame % 2 else 33.333),
                                         ('Window.capture_interrupted', int(interrupted))]:
                        writer.writerow([frame, scope, 0, 0, value])
                    if frame % 2 == 0:
                        writer.writerow([frame, 'Physics3D.step', 5, 1, ''])
                    if errors is not None:
                        writer.writerow([frame, 'Physics3D.update_error_steps', 0, 0,
                                         errors if not startup_only or frame == 0 else 0])
                    if gpu:
                        writer.writerow([frame, 'Graphics.gpu', 1, 1, ''])
                        writer.writerow([frame, 'Graphics.gpu_frame', 0, 0, frame + 9])
            return summarize(path, .05, 1920, 1080)

    def test_warmup_and_delayed_gpu(self):
        result = self.capture()
        self.assertTrue(result['valid_capture'])
        self.assertEqual(result['measured_frames'], 4)
        self.assertEqual(result['metrics']['Graphics.gpu']['samples'], 3)
        self.assertEqual(result['metrics']['Physics3D.step']['samples'], 4)
        self.assertEqual(result['metrics']['Physics3D.step']['p50_ms'], 0)
        self.assertEqual(result['dropped_fixed_ms'], 0)
        self.assertEqual(result['all_frames_dropped_fixed_ms'], 10)

    def test_missing_gpu_is_not_zero(self):
        result = self.capture(gpu=False)
        self.assertIsNone(result['metrics']['Graphics.gpu'])
        self.assertFalse(result['valid_capture'])

    def test_invalid_resolution_and_interruption(self):
        self.assertFalse(self.capture(width=960)['valid_capture'])
        self.assertFalse(self.capture(interrupted=True)['valid_capture'])

    def test_percentiles(self):
        self.assertIsNone(distribution([]))
        self.assertEqual(distribution(list(range(1, 101)))['p95_ms'], 95)

    def test_physics_capacity_errors_and_missing_telemetry(self):
        result = self.capture(errors=1)
        self.assertEqual(result['physics_update_error_steps'], 1)
        self.assertFalse(result['valid_capture'])
        result = self.capture(errors=None)
        self.assertIsNone(result['physics_update_error_steps'])
        self.assertFalse(result['valid_capture'])
        # Even a subsequently replaced world's startup failure invalidates the
        # run; warmup exclusion is only for timing, not simulation correctness.
        self.assertFalse(self.capture(errors=1, startup_only=True)['valid_capture'])

    def test_tower_phase_boundary_survives_projectile_cleanup(self):
        with tempfile.TemporaryDirectory(prefix='demi-tower-phase-test-') as folder:
            path = Path(folder) / 'frames.csv'
            with path.open('w', newline='') as stream:
                writer = csv.writer(stream)
                writer.writerow(['frame', 'scope', 'total_ms', 'calls', 'gauge'])
                for frame, bodies, active in [(0, 3, 2), (1, 3, 0), (2, 4, 3), (3, 3, 1), (4, 3, 0)]:
                    writer.writerow([frame, 'Physics3D.bodies', 0, 0, bodies])
                    writer.writerow([frame, 'Physics3D.active_bodies', 0, 0, active])
            _, rows, shot = phase_frames(path, 2)
            self.assertEqual(shot, 2)
            self.assertEqual(impact_burst_end(rows, shot, 2), 4)
            self.assertIsNone(phase_frames(path, 5)[2])
            self.assertIsNone(impact_burst_end(rows, None, 2))

    def test_tower_rejects_invalid_window_size_before_creating_output(self):
        with tempfile.TemporaryDirectory(prefix='demi-tower-cli-test-') as folder:
            output = Path(folder) / 'capture'
            script = Path(__file__).resolve().parents[1] / 'scripts/benchmark_barrel_tower.py'
            result = subprocess.run([sys.executable, str(script), '--binary', sys.executable,
                                     '--output', str(output), '--window-width', '0'],
                                    capture_output=True, text=True)
            self.assertEqual(result.returncode, 2)
            self.assertFalse(output.exists())


if __name__ == '__main__':
    unittest.main()

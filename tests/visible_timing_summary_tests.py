import csv
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from benchmark_3d_visible import summarize, distribution


class SummaryTests(unittest.TestCase):
    def capture(self, gpu=True, width=1920, interrupted=False):
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


if __name__ == '__main__':
    unittest.main()

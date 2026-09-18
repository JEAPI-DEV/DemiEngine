import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch


SCRIPT = Path(__file__).parents[1] / "scripts" / "android_device.py"
SPEC = importlib.util.spec_from_file_location("android_device", SCRIPT)
android_device = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(android_device)


class AndroidDeviceToolTests(unittest.TestCase):
    def test_resume_probe_requires_same_process_and_scoped_surface_markers(self):
        class FakeAdb:
            def __init__(self, pids, log_pid):
                self.pids = iter(pids)
                self.log_pid = log_pid
                self.calls = []

            def run(self, *args, **kwargs):
                self.calls.append(args)
                if args[:2] == ('shell', 'pidof'):
                    value = next(self.pids)
                elif args[0] == 'logcat':
                    value = '\n'.join(f'09-15 12:00:00 {self.log_pid} 42 I DemiEngine: {marker}'
                                      for marker in ('[surface] Java surfaceDestroyed.',
                                                     '[surface] Java surfaceCreated.',
                                                     '[render] Rebinding bgfx to native window'))
                elif args[0] == 'exec-out':
                    value = b'fake screenshot'
                else:
                    value = ''
                return SimpleNamespace(stdout=value, returncode=0)

        with tempfile.TemporaryDirectory() as temporary, patch.object(android_device.time, 'sleep'):
            output = Path(temporary)
            adb = FakeAdb(['42', '42'], '42')
            pids = set()
            result = android_device.qualification_resume_probe(adb, 'dev.test', 'dev.test/Main', output, 0, pids)
            self.assertTrue(result['same_process'])
            self.assertEqual(pids, {'42'})
            self.assertTrue((output / 'resumed.png').exists())
            self.assertEqual(adb.calls[-1], ('shell', 'am', 'force-stop', 'dev.test'))
            for changed, log_pid in ((['42', '43'], '42'), (['42', '42'], '7')):
                adb = FakeAdb(changed, log_pid)
                with self.assertRaises(android_device.ToolError):
                    android_device.qualification_resume_probe(adb, 'dev.test', 'dev.test/Main', output, 0, set())
                self.assertEqual(adb.calls[-1], ('shell', 'am', 'force-stop', 'dev.test'))

    def test_qualification_requires_surface_and_frame_pacing_markers(self):
        markers = android_device.REQUIRED_RUNTIME_MARKERS
        self.assertIn("[surface] Java surfaceCreated.", markers)
        self.assertIn("FPS from the Android compositor.",
                      markers)

    def test_default_artifact_uses_project_display_name(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory) / "demi.project.json"
            project.write_text(json.dumps({"name": "3D Animation"}))
            self.assertEqual(android_device.apk_path(project).name,
                             "3d_animation-debug.apk")
            self.assertNotIn("[save] Wrote save slot settings",
                             android_device.required_runtime_markers(project))
            project.write_text(json.dumps({
                "name": "3D Animation",
                "build": {"display_name": " Custom Demo! "},
                "main_scene": "scene://minimal_2d_android/menu"}))
            self.assertEqual(android_device.apk_path(project).name,
                             "custom_demo-debug.apk")
            self.assertIn("[render] Requested 60.0 FPS from the Android compositor.",
                          android_device.required_runtime_markers(project))

    def test_device_parser_uses_only_ready_devices(self):
        output = "List of devices attached\nready\tdevice model:Pixel\noffline\toffline\n"
        self.assertEqual(android_device.parse_devices(output), ["ready"])

    def test_project_configuration_uses_build_identity(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory) / "demi.project.json"
            project.write_text(json.dumps({
                "build": {
                    "application_id": "dev.example.game",
                    "executable_name": "example_game"
                }
            }), encoding="utf-8")
            package, executable, component = \
                android_device.project_configuration(project)
            self.assertEqual(package, "dev.example.game")
            self.assertEqual(executable, "example_game")
            self.assertEqual(component,
                             "dev.example.game/dev.jeapi.demi.android.DemiActivity")

    def test_source_snapshot_excludes_generated_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            (root / "scripts").mkdir()
            (root / "scripts/game.lua").write_text("return {}")
            (root / "generated").mkdir()
            (root / "generated/output.bin").write_bytes(b"generated")
            snapshot = android_device.source_snapshot(root)
            self.assertIn("scripts/game.lua", snapshot)
            self.assertNotIn("generated/output.bin", snapshot)

    def test_apk_freshness_tracks_sources_but_ignores_generated_state(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "scripts" / "game.lua"
            source.parent.mkdir()
            source.write_text("return {}", encoding="utf-8")
            generated = root / "generated" / "game.bin"
            generated.parent.mkdir()
            generated.write_bytes(b"generated")
            apk = root / "game.apk"
            self.assertTrue(android_device.apk_needs_build(apk, [root]))

            apk.write_bytes(b"apk")
            os.utime(source, ns=(1_000_000_000, 1_000_000_000))
            os.utime(apk, ns=(2_000_000_000, 2_000_000_000))
            os.utime(generated, ns=(3_000_000_000, 3_000_000_000))
            self.assertFalse(android_device.apk_needs_build(apk, [root]))

            os.utime(source, ns=(4_000_000_000, 4_000_000_000))
            self.assertTrue(android_device.apk_needs_build(apk, [root]))


if __name__ == "__main__":
    unittest.main()

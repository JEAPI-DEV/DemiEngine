import importlib.util
import json
import os
from pathlib import Path
import tempfile
import unittest


SCRIPT = Path(__file__).parents[1] / "scripts" / "android_device.py"
SPEC = importlib.util.spec_from_file_location("android_device", SCRIPT)
android_device = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(android_device)


class AndroidDeviceToolTests(unittest.TestCase):
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

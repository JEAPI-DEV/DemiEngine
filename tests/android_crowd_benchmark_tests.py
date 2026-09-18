import sys
from pathlib import Path
import tempfile
import unittest
import zipfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'scripts'))
from benchmark_android_crowd import extract_cooked_assets


class AndroidCrowdBenchmarkTests(unittest.TestCase):
    def test_extracts_only_assets_and_requires_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / 'input.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('assets/demi.project.json', '{}')
                archive.writestr('assets/scenes/main.scene.json', '{}')
                archive.writestr('assets/demi_asset_index.txt', 'generated')
                archive.writestr('lib/arm64-v8a/libmain.so', 'not an asset')
            extract_cooked_assets(apk, root / 'output')
            self.assertTrue((root / 'output/scenes/main.scene.json').is_file())
            self.assertFalse((root / 'output/lib').exists())
            self.assertFalse((root / 'output/demi_asset_index.txt').exists())
            with self.assertRaises(FileExistsError):
                extract_cooked_assets(apk, root / 'output')

    def test_rejects_traversal(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / 'input.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('assets/../outside', 'bad')
            with self.assertRaises(ValueError):
                extract_cooked_assets(apk, root / 'output')
            self.assertFalse((root / 'outside').exists())

    def test_rejects_missing_project(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            apk = root / 'input.apk'
            with zipfile.ZipFile(apk, 'w') as archive:
                archive.writestr('lib/native.so', 'not a project')
            with self.assertRaises(ValueError):
                extract_cooked_assets(apk, root / 'output')


if __name__ == '__main__':
    unittest.main()

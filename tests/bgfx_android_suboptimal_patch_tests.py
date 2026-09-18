import pathlib
import shutil
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCH = ROOT / 'cmake/patches/apply_bgfx_android_suboptimal.cmake'


class SuboptimalPatchTests(unittest.TestCase):
    def test_promotes_known_cache_local_fix_without_losing_other_edits(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            target = root / 'bgfx/src/renderer_vk.cpp'
            target.parent.mkdir(parents=True)
            source = (ROOT / 'tests/fixtures/bgfx_suboptimal_policy.cpp').read_text()
            cases = '\t\t\tcase VK_ERROR_OUT_OF_DATE_KHR:\n\t\t\tcase VK_SUBOPTIMAL_KHR:'
            comments = [
                '// The image was acquired and can still be presented per spec.\n'
                '\t\t\t\t// Displays whose transform differs from the requested preTransform\n'
                '\t\t\t\t// return this on every frame; recreating the swapchain each frame\n'
                '\t\t\t\t// destroys performance, so present the acquired image as-is.',
                '// Presentable per spec; see the acquire path for why recreating\n'
                '\t\t\t\t// here would thrash the swapchain every frame.']
            for action, comment in zip(('return false;', 'break;'), comments):
                source = source.replace(
                    cases + '\n        m_needToRecreateSwapchain = true;\n        ' + action,
                    '\t\t\tcase VK_ERROR_OUT_OF_DATE_KHR:\n'
                    '\t\t\t\tm_needToRecreateSwapchain = true;\n\t\t\t\t' + action +
                    '\n\n\t\t\tcase VK_SUBOPTIMAL_KHR:\n\t\t\t\t' + comment +
                    '\n\t\t\t\tbreak;', 1)
            target.write_text(source + '\n// unrelated local edit\n')
            subprocess.run(['cmake', '-DSOURCE_DIR=' + str(root), '-P', str(PATCH)],
                           check=True, capture_output=True)
            patched = target.read_text()
            self.assertEqual(patched.count('Demi Android suboptimal policy'), 2)
            self.assertIn('// unrelated local edit', patched)

    def test_android_success_desktop_recreate_and_real_errors(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            target = root / 'bgfx/src/renderer_vk.cpp'
            target.parent.mkdir(parents=True)
            shutil.copyfile(ROOT / 'tests/fixtures/bgfx_suboptimal_policy.cpp', target)
            command = ['cmake', '-DSOURCE_DIR=' + str(root), '-P', str(PATCH)]
            subprocess.run(command, check=True, capture_output=True)
            patched = target.read_bytes()
            subprocess.run(command, check=True, capture_output=True)
            self.assertEqual(patched, target.read_bytes())
            for android in (0, 1):
                binary = root / f'policy-{android}'
                subprocess.run(['c++', '-std=c++20', '-include', 'initializer_list',
                                f'-DBX_PLATFORM_ANDROID={android}', str(target),
                                '-o', str(binary)], check=True, capture_output=True)
                subprocess.run([str(binary)], check=True)

    def test_mismatched_source_fails_without_writing(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = pathlib.Path(temporary)
            target = root / 'bgfx/src/renderer_vk.cpp'
            target.parent.mkdir(parents=True)
            target.write_text('unrecognized upstream source')
            result = subprocess.run(['cmake', '-DSOURCE_DIR=' + str(root), '-P', str(PATCH)],
                                    capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual(target.read_text(), 'unrecognized upstream source')


if __name__ == '__main__':
    unittest.main()

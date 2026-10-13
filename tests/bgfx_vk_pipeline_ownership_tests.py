"""Check patch safety and execute bgfx's actual parent-aware pipeline eviction."""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCH = ROOT / "cmake/patches/apply_bgfx_vk_pipeline_ownership.cmake"
source_path = pathlib.Path(sys.argv[1])
source = source_path.read_text()
compiler = sys.argv[2]
original = source.replace(
    '\t\t\t// Demi program-owned Vulkan pipelines: retire before the layout.\n'
    '\t\t\tm_pipelineStateCache.invalidateWithParent(_handle.idx);\n', '')
original = re.sub(r'\t\t\tmurmur.add\(_program.idx\); // Demi (compute|graphics) pipeline owner\n', '', original)
original = original.replace('m_pipelineStateCache.add(hash, pipeline, _program.idx);',
                            'm_pipelineStateCache.add(hash, pipeline);')
assert original != source
cache_source = (source_path.parent / 'renderer.h').read_text()
start = cache_source.index('\ttemplate<typename Ty>\n\tclass StateCacheT')
cache = cache_source[start:cache_source.index('\n\tclass StateCache\n', start)]

with tempfile.TemporaryDirectory(prefix='demi-vk-pipeline-') as directory:
    root = pathlib.Path(directory)
    target = root / 'bgfx/src/renderer_vk.cpp'
    target.parent.mkdir(parents=True)
    target.write_text(original)
    command = ['cmake', '-DSOURCE_DIR=' + str(root), '-P', str(PATCH)]
    subprocess.run(command, check=True, capture_output=True)
    patched = target.read_text()
    assert patched == source, 'Patch changed unrelated pinned source'
    subprocess.run(command, check=True, capture_output=True)
    assert target.read_text() == patched, 'Patch is not idempotent'
    assert patched.count('m_pipelineStateCache.add(hash, pipeline, _program.idx);') == 2
    for kind in ('compute', 'graphics'):
        assert f'murmur.add(_program.idx); // Demi {kind} pipeline owner' in patched

    for name, text in [('before', original), ('after', patched)]:
        destroy = text.split('void destroyProgram(ProgramHandle _handle) override\n\t\t{', 1)[1].split('\n\t\t}', 1)[0]
        add = re.findall(r'm_pipelineStateCache.add\(hash, pipeline[^;]*;', text)
        assert len(add) == 2 and add[0] == add[1]
        fixture = r'''
#include <cstdint>
#include <unordered_map>
#include <utility>
#include <vector>
namespace stl = std;
std::vector<int> events;
template<typename T> struct StateCacheFuncT {
  static void validate(T, uint64_t) {}
  static void evict(T value) { events.push_back(value); }
};
''' + cache + r'''
struct ProgramHandle { uint16_t idx; };
struct Program { void destroy() { events.push_back(-1); } };
struct Renderer {
  StateCacheT<int> m_pipelineStateCache;
  Program m_program[2];
  void add(uint64_t hash, int pipeline, ProgramHandle _program) {
''' + add[0] + r'''
  }
  void destroyProgram(ProgramHandle _handle) {
''' + destroy + r'''
  }
};
int main() {
  Renderer renderer;
  renderer.add(100, 10, {0});
  renderer.add(101, 11, {0});
  renderer.add(200, 20, {1});
  renderer.destroyProgram({0});
  if (renderer.m_pipelineStateCache.getCount() != 1 ||
      renderer.m_pipelineStateCache.find(100) ||
      renderer.m_pipelineStateCache.find(101) ||
      renderer.m_pipelineStateCache.find(200) != 20) return 1;
  if (events.size() != 3 || events.back() != -1) return 2;
  // Reuse the retired program slot without disturbing another live program.
  renderer.add(100, 30, {0});
  if (renderer.m_pipelineStateCache.find(100) != 30) return 3;
  renderer.destroyProgram({1});
  if (renderer.m_pipelineStateCache.find(100) != 30 ||
      renderer.m_pipelineStateCache.find(200)) return 4;
  renderer.destroyProgram({0});
  return renderer.m_pipelineStateCache.getCount() == 0 ? 0 : 5;
}
'''
        (root / (name + '.cpp')).write_text(fixture)
    (root / 'CMakeLists.txt').write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(PipelineOwnership LANGUAGES CXX)\n'
        'set(CMAKE_CXX_STANDARD 20)\n'
        'set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")\n'
        'add_executable(before before.cpp)\nadd_executable(after after.cpp)\n')
    build = root / 'build'
    subprocess.run(['cmake', '-S', str(root), '-B', str(build),
                    '-DCMAKE_CXX_COMPILER=' + compiler], check=True, capture_output=True)
    subprocess.run(['cmake', '--build', str(build), '--config', 'Debug'], check=True, capture_output=True)
    for name, expected in [('before', 1), ('after', 0)]:
        binaries = [p for p in (build / 'bin').rglob('*') if p.is_file() and p.name in (name, name + '.exe')]
        assert len(binaries) == 1
        result = subprocess.run([str(binaries[0])])
        assert result.returncode == expected, (name, result.returncode)
    for drift in ('unrecognized upstream source', original + original, patched + original):
        target.write_text(drift)
        result = subprocess.run(command, capture_output=True)
        assert result.returncode != 0 and target.read_text() == drift
print('Vulkan pipeline ownership regression and patch checks passed')

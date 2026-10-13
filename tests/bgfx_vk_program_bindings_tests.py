"""Exercise the actual pinned ProgramVK binding loop across program-slot reuse."""
import pathlib
import re
import subprocess
import sys
import tempfile

ROOT = pathlib.Path(__file__).resolve().parents[1]
PATCH = ROOT / "cmake/patches/apply_bgfx_vk_program_bindings.cmake"
source = pathlib.Path(sys.argv[1]).read_text()
compiler = sys.argv[2]
reset = re.compile(r"\t\t\t// Demi reset reused Vulkan program bindings\.\n(?:\t\t\tm_bindInfo\[stage\][^\n]*\n){5}")
original = reset.sub("", source)
assert original != source, "Configured bgfx source is missing the reset patch"

PREFIX = r'''
#include <cstdint>
#include <cassert>
#include <cstddef>
#define BX_COUNTOF(a) (sizeof(a) / sizeof((a)[0]))
#define BX_ASSERT(condition, ...) assert(condition)
struct UniformHandle { uint16_t idx; };
#define BGFX_INVALID_HANDLE UniformHandle{UINT16_MAX}
bool isValid(UniformHandle h) { return h.idx != UINT16_MAX; }
struct BindType { enum Enum { Buffer, Image, Sampler, Count }; };
struct BindInfo {
  UniformHandle uniformHandle = BGFX_INVALID_HANDLE;
  BindType::Enum type = BindType::Count;
  uint32_t binding = 0, samplerBinding = 0, index = UINT32_MAX;
};
struct TextureBindInfo { int type = 0; };
struct ShaderVK {
  BindInfo m_bindInfo[16];
  TextureBindInfo m_textures[16];
  bool m_oldBindingModel = false;
};
struct ProgramVK {
  const ShaderVK *m_vsh = nullptr, *m_fsh = nullptr;
  BindInfo m_bindInfo[16];
  TextureBindInfo m_textures[16];
  uint8_t m_numTextures = 0;
  void create(const ShaderVK* _vsh, const ShaderVK* _fsh) {
    m_vsh = _vsh;
    m_fsh = _fsh;
'''
SUFFIX = r'''
  }
};
int main() {
  ShaderVK vertex, rich, sparse, empty;
  rich.m_bindInfo[7] = {{7}, BindType::Sampler, 17, 33, 0};
  rich.m_textures[0].type = 7;
  sparse.m_bindInfo[0] = {{0}, BindType::Sampler, 1, 2, 0};
  sparse.m_textures[0].type = 2;
  ProgramVK program;
  program.create(&vertex, &rich);
  if (!isValid(program.m_bindInfo[7].uniformHandle) || program.m_numTextures != 1)
    return 2;
  program.create(&vertex, &sparse);
  if (isValid(program.m_bindInfo[7].uniformHandle)) return 1;
  if (program.m_numTextures != 1 || program.m_bindInfo[0].index != 0 ||
      program.m_textures[0].type != 2 || program.m_bindInfo[0].binding != 1)
    return 3;
  program.create(&empty, nullptr);
  if (program.m_numTextures != 0) return 4;
  for (const auto &binding : program.m_bindInfo)
    if (isValid(binding.uniformHandle) || binding.type != BindType::Count ||
        binding.index != UINT32_MAX || binding.binding || binding.samplerBinding)
      return 5;
  // A vertex-only texture also survives the reset/remapping pass correctly.
  program.create(&rich, nullptr);
  return program.m_numTextures == 1 && program.m_textures[0].type == 7 ? 0 : 6;
}
'''

with tempfile.TemporaryDirectory(prefix="demi-vk-binding-") as directory:
    root = pathlib.Path(directory)
    target = root / "bgfx/src/renderer_vk.cpp"
    target.parent.mkdir(parents=True)
    target.write_text(original)
    command = ["cmake", "-DSOURCE_DIR=" + str(root), "-P", str(PATCH)]
    subprocess.run(command, check=True, capture_output=True)
    patched = target.read_text()
    subprocess.run(command, check=True, capture_output=True)
    assert target.read_text() == patched, "Patch is not idempotent"
    for name, text in [("before", original), ("after", patched)]:
        create = text.split("void ProgramVK::create(", 1)[1]
        loop = create[create.index("m_numTextures = 0;"):create.index("// create exact pipeline layout")]
        cpp = root / (name + ".cpp")
        cpp.write_text(PREFIX + loop + SUFFIX)
    # Let CMake select compiler flags and executable suffixes on each platform.
    (root / "CMakeLists.txt").write_text(
        'cmake_minimum_required(VERSION 3.20)\nproject(BindingReuse LANGUAGES CXX)\n'
        'set(CMAKE_CXX_STANDARD 20)\n'
        'set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")\n'
        'add_executable(before before.cpp)\nadd_executable(after after.cpp)\n')
    build = root / "build"
    subprocess.run(["cmake", "-S", str(root), "-B", str(build),
                    "-DCMAKE_CXX_COMPILER=" + compiler], check=True, capture_output=True)
    subprocess.run(["cmake", "--build", str(build), "--config", "Debug"],
                   check=True, capture_output=True)
    for name, expected in [("before", 1), ("after", 0)]:
        binaries = [path for path in (build / "bin").rglob("*")
                    if path.is_file() and path.name in (name, name + ".exe")]
        assert len(binaries) == 1, binaries
        result = subprocess.run([str(binaries[0])])
        assert result.returncode == expected, (name, result.returncode)
    # Ambiguous upstream changes must fail rather than patch multiple loops.
    target.write_text(original + original)
    result = subprocess.run(command, capture_output=True)
    assert result.returncode != 0 and target.read_text() == original + original
    target.write_text(patched + original)
    result = subprocess.run(command, capture_output=True)
    assert result.returncode != 0 and target.read_text() == patched + original
    target.write_text("upstream shape changed")
    result = subprocess.run(command, capture_output=True)
    assert result.returncode != 0 and target.read_text() == "upstream shape changed"
print("Vulkan program binding reuse regression and patch checks passed")

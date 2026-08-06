#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/CookedShaderLibrary.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <set>
#include <string>

using namespace demi;
using namespace demi::runtime::render;

namespace {

void write(const std::filesystem::path &path, const std::string &contents) {
  std::filesystem::create_directories(path.parent_path());
  std::ofstream(path, std::ios::binary) << contents;
}

class FakeResources final : public GpuResources {
public:
  TextureHandle createTexture(const TextureCreateInfo &,
                              std::string &) override {
    return {.index = next_++, .generation = 1};
  }
  SamplerHandle createSampler(std::string_view, std::string &) override {
    return {.index = next_++, .generation = 1};
  }
  UniformHandle createUniform(std::string_view, UniformType, std::uint16_t,
                              std::string &error) override {
    ++uniformAttempts;
    if (failUniformAt != 0 && uniformAttempts == failUniformAt) {
      error = "injected uniform failure";
      return {};
    }
    const UniformHandle result{.index = next_++, .generation = 1};
    uniforms.insert(result.index);
    return result;
  }
  BufferHandle createBuffer(const BufferCreateInfo &, std::string &) override {
    return {.index = next_++, .generation = 1};
  }
  ProgramHandle createProgram(const ProgramCreateInfo &info,
                              std::string &error) override {
    ++programAttempts;
    if (failProgramAt != 0 && programAttempts == failProgramAt) {
      error = "injected program failure";
      return {};
    }
    assert(!info.vertexShader.empty() && !info.fragmentShader.empty());
    const ProgramHandle result{.index = next_++, .generation = 1};
    programs.insert(result.index);
    return result;
  }
  ProgramHandle createBuiltinProgram(BuiltinProgram,
                                     std::string &) override {
    const ProgramHandle result{.index = next_++, .generation = 1};
    programs.insert(result.index);
    return result;
  }
  std::string_view shaderBackend() const override { return "opengl"; }
  RenderTargetHandles createRenderTarget(const RenderTargetCreateInfo &,
                                         std::string &) override {
    return {};
  }
  bool destroy(TextureHandle) override { return true; }
  bool destroy(SamplerHandle) override { return true; }
  bool destroy(UniformHandle handle) override {
    return uniforms.erase(handle.index) == 1;
  }
  bool destroy(BufferHandle) override { return true; }
  bool destroy(ProgramHandle handle) override {
    return programs.erase(handle.index) == 1;
  }
  bool destroy(FrameBufferHandle) override { return true; }
  void clear() override {
    uniforms.clear();
    programs.clear();
  }

  std::uint32_t failUniformAt = 0;
  std::uint32_t failProgramAt = 0;
  std::uint32_t uniformAttempts = 0;
  std::uint32_t programAttempts = 0;
  std::set<std::uint32_t> uniforms;
  std::set<std::uint32_t> programs;

private:
  std::uint32_t next_ = 1;
};

std::string manifest(const std::string &entries) {
  return "{\"format_version\":1,\"platform\":\"linux\"," \
         "\"shader_programs\":[" +
         entries + "]}";
}

} // namespace

int main() {
  const auto root =
      std::filesystem::temp_directory_path() / "demi_material_library_tests";
  std::filesystem::remove_all(root);
  write(root / "vertex.bin", "vertex");
  write(root / "fragment.bin", "fragment");
  write(root / "cook.manifest.json",
        manifest(R"({"asset":"asset://shader/test","backend":"opengl","vertex":"vertex.bin","fragment":"fragment.bin"})"));

  AssetRegistry registry{.projectDirectory = root,
                         .assets = {{.id = "asset://shader/test",
                                     .type = "Shader"}}};
  FakeResources resources;
  std::vector<std::string> diagnostics;
  CookedShaderLibrary shaders(resources);
  assert(shaders.load(registry, diagnostics));
  const ProgramHandle original = shaders.find("asset://shader/test");
  assert(original && shaders.size() == 1 && resources.programs.size() == 1);

  // Duplicate IDs fail atomically: the old working program remains alive and
  // the partially-created replacement is destroyed.
  const std::string entry =
      R"({"asset":"asset://shader/test","backend":"opengl","vertex":"vertex.bin","fragment":"fragment.bin"})";
  write(root / "cook.manifest.json", manifest(entry + "," + entry));
  diagnostics.clear();
  assert(!shaders.load(registry, diagnostics));
  assert(shaders.find("asset://shader/test") == original);
  assert(resources.programs.size() == 1);

  write(root / "cook.manifest.json",
        manifest(R"({"asset":"asset://shader/test","backend":"opengl","vertex":"../escape.bin","fragment":"fragment.bin"})"));
  diagnostics.clear();
  assert(!shaders.load(registry, diagnostics));
  assert(shaders.find("asset://shader/test") == original);
  assert(resources.programs.size() == 1);
  shaders.clear();
  assert(resources.programs.empty());

  write(root / "cook.manifest.json", manifest(entry));
  write(root / "test.material.json",
        R"({"format_version":1,"shader":"asset://shader/test","textures":{"albedo":"asset://texture/test"},"parameters":{"strength":0.5,"effect_color":[1,0.25,0.5,1]},"render_state":{"blend":"alpha","cull":"none","depth_test":false,"depth_write":false,"alpha_cutoff":0.25}})");
  registry.assets.push_back({.id = "asset://material/test",
                             .type = "Material",
                             .sourcePath = root / "test.material.json"});
  MaterialLibrary materials(resources);
  diagnostics.clear();
  assert(materials.load(registry, diagnostics));
  const MaterialBinding *material = materials.find("asset://material/test");
  assert(material && material->program && material->uniformSet != 0);
  assert(material->albedoTexture == "asset://texture/test");
  assert(material->state.blend == BlendMode::Alpha);
  assert(material->state.depthTest == DepthTest::Disabled);
  assert(!material->state.writeDepth);
  assert(material->alphaCutoff == 0.25F);
  assert(material->uniforms.size() == 2 && resources.uniforms.size() == 2);
  materials.clear();
  assert(resources.uniforms.empty() && resources.programs.empty());

  // A mid-material uniform failure cannot leak uniforms created earlier in
  // the same binding.
  resources.uniformAttempts = 0;
  resources.failUniformAt = 2;
  diagnostics.clear();
  assert(!materials.load(registry, diagnostics));
  assert(materials.find("asset://material/test") == nullptr);
  assert(resources.uniforms.empty());
  materials.clear();
  assert(resources.programs.empty());

  std::filesystem::remove_all(root);
  return 0;
}

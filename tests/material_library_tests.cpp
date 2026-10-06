#include "demi/runtime/render/MaterialLibrary.h"
#include "demi/runtime/render/backend/CookedShaderLibrary.h"
#include "demi/runtime/render/bgfx3d/MeshSurface3D.h"
#include "demi/runtime/render/bgfx3d/MeshDrawOrder3D.h"

#include <cassert>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <set>
#include <string>

using namespace demi;
using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

// Numeric reference for the fragment shader's stable GGX distribution.
float ggxDistribution(const float roughness, const float normalDotHalf) {
  const float alpha = roughness * roughness;
  const float alpha2 = alpha * alpha;
  const float crossSquared = 1.0F - normalDotHalf * normalDotHalf;
  const float denominator = crossSquared +
                            normalDotHalf * normalDotHalf * alpha2;
  const float ratio = alpha / std::max(denominator, 0.00000001F);
  return ratio * ratio / std::numbers::pi_v<float>;
}

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
  assert(std::abs(ggxDistribution(0.04F, 1.0F) - 124339.8F) < 10.0F);
  assert(std::abs(ggxDistribution(0.1F, 1.0F) - 3183.1F) < 0.5F);
  assert(std::abs(ggxDistribution(0.8F, 1.0F) - 0.77712F) < 0.001F);
  assert(std::isfinite(ggxDistribution(0.04F, 0.5F)) &&
         ggxDistribution(0.04F, 0.5F) > 0.0F);
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
        R"({"format_version":1,"shader":"asset://shader/test","textures":{"albedo":"asset://texture/test"},"parameters":{"strength":0.5,"effect_color":[1,0.25,0.5,1],"base_color":[0.2,0.4,0.6,0.8],"metallic":0.7,"roughness":0.2,"opacity":0.9},"render_state":{"blend":"alpha","cull":"none","depth_test":false,"depth_write":false,"alpha_cutoff":0.25}})");
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
  assert(material->uniforms.size() == 6 && resources.uniforms.size() == 6);
  assert(material->metallic == 0.7F && material->roughness == 0.2F &&
         material->opacity == 0.9F);
  assert(material->baseColor == (std::array<float, 4>{0.2F, 0.4F, 0.6F, 0.8F}));
  assert(!material->unlit);
  MeshRendererComponent mesh;
  MaterialBinding inherited;
  inherited = *material;
  inherited.state.writeDepth = true; // Even an old alpha asset cannot write depth.
  auto surface = resolveMeshSurface3D(mesh, &inherited);
  assert(surface.metallic == 0.7F && surface.roughness == 0.2F);
  assert(surface.baseColor == material->baseColor);
  assert(surface.opacity == 0.9F && surface.blend == BlendMode::Alpha);
  assert(!surface.state.writeDepth);
  mesh.materialNumbers = {{"metallic", 0.4F}, {"roughness", 0.5F},
                          {"opacity", 0.6F}};
  mesh.materialColors["base_color"] = {0.9F, 0.8F, 0.7F, 0.6F};
  surface = resolveMeshSurface3D(mesh, &inherited);
  assert(surface.metallic == 0.4F && surface.roughness == 0.5F &&
         surface.opacity == 0.6F);
  assert(surface.baseColor == (std::array<float, 4>{0.9F, 0.8F, 0.7F, 0.6F}));
  mesh.metallic = 0.1F;
  mesh.roughness = 0.9F;
  mesh.opacity = 0.3F;
  mesh.surfaceMode = "additive";
  surface = resolveMeshSurface3D(mesh, &inherited);
  assert(surface.metallic == 0.1F && surface.roughness == 0.9F &&
         surface.opacity == 0.3F && surface.blend == BlendMode::Additive);
  assert(!surface.state.writeDepth);
  mesh.surfaceMode = "opaque";
  surface = resolveMeshSurface3D(mesh, &inherited);
  assert(surface.blend == BlendMode::Opaque);
  surface = resolveMeshSurface3D(mesh, &inherited, 0.5F);
  assert(surface.blend == BlendMode::Alpha && !surface.state.writeDepth);
  mesh = {};
  surface = resolveMeshSurface3D(mesh, nullptr);
  assert(surface.metallic == 0.0F && surface.roughness == 0.8F &&
         surface.opacity == 1.0F && surface.blend == BlendMode::Opaque &&
         surface.state.writeDepth);
  mesh.opacity = 0.25F;
  assert(resolveMeshSurface3D(mesh, nullptr).blend == BlendMode::Opaque);
  mesh.roughness = 0.0F;
  assert(resolveMeshSurface3D(mesh, nullptr).roughness == 0.0F);
  // Resolving per draw means editing a material property does not require a
  // geometry or batch cache invalidation.
  mesh.roughness = 0.35F;
  const auto firstBatchKey = surfaceBatchKey3D(resolveMeshSurface3D(mesh, nullptr));
  assert(resolveMeshSurface3D(mesh, nullptr).roughness == 0.35F);
  mesh.roughness = 0.65F;
  assert(resolveMeshSurface3D(mesh, nullptr).roughness == 0.65F);
  assert(surfaceBatchKey3D(resolveMeshSurface3D(mesh, nullptr)) != firstBatchKey);

  std::vector<Entity> entities(3);
  entities[0].id = "near";
  entities[1].id = "opaque";
  entities[2].id = "far";
  for (Entity &entity : entities)
    entity.setComponent(MeshRendererComponent{});
  entities[0].component<MeshRendererComponent>()->surfaceMode = "transparent";
  entities[2].component<MeshRendererComponent>()->surfaceMode = "additive";
  std::vector<VisibleMesh3D> visible{
      {.entity = &entities[0], .transform = {.position = {0, 0, 1}}},
      {.entity = &entities[1], .transform = {.position = {0, 0, 0}}},
      {.entity = &entities[2], .transform = {.position = {0, 0, -3}}}};
  assert(orderMeshSurfaces3D(visible, materials, {0, 0, 5}, {0, 0, -1}) == 1);
  assert(visible[0].entity->id == "opaque" &&
         visible[1].entity->id == "far" &&
         visible[2].entity->id == "near");
  write(root / "unlit.material.json",
        R"({"format_version":1,"shader":"builtin://unlit"})");
  registry.assets.push_back({.id = "asset://material/unlit",
                             .type = "Material",
                             .sourcePath = root / "unlit.material.json"});
  diagnostics.clear();
  assert(materials.load(registry, diagnostics));
  assert(materials.find("asset://material/unlit") &&
         materials.find("asset://material/unlit")->unlit);
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

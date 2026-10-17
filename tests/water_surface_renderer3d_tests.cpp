#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/render/bgfx3d/PostProcessRenderer3D.h"
#include "demi/runtime/render/bgfx3d/WaterSurfaceRenderer3D.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/terrain/TerrainGraphExecutor.h"
#include "demi/runtime/terrain/TerrainWaterMesh.h"

#ifdef NDEBUG
#undef NDEBUG
#endif
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <iostream>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

using std::exp;
float max(float first, float second) { return std::max(first, second); }
float clamp(float value, float lower, float upper) {
  return std::clamp(value, lower, upper);
}
float mix(float first, float second, float weight) {
  return first + (second - first) * weight;
}
float smoothstep(float lower, float upper, float value) {
  const float normalized = clamp((value - lower) / (upper - lower), 0.F, 1.F);
  return normalized * normalized * (3.F - 2.F * normalized);
}
#include "demi/runtime/render/bgfx3d/shaders/demi_water_optics.sh"

void opticsRespectDepthAndGrazingAngles() {
  assert(waterDepthContribution(0, 3) == 1);
  assert(std::abs(waterDepthContribution(3, 3) - std::exp(-1.0F)) < 0.000001F);
  assert(waterDepthContribution(6, 3) < waterDepthContribution(3, 3));
  assert(waterDepthContribution(-1, 3) == 1);
  assert(waterRayPath(8, 4, 1) == 4);
  assert(waterRayPath(8, 4, 0.5F) == 8);
  assert(waterRayPath(2, 4, 1) == 0);
  assert(waterFresnel(0) == 1);
  assert(waterFresnel(1) > 0.02F && waterFresnel(1) < 0.021F);
  assert(waterFresnel(0.25F) > waterFresnel(0.75F));
  assert(waterSurfaceOpacity(0, 0.15F, 0.9F, false) == 0.9F);
  assert(std::abs(waterSurfaceOpacity(1, 0.15F, 0.9F, false) - 0.15F) <
         0.000001F);
  assert(waterSurfaceOpacity(0, 0.15F, 0.9F, true) == 0);
  assert(waterSurfaceOpacity(1, 0.15F, 0.9F, true) == 0);
  assert(waterShoreFoam(0, 0) == 0);
  assert(waterShoreFoam(0, 0.5F) == 1);
  assert(waterShoreFoam(0.5F, 0.5F) == 0);
}

void contactCoverageUsesOnlyThePixelDepthFootprint() {
  assert(waterContactCoverage(0, 2) == 0);
  assert(waterContactCoverage(1, 2) == 0.5F);
  assert(waterContactCoverage(2, 2) == 1);
  assert(waterContactCoverage(4, 2) == 1);
  assert(waterContactCoverage(-1, 2) == 0);
  assert(waterContactCoverage(0, 0) == 0);
  assert(waterContactCoverage(1, 0) == 1);
  assert(waterContactCoverage(-1, 0) == 0);
}

class CaptureCommands final : public RenderCommands {
public:
  struct Draw {
    BufferSlice vertices;
    BufferSlice indices;
    DrawState state;
    std::array<float, 16> transform;
    std::array<TextureHandle, 3> textures;
    std::array<std::array<float, 4>, 16> values;
  };
  std::vector<Draw> draws;
  std::vector<std::array<float, 4>> postValues;
  TextureHandle postDepth;
  std::uint16_t postView = 0;
  bool configureView2D(const View2DConfig &, std::string &) override {
    return true;
  }
  bool configureView3D(const View3DConfig &, std::string &) override {
    return true;
  }
  bool submit(const TransientDraw &draw, std::string &) override {
    postValues.clear();
    for (const auto &uniform : draw.uniforms) {
      std::array<float, 4> value;
      assert(uniform.values.size() == value.size());
      std::copy(uniform.values.begin(), uniform.values.end(), value.begin());
      postValues.push_back(value);
    }
    assert(draw.textures.size() == 1 && draw.textures[0].stage == 1);
    postDepth = draw.textures[0].texture;
    postView = draw.viewId;
    return true;
  }
  bool submit(const InstancedBufferedDraw &, std::string &) override {
    return false;
  }
  bool submit(const BufferedDraw &draw, std::string &) override {
    Draw capture{.vertices = draw.vertices,
                 .indices = draw.indices,
                 .state = draw.state,
                 .transform = draw.transform};
    assert(draw.texture && draw.sampler);
    capture.textures[0] = draw.texture;
    assert(draw.textures.size() == capture.textures.size() - 1);
    assert(draw.uniforms.size() == capture.values.size());
    for (std::size_t index = 0; index < draw.textures.size(); ++index) {
      const auto &binding = draw.textures[index];
      assert(binding.stage == index + 1 && binding.sampler);
      capture.textures[binding.stage] = binding.texture;
    }
    for (std::size_t index = 0; index < capture.values.size(); ++index) {
      assert(draw.uniforms[index].values.size() == 4);
      std::copy(draw.uniforms[index].values.begin(),
                draw.uniforms[index].values.end(),
                capture.values[index].begin());
    }
    draws.push_back(capture);
    return true;
  }
};

Entity preparedWater() {
  auto source = std::make_shared<HeightField>();
  auto artifacts = std::make_shared<TerrainGraphArtifacts>();
  TerrainWaterBodySpec body;
  body.id = "lake";
  body.kind = TerrainWaterBody::Lake;
  artifacts->water.bodies.push_back(body);
  TerrainWaterSurface surface;
  surface.id = body.id;
  surface.kind = body.kind;
  surface.vertices = {{0, 0, 0}, {0, 0, 2}, {2, 0, 0}};
  surface.normals = {{0, 1, 0}, {0, 1, 0}, {0, 1, 0}};
  surface.depth = {0, 2, 2};
  surface.indices = {0, 1, 2};
  artifacts->waterResult.emplace();
  artifacts->waterResult->surfaces.push_back(surface);
  source->graphArtifacts = artifacts;
  Entity entity;
  entity.id = "terrain/__water/lake";
  MeshRendererComponent mesh;
  mesh.vertices = surface.vertices;
  mesh.normals = surface.normals;
  entity.setComponent(std::move(mesh));
  entity.setComponent(terrain_detail::TerrainGeneratedWaterSurface{
      .owner = "terrain",
      .bodyId = body.id,
      .appearance = body.appearance,
      .sourceField = source});
  return entity;
}

void capturedWaterRetainsGeometryAndPassState(GpuResources &resources) {
  CaptureCommands commands;
  WaterSurfaceRenderer3D renderer(resources, commands);
  auto entity = preparedWater();
  VisibleMesh3D visible;
  visible.entity = &entity;
  visible.transform.position = {10, 3, 20};
  visible.transform.scale = {2, 3, 4};
  std::array surfaces{visible};
  BgfxCameraFrame3D frame;
  frame.position = {12, 9, 24};
  frame.forward = {0, -1, -1};
  frame.camera.farClip = 500;
  WaterSceneCapture3D capture{.color = {.index = 100},
                              .linearDepth = {.index = 101},
                              .width = 800,
                              .height = 600};
  std::string error;
  renderer.beginPresentation();
  assert(renderer.draw(surfaces, frame, capture, {}, {}, 2, error));
  assert(commands.draws.size() == 1);
  const auto before = commands.draws.back();
  assert(before.vertices.count == 3 && before.indices.count == 3);
  assert(before.state.blend == BlendMode::Opaque);
  assert(before.state.depthTest == DepthTest::Less && !before.state.writeDepth);
  assert(before.state.multisampling && before.state.cull == CullMode::None);
  assert(before.textures[0] == capture.color &&
         before.textures[1] == capture.linearDepth);
  assert(before.transform[12] == 10 && before.transform[13] == 3 &&
         before.transform[14] == 20);
  assert(before.values[6][0] == 1 && before.values[6][1] == 2 &&
         before.values[6][2] == 1);
  assert(before.values[1][3] == 500 && before.values[11][2] == 2);
  renderer.endPresentation();
  renderer.beginPresentation();
  entity.component<terrain_detail::TerrainGeneratedWaterSurface>()
      ->appearance.waveAmplitude = 0.2F;
  capture.width = 1600;
  assert(renderer.draw(surfaces, frame, capture, {}, {}, 4, error));
  const auto after = commands.draws.back();
  assert(after.vertices.handle == before.vertices.handle &&
         after.indices.handle == before.indices.handle);
  assert(after.values[10][0] == 0.2F && after.values[11][2] == 4);
  assert(after.values[4][0] == 1.0F / 1600);
  assert(renderer.statistics().draws == 1 &&
         renderer.statistics().triangles == 1);
  renderer.endPresentation();
  const auto draws = commands.draws.size();
  capture.linearDepth = {};
  assert(!renderer.draw(surfaces, frame, capture, {}, {}, 5, error));
  assert(commands.draws.size() == draws);
}

void underwaterPostProcessingHasExplicitViewsAndNeutralAboveWaterState(
    GpuResources &resources) {
  CaptureCommands commands;
  PostProcessRenderer3D post(resources, commands);
  std::string error;
  assert(post.initialize(error));
  BgfxCameraFrame3D frame;
  frame.viewId = 7;
  frame.viewportWidth = 800;
  frame.viewportHeight = 600;
  const TextureHandle source{.index = 100};
  assert(post.present(frame, source, {}, error));
  assert(commands.postView == 9 && commands.postValues.size() == 13);
  assert(commands.postValues[4][0] == 0);
  assert(commands.postDepth == source);
  frame.underwater = UnderwaterAppearance3D{
      .color = {0.1F, 0.2F, 0.3F, 0.8F},
      .absorptionDistance = 5,
      .immersion = 0.5F,
      .surfaceRelative = {0, 2, 0},
      .normal = {0, 1, 0},
      .inverseBasis = {Vec3{1, 0, 0}, Vec3{0, 1, 0}, Vec3{0, 0, 1}},
      .linearDepth = {.index = 101}};
  assert(post.present(frame, source, {}, error, 3));
  assert(commands.postView == 3 &&
         commands.postDepth == frame.underwater->linearDepth);
  assert(commands.postValues[4][0] == 1 && commands.postValues[4][1] == 0.2F);
  assert(commands.postValues[4][2] == 0.5F);
  assert(commands.postValues[6][3] == 2);
  frame.underwater.reset();
  assert(post.present(frame, source, {}, error, 0));
  assert(commands.postView == 0 && commands.postValues[4][0] == 0);
  assert(commands.postDepth == source);
}

} // namespace

int main() {
  opticsRespectDepthAndGrazingAngles();
  contactCoverageUsesOnlyThePixelDepthFootprint();
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(
      {.api = GraphicsApi::Noop, .width = 64, .height = 64, .vsync = false},
      error));
  {
    auto resources = createBgfxGpuResources();
    capturedWaterRetainsGeometryAndPassState(*resources);
    underwaterPostProcessingHasExplicitViewsAndNeutralAboveWaterState(
        *resources);
  }
  graphics.shutdown();
  std::cout << "Water optics and captured surface rendering checks passed\n";
}

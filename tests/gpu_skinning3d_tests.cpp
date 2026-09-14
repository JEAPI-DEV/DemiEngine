#include "demi/assets/GltfSkinnedModel.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <cassert>
#include <cmath>
#include <filesystem>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {
Vec3 point(const std::array<float, 16> &m, Vec3 p) {
  return {m[0] * p.x + m[4] * p.y + m[8] * p.z + m[12],
          m[1] * p.x + m[5] * p.y + m[9] * p.z + m[13],
          m[2] * p.x + m[6] * p.y + m[10] * p.z + m[14]};
}
void verifyPositions(const demi::assets::GltfSkinnedModel3D &model, int clip,
                     float time, bool loop) {
  std::vector<GpuSkinnedVertex3D> encoded;
  std::string error;
  assert(buildGpuSkinVertices(model, encoded, error));
  demi::assets::GltfSkinnedModel3D::Pose pose;
  std::vector<Vec3> reference;
  assert(model.samplePose(clip, time, loop, pose, error));
  assert(model.samplePositions(clip, time, loop, reference, error));
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    const auto &v = encoded[i];
    Vec3 sum{};
    for (int j = 0; j < 4; ++j) {
      assert(v.joints[j] >= 0 && v.joints[j] < pose.skins[0].size());
      auto p = point(pose.skins[0][static_cast<std::size_t>(v.joints[j])],
                     {v.vertex.x, v.vertex.y, v.vertex.z});
      sum.x += p.x * v.weights[j];
      sum.y += p.y * v.weights[j];
      sum.z += p.z * v.weights[j];
    }
    const auto p = point(model.importTransform, sum);
    assert(std::abs(p.x - reference[i].x) < 0.00002F);
    assert(std::abs(p.y - reference[i].y) < 0.00002F);
    assert(std::abs(p.z - reference[i].z) < 0.00002F);
  }
}
double gauge(const char *name) {
  for (const auto &e : RuntimeProfiler::frameEntries())
    if (e.name == name)
      return e.gauge;
  return -1;
}
int calls(const char *name) {
  for (const auto &e : RuntimeProfiler::frameEntries())
    if (e.name == name)
      return e.calls;
  return 0;
}
} // namespace

int main() {
  static_assert(sizeof(GpuSkinnedVertex3D) == 68);
  const auto path =
      std::filesystem::path(DEMI_SOURCE_DIR) /
      "examples/animation_3d/assets/AnimationLib/UAL1_Standard.glb";
  std::string error;
  auto model = demi::assets::loadGltfSkinnedModel3D(path, error);
  assert(model);
  const int clip = model->clipIndex("Walk_Loop");
  for (bool loop : {false, true})
    for (float time : {0.F, .3F, 2.F, 10.F})
      verifyPositions(*model, clip, time, loop);
  // Nonuniform import scale/translation and non-unit weights must match the CPU
  // xyz sum followed by import transform (not a homogeneous divide by weight).
  model->importTransform[0] = -2;
  model->importTransform[5] = 3;
  model->importTransform[12] = 7;
  for (auto &v : model->vertices)
    for (auto &w : v.weights)
      w *= 1.5F;
  verifyPositions(*model, clip, .4F, true);
  std::vector<GpuSkinnedVertex3D> vertices;
  auto bad = *model;
  bad.skins.push_back(bad.skins[0]);
  assert(!buildGpuSkinVertices(bad, vertices, error));
  bad = *model;
  bad.vertices[0].normal = {};
  assert(!buildGpuSkinVertices(bad, vertices, error));
  bad = *model;
  bad.vertices[0].weights = {};
  assert(!buildGpuSkinVertices(bad, vertices, error));
  bad = *model;
  bad.vertices[0].weights = {1, 0, 0, 0};
  bad.vertices[0].joints = {0, 65535, 65535, 65535};
  assert(buildGpuSkinVertices(bad, vertices, error));
  assert(vertices[0].joints[1] == 0);
  bad.vertices[0].joints[0] = 65535;
  assert(!buildGpuSkinVertices(bad, vertices, error));
  bad = *model;
  bad.skins[0].joints.resize(129);
  assert(!buildGpuSkinVertices(bad, vertices, error));

  BgfxGraphicsDevice device;
  assert(device.initialize(
      {.api = GraphicsApi::Noop, .width = 320, .height = 180}, error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  {
    BgfxRenderer3D renderer(*resources, *commands);
    assert(renderer.initialize(error));
    demi::AssetRegistry registry;
    registry.assets.push_back(
        {.id = "asset://character", .type = "Model3D", .sourcePath = path});
    std::vector<std::string> diagnostics;
    assert(renderer.loadAssets(registry, diagnostics));
    World world;
    Entity character;
    character.id = "character";
    character.setComponent(Transform3DComponent{});
    character.setComponent(MeshRendererComponent{.model = "asset://character"});
    character.setComponent(
        AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = .3F});
    world.entities.push_back(std::move(character));
    BgfxCameraFrame3D frame;
    frame.position = {0, 1, 5};
    frame.forward = {0, 0, -1};
    frame.viewportWidth = 320;
    frame.viewportHeight = 180;
    RuntimeProfiler::setEnabled(true);
    const auto render = [&] {
      RuntimeProfiler::beginFrame();
      assert(renderer.renderFrame(world, frame, .016F, error));
      static_cast<void>(device.endFrame());
    };
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 1);
    assert(calls("Renderer3D.skin_palette_cpu") == 1);
    assert(calls("Renderer3D.mesh_vertices_cpu") == 0);
    render();
    assert(calls("Renderer3D.animation_rebuild") == 0);
    auto *player = world.entities[0].component<AnimationPlayer3DComponent>();
    player->time = .5F;
    render();
    assert(calls("Renderer3D.skin_palette_cpu") == 1);
    // Unsupported blend/procedural paths remain real CPU deformation.
    player->blendWeight = .5F;
    render();
    assert(gauge("Renderer3D.cpu_skinned_meshes") == 1);
    assert(calls("Renderer3D.mesh_vertices_cpu") == 1);
    player->blendWeight = 1;
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 1);
    assert(renderer.loadAssets(registry, diagnostics));
    render();
    assert(calls("Renderer3D.skin_palette_cpu") == 1);
    world.entities.clear();
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 0);
    for (int i = 0; i < 260; ++i) {
      Entity peer;
      peer.id = "peer_" + std::to_string(i);
      peer.setComponent(Transform3DComponent{});
      peer.setComponent(MeshRendererComponent{.model = "asset://character"});
      peer.setComponent(AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = float(i) * .001F});
      world.entities.push_back(std::move(peer));
    }
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 260);
    assert(gauge("Renderer3D.animation_batch_meshes") == 128);
    assert(gauge("Renderer3D.animation_batch_vertices") == 0);
    for (auto &peer : world.entities) peer.component<AnimationPlayer3DComponent>()->time += .1F;
    world.entities[17].component<AnimationPlayer3DComponent>()->blendWeight = .5F;
    world.entities[145].component<AnimationPlayer3DComponent>()->blendWeight = .5F;
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 258);
    assert(gauge("Renderer3D.cpu_skinned_meshes") == 2);
    assert(calls("Renderer3D.animation_rebuild") == 260);
    render();
    assert(calls("Renderer3D.animation_rebuild") == 0);
    RuntimeProfiler::setEnabled(false);
  }
  commands.reset();
  resources.reset();
  device.shutdown();
}

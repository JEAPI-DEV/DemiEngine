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
#include <limits>

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
  GpuSkinPaletteLayout layout;
  std::string error;
  assert(buildGpuSkinVertices(model, encoded, layout, error));
  demi::assets::GltfSkinnedModel3D::Pose pose;
  std::vector<Vec3> reference;
  assert(model.samplePose(clip, time, loop, pose, error));
  assert(model.samplePositions(clip, time, loop, reference, error));
  std::vector<float> packed;
  assert(packGpuSkinPalette(layout, pose, packed, error));
  for (std::size_t i = 0; i < encoded.size(); ++i) {
    const auto &v = encoded[i];
    Vec3 sum{};
    for (int j = 0; j < 4; ++j) {
      assert(v.joints[j] >= 0 && v.joints[j] < layout.rows.size());
      std::array<float, 16> matrix;
      std::copy_n(packed.begin() + static_cast<std::size_t>(v.joints[j]) * 16,
                  16, matrix.begin());
      auto p = point(matrix, {v.vertex.x, v.vertex.y, v.vertex.z});
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
  GpuSkinPaletteLayout layout;
  auto bad = *model;
  bad.skins.push_back(bad.skins[0]);
  assert(buildGpuSkinVertices(bad, vertices, layout,
                              error)); // Unused skins cost no rows.
  bad = *model;
  bad.vertices[0].normal = {};
  assert(!buildGpuSkinVertices(bad, vertices, layout, error));
  bad = *model;
  bad.vertices[0].weights = {};
  assert(buildGpuSkinVertices(bad, vertices, layout, error));
  verifyPositions(bad, clip, .4F,
                  true); // Zero weights preserve local positions.
  bad = *model;
  bad.vertices[0].weights = {1, 0, 0, 0};
  bad.vertices[0].joints = {0, 65535, 65535, 65535};
  assert(buildGpuSkinVertices(bad, vertices, layout, error));
  assert(vertices[0].joints[1] == 0);
  bad.vertices[0].joints[0] = 65535;
  assert(!buildGpuSkinVertices(bad, vertices, layout, error));
  bad = *model;
  bad.skins[0].joints.resize(129);
  assert(!buildGpuSkinVertices(bad, vertices, layout, error));

  // Two skins may use joint slot zero for different nodes/inverse binds.
  constexpr std::array<float, 16> Identity{1, 0, 0, 0, 0, 1, 0, 0,
                                           0, 0, 1, 0, 0, 0, 0, 1};
  demi::assets::GltfSkinnedModel3D mixed;
  mixed.nodes.resize(3);
  mixed.nodes[2].parent = 1;
  mixed.nodes[2].translation = {0, 0, 1};
  auto inverse = Identity;
  inverse[12] = -1.5F;
  mixed.skins.push_back(
      {.joints = {0, 1}, .inverseBindMatrices = {Identity, Identity}});
  mixed.skins.push_back({.joints = {1}, .inverseBindMatrices = {inverse}});
  using Path = demi::assets::GltfSkinnedModel3D::ChannelPath;
  mixed.clips.push_back(
      {.name = "move",
       .duration = 1,
       .channels = {{.node = 0,
                     .path = Path::Translation,
                     .times = {0, 1},
                     .values = {{0, 0, 0, 0}, {2, 0, 0, 0}}},
                    {.node = 1,
                     .path = Path::Translation,
                     .times = {0, 1},
                     .values = {{0, 0, 0, 0}, {0, 3, 0, 0}}}}});
  mixed.vertices.resize(6);
  for (std::size_t i = 0; i < 6; ++i) {
    mixed.vertices[i].position = {float(i % 3), 0, 0};
    mixed.vertices[i].normal = {0, 0, 1};
    mixed.vertices[i].node = 2;
  }
  mixed.vertices[0].skin = 0;
  mixed.vertices[0].joints = {0, 1, 0, 0};
  mixed.vertices[0].weights = {.75F, .25F, 0, 0};
  mixed.vertices[1].skin = 1;
  mixed.vertices[1].weights = {1.5F, 0, 0, 0};
  mixed.vertices[3].skin =
      0; // Unweighted skin ignores its owner node, unlike vertex 2.
  mixed.vertices[4].node = -1;
  mixed.indices = {0, 1, 2, 3, 4, 5};
  mixed.importTransform[0] = -2;
  mixed.importTransform[5] = 3;
  mixed.importTransform[12] = 7;
  for (bool loop : {false, true})
    for (float time : {0.F, .4F, 1.5F})
      verifyPositions(mixed, 0, time, loop);
  assert(buildGpuSkinVertices(mixed, vertices, layout, error));
  assert(layout.rows.size() == 5);
  auto rigidOnly = mixed;
  rigidOnly.skins.clear();
  for (auto &vertex : rigidOnly.vertices) vertex.skin = -1;
  verifyPositions(rigidOnly, 0, .4F, true);
  assert(buildGpuSkinVertices(rigidOnly, vertices, layout, error));
  assert(layout.rows.size() == 2); // Owner node plus shared identity.
  assert(buildGpuSkinVertices(mixed, vertices, layout, error));
  const auto stableRows = layout.rows;
  assert(buildGpuSkinVertices(mixed, vertices, layout, error));
  for (std::size_t i = 0; i < layout.rows.size(); ++i) {
    assert(layout.rows[i].kind == stableRows[i].kind &&
           layout.rows[i].skin == stableRows[i].skin &&
           layout.rows[i].index == stableRows[i].index);
  }
  demi::assets::GltfSkinnedModel3D::Pose pose;
  std::vector<float> packed;
  assert(mixed.samplePose(0, .4F, true, pose, error));
  assert(packGpuSkinPalette(layout, pose, packed, error));
  auto brokenPose = pose;
  brokenPose.nodes.clear();
  assert(!packGpuSkinPalette(layout, brokenPose, packed, error) &&
         packed.empty());
  brokenPose = pose;
  brokenPose.skins[1][0][0] = std::numeric_limits<float>::infinity();
  assert(!packGpuSkinPalette(layout, brokenPose, packed, error) &&
         packed.empty());
  auto invalid = mixed;
  invalid.vertices[2].node = 999;
  assert(!buildGpuSkinVertices(invalid, vertices, layout, error));
  assert(vertices.empty() && layout.rows.empty());
  invalid = mixed;
  invalid.vertices[0].skin = 999;
  assert(!buildGpuSkinVertices(invalid, vertices, layout, error));
  // The cap applies to all actually referenced transforms, not each skin.
  auto limit = mixed;
  limit.nodes.resize(129);
  limit.vertices.resize(129);
  limit.indices.clear();
  for (std::size_t i = 0; i < 129; ++i) {
    limit.vertices[i].skin = -1;
    limit.vertices[i].node = static_cast<int>(i);
    limit.vertices[i].normal = {0, 0, 1};
    limit.indices.push_back(static_cast<std::uint32_t>(i));
  }
  assert(!buildGpuSkinVertices(limit, vertices, layout, error));
  limit.vertices.back().node = 0;
  assert(buildGpuSkinVertices(limit, vertices, layout, error) &&
         layout.rows.size() == 128);

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
    registry.assets.push_back(
        {.id = "asset://lod", .type = "Model3D", .sourcePath = path});
    registry.assets.push_back({.id = "asset://lod_static", .type = "Model3D",
        .settingsJson = R"({"model_import":{"format_version":1,"preset":"animated_character","import_animations":false}})",
        .sourcePath = path});
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
      peer.setComponent(AnimationPlayer3DComponent{.clipName = "Walk_Loop",
                                                   .time = float(i) * .001F});
      world.entities.push_back(std::move(peer));
    }
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 260);
    assert(gauge("Renderer3D.animation_batch_meshes") == 128);
    assert(gauge("Renderer3D.animation_batch_vertices") == 0);
    for (auto &peer : world.entities)
      peer.component<AnimationPlayer3DComponent>()->time += .1F;
    world.entities[17].component<AnimationPlayer3DComponent>()->blendWeight =
        .5F;
    world.entities[145].component<AnimationPlayer3DComponent>()->blendWeight =
        .5F;
    render();
    assert(gauge("Renderer3D.gpu_skinned_meshes") == 258);
    assert(gauge("Renderer3D.cpu_skinned_meshes") == 2);
    assert(calls("Renderer3D.animation_rebuild") == 260);
    render();
    assert(calls("Renderer3D.animation_rebuild") == 0);
    world.entities.clear(); render();
    Entity budgeted;
    budgeted.id="budgeted";
    budgeted.setComponent(Transform3DComponent{});
    budgeted.setComponent(MeshRendererComponent{.model="asset://character"});
    AnimationPlayer3DComponent budgetPlayer;
    budgetPlayer.clipName="Walk_Loop";
    budgetPlayer.visualUpdateRate=15;
    budgetPlayer.visualUpdateDistance=2;
    budgeted.setComponent(std::move(budgetPlayer));
    world.entities.push_back(std::move(budgeted));
    render();
    auto *paced=world.entities[0].component<AnimationPlayer3DComponent>();
    int poseUpdates=0;
    for(int i=1;i<=240;++i) {
      paced->visualClock=double(i)/240; paced->time=float(i)/120;
      render(); poseUpdates+=calls("Renderer3D.animation_rebuild");
      assert(paced->time==float(i)/120); // Renderer may not change the timeline.
    }
    assert(poseUpdates==15);
    frame.position={0,1,1}; // Near camera bypasses the far visual rate.
    for(int i=0;i<12;++i) {
      paced->visualClock+=1.0/240; paced->time+=1.0F/120;
      render(); assert(calls("Renderer3D.animation_rebuild")==1);
    }
    auto *lodMesh=world.entities[0].component<MeshRendererComponent>();
    lodMesh->lowLodModel="asset://lod"; lodMesh->lowLodDistance=4;
    render(); assert(renderer.statistics().lowLodMeshes==0);
    frame.position={0,1,5};
    render();
    assert(renderer.statistics().lowLodMeshes==1);
    assert(calls("Renderer3D.animation_rebuild")==1); // LOD switch bypasses cadence.
    assert(lodMesh->model=="asset://character"); // Render choice never rewrites source.
    render(); assert(calls("Renderer3D.animation_rebuild")==0);
    frame.position={0,1,1};
    render(); assert(renderer.statistics().lowLodMeshes==0);
    assert(calls("Renderer3D.animation_rebuild")==1);
    lodMesh->lowLodModel="asset://lod_static";
    frame.position={0,1,5};
    render(); assert(renderer.statistics().lowLodMeshes==0); // Missing clip cannot freeze the actor.
    lodMesh->lowLodModel="asset://lod";
    paced->blendWeight=.5F;
    render(); assert(renderer.statistics().lowLodMeshes==0);
    assert(gauge("Renderer3D.cpu_skinned_meshes")==1);
    RuntimeProfiler::setEnabled(false);
  }
  commands.reset();
  resources.reset();
  device.shutdown();
}

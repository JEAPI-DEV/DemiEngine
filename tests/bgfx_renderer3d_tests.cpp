#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Environment3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <algorithm>
#ifdef NDEBUG
#undef NDEBUG
#endif
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

class TextureCapture final : public RenderCommands {
public:
  explicit TextureCapture(RenderCommands &target) : target_(target) {}
  std::vector<TextureHandle> textures;
  std::vector<DrawState> bufferedStates;
  std::vector<std::array<float, 16>> bufferedTransforms;
  std::vector<View2DConfig> views2D;
  std::vector<View3DConfig> views3D;
  bool configureView2D(const View2DConfig &view, std::string &error) override {
    views2D.push_back(view);
    return target_.configureView2D(view, error);
  }
  bool configureView3D(const View3DConfig &view, std::string &error) override {
    views3D.push_back(view);
    return target_.configureView3D(view, error);
  }
  bool submit(const TransientDraw &draw, std::string &error) override {
    return target_.submit(draw, error);
  }
  bool submit(const BufferedDraw &draw, std::string &error) override {
    textures.push_back(draw.texture);
    bufferedStates.push_back(draw.state);
    bufferedTransforms.push_back(draw.transform);
    return target_.submit(draw, error);
  }
  bool submit(const InstancedBufferedDraw &draw, std::string &error) override {
    textures.push_back(draw.texture);
    return target_.submit(draw, error);
  }
private:
  RenderCommands &target_;
};

Entity shape(std::string id, std::string shapeName, const Vec3 position) {
  Entity entity;
  entity.id = std::move(id);
  entity.setComponent(Transform3DComponent{.position = position});
  entity.setComponent(MeshRendererComponent{.shape = std::move(shapeName),
                                            .size = {1.0F, 1.0F, 1.0F},
                                            .color = {0.3F, 0.7F, 0.9F, 1.0F}});
  return entity;
}

} // namespace

int main() {
  BgfxGraphicsDevice graphics;
  std::string error;
  assert(graphics.initialize(GraphicsDeviceConfig{.api = GraphicsApi::Noop,
                                                  .width = 320,
                                                  .height = 180,
                                                  .vsync = false},
                             error));
  auto resources = createBgfxGpuResources();
  auto commands = createBgfxRenderCommands(*resources);
  TextureCapture capture(*commands);
  BgfxRenderer3D renderer(*resources, capture, false); // CPU fallback reference.
  assert(renderer.initialize(error));
  assert(renderer.initialize(error));
  std::vector<std::string> diagnostics;
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://textures/override", .type = "Texture2D",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_3d/assets/textures/checker.png"});
  registry.assets.push_back(
      {.id = "asset://models/animated",
       .type = "Model3D",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/animation_3d/assets/AnimationLib/"
                     "UAL1_Standard.glb"});
  for (const std::string id :
       {"asset://models/lod_high", "asset://models/lod_medium",
        "asset://models/lod_low"}) {
    registry.assets.push_back(
        {.id = id,
         .type = "Model3D",
         .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                       "examples/animation_3d/assets/AnimationLib/"
                       "UAL1_Standard.glb"});
  }
  registry.assets.push_back(
      {.id = "asset://targets/test",
       .type = "RenderTarget",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_voxel/assets/targets/"
                     "voxel_minimap.target.json"});
  assert(renderer.loadAssets(registry, diagnostics));

  World world;
  world.ui.canvasSize = {320.0F, 180.0F};
  world.ui.nodes.push_back({.id = "status",
                            .type = "label",
                            .text = "BGFX 3D",
                            .resolved = {4.0F, 4.0F, 120.0F, 24.0F},
                            .fontSize = 18.0F});
  Entity parent = shape("parent", "cube", {0.0F, 0.0F, 0.0F});
  world.entities.push_back(std::move(parent));
  Entity child = shape("child", "sphere", {1.0F, 0.0F, 0.0F});
  child.component<Transform3DComponent>()->parent = "parent";
  world.entities.push_back(std::move(child));
  world.entities.push_back(shape("plane", "plane", {0.0F, -1.0F, 0.0F}));
  world.entities.push_back(shape("cylinder", "cylinder", {-1.0F, 0.0F, 0.0F}));
  Entity procedural = shape("procedural", "", {0.0F, 1.0F, 0.0F});
  procedural.component<MeshRendererComponent>()->vertices = {
      {-0.5F, 0.0F, 0.0F}, {0.5F, 0.0F, 0.0F}, {0.0F, 1.0F, 0.0F}};
  procedural.component<MeshRendererComponent>()->uvs = {
      {0.0F, 0.0F}, {1.0F, 0.0F}, {0.5F, 1.0F}};
  world.entities.push_back(std::move(procedural));
  Entity lodModel;
  lodModel.id = "lod_model";
  lodModel.setComponent(Transform3DComponent{});
  lodModel.setComponent(MeshRendererComponent{
      .model = "asset://models/lod_high",
      .mediumLodModel = "asset://models/lod_medium",
      .mediumLodDistance = 4.0F,
      .lowLodModel = "asset://models/lod_low",
      .lowLodDistance = 10.0F,
  });
  world.entities.push_back(std::move(lodModel));

  const BgfxCameraFrame3D frame{
      .camera = {.clearColor = {0.05F, 0.06F, 0.09F, 1.0F}},
      .position = {0.0F, 2.0F, 5.0F},
      .forward = {0.0F, -0.3F, -1.0F},
      .viewportWidth = 320,
      .viewportHeight = 180,
  };
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().mediumLodMeshes == 1U);
  assert(renderer.statistics().lowLodMeshes == 0U);
  assert(renderer.statistics().batches >= 1);
  assert(renderer.statistics().triangles > 12);
  const std::uint32_t batchesWithoutDebugGeometry =
      renderer.statistics().batches;
  static_cast<void>(graphics.endFrame());
  {
    World skyWorld;
    Entity environment;
    environment.id = "environment";
    Environment3DComponent::parse({{"sky_texture", "asset://textures/override"}}, environment);
    assert(environment.component<Environment3DComponent>()->skyTexture == "asset://textures/override");
    skyWorld.entities.push_back(std::move(environment));
    auto skyFrame = frame;
    skyFrame.position = {13, 2, -17};
    skyFrame.lightingOverride = SceneLighting3D{}; // Editor lighting override preserves sky.
    capture.bufferedStates.clear();
    capture.bufferedTransforms.clear();
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.size() == 1);
    assert(capture.bufferedStates.front().depthTest == DepthTest::LessEqual);
    assert(!capture.bufferedStates.front().writeDepth);
    assert(capture.bufferedTransforms.front()[12] == 13);
    assert(capture.bufferedTransforms.front()[14] == -17);
    static_cast<void>(graphics.endFrame());
    skyWorld.entities.front().enabled = false;
    capture.bufferedStates.clear();
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.empty());
    static_cast<void>(graphics.endFrame());
    skyWorld.entities.front().enabled = true;
    skyFrame.camera.perspective = false;
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.empty());
    static_cast<void>(graphics.endFrame());
    skyFrame.camera.perspective = true;
    skyFrame.camera.clearMode = "depth";
    assert(renderer.renderFrame(skyWorld, skyFrame, 0.016F, error));
    assert(capture.bufferedStates.empty());
    static_cast<void>(graphics.endFrame());
  }
  for (int index = 0; index < 64; ++index) {
    world.entities.push_back(shape("sphere_copy_" + std::to_string(index),
                                   "sphere", {0.0F, 0.0F, 0.0F}));
  }
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().visibleMeshes == 70U);
  assert(renderer.statistics().batches == batchesWithoutDebugGeometry);
  static_cast<void>(graphics.endFrame());
  // The second frame reuses the resident procedural buffers. Changing the
  // revision replaces them, and removing the owner releases the cache entry.
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());

  world.entities.front().setComponent(BoxCollider3DComponent{});
  world.debug.colliders = true;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().batches == batchesWithoutDebugGeometry + 1U);
  static_cast<void>(graphics.endFrame());
  world.debug.colliders = false;

  Entity emitter;
  emitter.id = "particles";
  emitter.setComponent(Transform3DComponent{});
  emitter.setComponent(ParticleEmitter3DComponent{.burst = 3,
                                                  .lifetime = 2.0F,
                                                  .sizeStart = 0.5F,
                                                  .sizeEnd = 0.25F,
                                                  .playing = true,
                                                  .loop = false});
  world.entities.push_back(std::move(emitter));
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().particles == 3);
  assert(renderer.statistics().triangles >= 6);
  static_cast<void>(graphics.endFrame());

  BgfxCameraFrame3D postProcessFrame = frame;
  postProcessFrame.postProcess =
      PostProcessStackComponent{.exposure = 0.5F,
                                .contrast = 1.2F,
                                .saturation = 0.7F,
                                .tint = {0.9F, 0.8F, 1.0F, 1.0F},
                                .vignette = 0.4F,
                                .bloom = 0.5F,
                                .bloomThreshold = 0.8F,
                                .fadeColor = {0.1F, 0.2F, 0.3F, 0.8F},
                                .fade = 0.25F};
  assert(renderer.renderFrame(world, postProcessFrame, 0.016F, error));
  assert(renderer.statistics().batches >= 2);
  static_cast<void>(graphics.endFrame());

  const RenderTargetHandles embeddedTarget = resources->createRenderTarget(
      {.width = 320, .height = 180, .debugName = "embedded 3D test"}, error);
  assert(embeddedTarget.frameBuffer && embeddedTarget.color);
  BgfxCameraFrame3D embeddedFrame = postProcessFrame;
  embeddedFrame.frameBuffer = embeddedTarget.frameBuffer;
  embeddedFrame.camera.renderHudToTarget = true;
  assert(renderer.renderFrame(world, embeddedFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  for (const float scale : {0.8F, 1.5F}) {
    embeddedFrame.postProcess.reset();
    embeddedFrame.camera.renderScale = scale;
    capture.views2D.clear();
    capture.views3D.clear();
    assert(renderer.renderFrame(world, embeddedFrame, 0.016F, error));
    assert(capture.views3D.front().width == std::lround(320 * scale));
    assert(capture.views3D.front().frameBuffer != embeddedTarget.frameBuffer);
    const auto present = std::ranges::find_if(capture.views2D, [&](const auto &view) {
      return view.id == embeddedFrame.viewId + 2;
    });
    assert(present != capture.views2D.end());
    assert(present->frameBuffer == embeddedTarget.frameBuffer);
    assert(present->width == 320 && present->height == 180);
    assert(present->x == 0 && present->y == 0);
    static_cast<void>(graphics.endFrame());
  }
  assert(resources->destroy(embeddedTarget.frameBuffer));
  assert(resources->destroy(embeddedTarget.depth));
  assert(resources->destroy(embeddedTarget.color));

  BgfxCameraFrame3D targetFrame = frame;
  targetFrame.cameraId = "minimap";
  targetFrame.camera.renderTarget = "asset://targets/test";
  targetFrame.camera.renderHudToTarget = true;
  assert(renderer.renderFrame(world, targetFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  // A throttled target keeps presenting its resident color attachment without
  // clearing or redrawing the world.
  targetFrame.updateContent = false;
  assert(renderer.renderFrame(world, targetFrame, 0.016F, error));
  static_cast<void>(graphics.endFrame());

  BgfxCameraFrame3D missingTargetFrame = targetFrame;
  missingTargetFrame.camera.renderTarget = "asset://targets/missing";
  assert(!renderer.renderFrame(world, missingTargetFrame, 0.016F, error));
  assert(error.find("unloaded render target") != std::string::npos);

  // Reloading an empty registry releases every target and removes its UI
  // texture registration. A former target must not remain renderable through a
  // stale GPU handle.
  demi::AssetRegistry emptyRegistry;
  diagnostics.clear();
  assert(renderer.loadAssets(emptyRegistry, diagnostics));
  assert(!renderer.renderFrame(world, targetFrame, 0.016F, error));
  assert(error.find("unloaded render target") != std::string::npos);

  diagnostics.clear();
  demi::AssetRegistry invalidTargetRegistry;
  invalidTargetRegistry.assets.push_back(
      {.id = "asset://targets/invalid",
       .type = "RenderTarget",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/minimal_voxel/assets/targets/missing.json"});
  assert(!renderer.loadAssets(invalidTargetRegistry, diagnostics));
  assert(!diagnostics.empty());

  // Restore model assets before exercising skeletal animation below.
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));
  const auto proceduralEntity =
      std::ranges::find_if(world.entities, [](const Entity &entity) {
        return entity.id == "procedural";
      });
  assert(proceduralEntity != world.entities.end());
  proceduralEntity->component<MeshRendererComponent>()->revision = 2;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  std::erase_if(world.entities,
                [](const Entity &entity) { return entity.id == "procedural"; });
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());

  Entity animated = shape("model", "", {});
  animated.component<MeshRendererComponent>()->model =
      "asset://models/animated";
  animated.setComponent(
      AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = 0.2F});
  world.entities.push_back(std::move(animated));
  diagnostics.clear();
  assert(diagnostics.empty());
  RuntimeProfiler::setEnabled(true);
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto rebuilt = RuntimeProfiler::frameEntries();
  for (const std::string name :
       {"Renderer3D.animation_rebuild", "Renderer3D.skin_cpu",
        "Renderer3D.skin_upload_cpu"}) {
    assert(std::any_of(rebuilt.begin(), rebuilt.end(), [&](const auto &entry) {
      return entry.name == name && entry.calls == 1;
    }));
  }
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto unchanged = RuntimeProfiler::frameEntries();
  assert(
      std::none_of(unchanged.begin(), unchanged.end(), [](const auto &entry) {
        return entry.name == "Renderer3D.animation_rebuild" && entry.calls > 0;
      }));
  world.entities.back().component<AnimationPlayer3DComponent>()->time = 0.4F;
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto advanced = RuntimeProfiler::frameEntries();
  assert(std::any_of(advanced.begin(), advanced.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 1;
  }));
  // More than two preparation batches, followed by a cached frozen frame.
  for (int i = 0; i < 40; ++i) {
    Entity peer = shape("peer_" + std::to_string(i), "", {});
    peer.component<MeshRendererComponent>()->model = "asset://models/animated";
    peer.setComponent(AnimationPlayer3DComponent{.clipName = "Walk_Loop", .time = float(i) * 0.01F});
    world.entities.push_back(std::move(peer));
  }
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto crowd = RuntimeProfiler::frameEntries();
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 40;
  }));
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_batch_meshes" && entry.gauge == 16;
  }));
  assert(std::any_of(crowd.begin(), crowd.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_batch_vertices" && entry.gauge > 0 && entry.gauge <= 262144;
  }));
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto frozen = RuntimeProfiler::frameEntries();
  assert(std::none_of(frozen.begin(), frozen.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls > 0;
  }));
  // Loop policy is part of the pose key even if time is unchanged.
  world.entities.back().component<AnimationPlayer3DComponent>()->loop = false;
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto loopChanged = RuntimeProfiler::frameEntries();
  assert(std::any_of(loopChanged.begin(), loopChanged.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 1;
  }));
  auto *badPose = world.entities.back().component<AnimationPlayer3DComponent>();
  badPose->boneSegments["missing_test_bone"] = {{0, 0, 0}, {0, 1, 0}, {0, 0, 1}};
  ++badPose->proceduralPoseRevision;
  assert(!renderer.renderFrame(world, frame, 0.016F, error));
  assert(error.find("missing_test_bone") != std::string::npos);
  badPose->boneSegments.clear();
  ++badPose->proceduralPoseRevision;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  diagnostics.clear();
  assert(renderer.loadAssets(registry, diagnostics));
  RuntimeProfiler::beginFrame();
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  const auto reloaded = RuntimeProfiler::frameEntries();
  assert(std::any_of(reloaded.begin(), reloaded.end(), [](const auto &entry) {
    return entry.name == "Renderer3D.animation_rebuild" && entry.calls == 41;
  }));
  auto lightingFrame = frame;
  for (int lightingCase = 0; lightingCase < 4; ++lightingCase) {
    lightingFrame.lightingOverride = SceneLighting3D{};
    if (lightingCase == 1) {
      lightingFrame.lightingOverride->pointPositionRange[3] = 10;
      lightingFrame.lightingOverride->pointColorIntensity[3] = 1;
    } else if (lightingCase == 2) {
      lightingFrame.lightingOverride->spotPositionRange[15] = 10;
      lightingFrame.lightingOverride->spotColorIntensity[15] = 1;
    }
    RuntimeProfiler::beginFrame();
    assert(renderer.renderFrame(world, lightingFrame, 0.016F, error));
    static_cast<void>(graphics.endFrame());
    const auto entries = RuntimeProfiler::frameEntries();
    const double expected = lightingCase == 0 || lightingCase == 3 ? 1.0 : 0.0;
    assert(std::any_of(entries.begin(), entries.end(), [expected](const auto &entry) {
      return entry.name == "Renderer3D.directional_shader" && entry.gauge == expected;
    }));
  }
  RuntimeProfiler::setEnabled(false);

  World textureWorld;
  auto probe = shape("texture-probe", "cube", {});
  probe.component<MeshRendererComponent>()->texture = "asset://textures/override";
  textureWorld.entities.push_back(std::move(probe));
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size() == 1);
  const auto explicitTexture = capture.textures.front();
  auto *mesh = textureWorld.entities.front().component<MeshRendererComponent>();
  mesh->texture.clear();
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld, frame, .016F, error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size() == 1 && capture.textures.front() != explicitTexture);
  // Static models go through instancing; animated ones use buffered submission.
  // Both must honor the same explicit texture as a primitive, not white/embedded.
  mesh->model = "asset://models/animated";
  mesh->texture = "asset://textures/override";
  for (bool animated : {false, true}) {
    if (animated)
      textureWorld.entities.front().setComponent(AnimationPlayer3DComponent{
          .clipName = "Walk_Loop", .time = .2F});
    capture.textures.clear();
    assert(renderer.renderFrame(textureWorld, frame, .016F, error));
    static_cast<void>(graphics.endFrame());
    assert(!capture.textures.empty());
    for (const auto texture : capture.textures)
      assert(texture == explicitTexture);
  }

  // Relief geometry is generated from an image in a shared session cache,
  // not imported from a generated per-brick asset.
  textureWorld.entities.front().removeComponent<AnimationPlayer3DComponent>();
  mesh=textureWorld.entities.front().component<MeshRendererComponent>();
  mesh->model.clear();
  SurfaceRelief3DComponent relief{.heightMap="asset://textures/override"};
  textureWorld.entities.front().setComponent(relief);
  capture.textures.clear();
  assert(renderer.renderFrame(textureWorld,frame,.016F,error));
  static_cast<void>(graphics.endFrame());
  assert(capture.textures.size()==1 && capture.textures.front()==explicitTexture);
  ReliefMeshCache3D cache(*resources);
  cache.loadAssets(registry);
  const auto *generated=cache.get(relief,{1,1,.115F},error);
  assert(generated && generated->indexCount()>36 && cache.size()==1);
  assert(cache.get(relief,{2,3,.115F},error)==generated && cache.size()==1);
  auto regionRelief=relief;
  regionRelief.tiles={4,3};regionRelief.atlasGrid={8,14};
  const auto *regionMesh=cache.get(regionRelief,{4,3,.115F},error);
  assert(regionMesh && regionMesh->indexCount()==12*generated->indexCount());
  assert(cache.get(regionRelief,{8,6,.115F},error)==regionMesh);
  auto flatRegion=regionRelief;flatRegion.heightMap.clear();flatRegion.depth=0;
  assert(cache.get(flatRegion,{4,3,.115F},error)->indexCount()==12*36);
  assert(!cache.get(relief,{1,1,.001F},error));
  cache.clear();
  assert(cache.size()==0);
  cache.loadAssets(registry);
  auto tile=relief;tile.uvScale={1.F/16,1.F/16};
  for(int i=0;i<300;++i) {
    tile.uvOffset={float(i%16)/16,float(i/16)/16};
    assert(cache.get(tile,{1,1,.115F},error));
  }
  assert(cache.get(relief,{1,1,.115F},error));
  assert(cache.size()>256); // An active view can exceed the retention budget.
  cache.beginFrame();
  cache.beginFrame();
  relief.uvOffset.y=.731F;
  assert(cache.get(relief,{1,1,.115F},error) && cache.size()==256);
  cache.clear();

  renderer.shutdown();
  renderer.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}

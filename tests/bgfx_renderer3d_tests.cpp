#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/backend/BgfxGraphicsDevice.h"
#include "demi/runtime/scene/components/3dcomponents/AnimationPlayer3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/BoxCollider3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/MeshRendererComponent.h"
#include "demi/runtime/scene/components/3dcomponents/ParticleEmitter3DComponent.h"
#include "demi/runtime/scene/components/3dcomponents/Transform3DComponent.h"

#include <algorithm>
#include <cassert>
#include <filesystem>
#include <string>
#include <vector>

using namespace demi::runtime;
using namespace demi::runtime::render;

namespace {

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
  BgfxRenderer3D renderer(*resources, *commands);
  assert(renderer.initialize(error));
  assert(renderer.initialize(error));
  std::vector<std::string> diagnostics;
  demi::AssetRegistry registry;
  registry.assets.push_back(
      {.id = "asset://models/animated",
       .type = "Model3D",
       .sourcePath = std::filesystem::path(DEMI_SOURCE_DIR) /
                     "examples/animation_3d/assets/AnimationLib/"
                     "UAL1_Standard.glb"});
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

  const BgfxCameraFrame3D frame{
      .camera = {.clearColor = {0.05F, 0.06F, 0.09F, 1.0F}},
      .position = {0.0F, 2.0F, 5.0F},
      .forward = {0.0F, -0.3F, -1.0F},
      .viewportWidth = 320,
      .viewportHeight = 180,
  };
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  assert(renderer.statistics().batches >= 1);
  assert(renderer.statistics().triangles > 12);
  const std::uint32_t batchesWithoutDebugGeometry =
      renderer.statistics().batches;
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
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());
  world.entities.back().component<AnimationPlayer3DComponent>()->time = 0.4F;
  assert(renderer.renderFrame(world, frame, 0.016F, error));
  static_cast<void>(graphics.endFrame());

  renderer.shutdown();
  renderer.shutdown();
  commands.reset();
  resources.reset();
  graphics.shutdown();
  return 0;
}

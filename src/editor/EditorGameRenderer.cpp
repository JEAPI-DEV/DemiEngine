#include "editor/EditorGameRenderer.h"

#include "demi/assets/AssetRegistry.h"
#include "demi/runtime/profiling/RuntimeProfiler.h"
#include "demi/runtime/render/BgfxRenderer2D.h"
#include "demi/runtime/render/BgfxRenderer3D.h"
#include "demi/runtime/render/backend/BgfxResourceLookup.h"
#include "demi/runtime/render/backend/GpuResources.h"
#include "demi/runtime/render/backend/RenderCommands.h"
#include "demi/runtime/scene/WorldQueries.h"

#include <bgfx/bgfx.h>

#include <vector>

namespace demi::editor {

EditorGameRenderer::EditorGameRenderer(
    runtime::render::GpuResources &resources,
    runtime::render::RenderCommands &commands)
    : resources_(resources), commands_(commands),
      target_(std::make_unique<runtime::render::RenderTargetHandles>()) {}

EditorGameRenderer::~EditorGameRenderer() { release(); }

bool EditorGameRenderer::configure(
    const std::filesystem::path &projectDirectory, std::string &error) {
  release();
  const AssetRegistry assets = loadAssetRegistry(projectDirectory);
  if (hasErrors(assets.diagnostics)) {
    error = assets.diagnostics.front().message;
    return false;
  }
  renderer3D_ =
      std::make_unique<runtime::render::BgfxRenderer3D>(resources_, commands_);
  renderer2D_ =
      std::make_unique<runtime::render::BgfxRenderer2D>(resources_, commands_);
  if (!renderer3D_->initialize(error) || !renderer2D_->initialize(error)) {
    release();
    return false;
  }
  std::vector<std::string> diagnostics;
  if (!renderer3D_->loadAssets(assets, diagnostics) ||
      !renderer2D_->loadAssets(assets, diagnostics)) {
    error = diagnostics.empty() ? "Could not load Game view assets."
                                : diagnostics.front();
    release();
    return false;
  }
  return true;
}

void EditorGameRenderer::release() {
  if (renderer2D_ != nullptr)
    renderer2D_->shutdown();
  if (renderer3D_ != nullptr)
    renderer3D_->shutdown();
  renderer2D_.reset();
  renderer3D_.reset();
  destroyTarget();
}

bool EditorGameRenderer::prepareTarget(const EditorViewportArea area,
                                       std::string &error) {
  if (area.width == 0 || area.height == 0)
    return true;
  return ensureTarget(area.width, area.height, error);
}

bool EditorGameRenderer::render(const runtime::World &world,
                                const EditorViewportArea area,
                                const float deltaSeconds,
                                const float interpolationAlpha,
                                std::string &error) {
  runtime::ProfileScope renderScope("Render.submit");
  if (renderer2D_ == nullptr || renderer3D_ == nullptr) {
    error = "The Game view renderer is not configured.";
    return false;
  }
  if (area.width == 0 || area.height == 0)
    return true;
  if (!target_->frameBuffer) {
    if (!ensureTarget(area.width, area.height, error))
      return false;
  } else if (targetWidth_ != area.width || targetHeight_ != area.height) {
    // The current UI command list still references the previous texture.
    // prepareTarget resizes it safely before the next UI frame.
    return true;
  }
  if (!runtime::sceneIs3D(world)) {
    const runtime::Camera2DComponent fallback;
    const runtime::Camera2DComponent *camera = runtime::activeCamera(world);
    if (!renderer2D_->beginFrameRegion(camera != nullptr ? *camera : fallback,
                                       runtime::activeCameraPosition(world), 8,
                                       area.x, area.y, area.width, area.height,
                                       deltaSeconds, error, interpolationAlpha,
                                       target_->frameBuffer))
      return false;
    const bool rendered =
        renderer2D_->drawWorld(world) && renderer2D_->drawHud(world);
    const bool flushed = renderer2D_->endFrame(error);
    const auto &statistics = renderer2D_->statistics();
    runtime::RuntimeProfiler::setGauge("Renderer2D.draw_calls",
                                       statistics.drawCalls);
    runtime::RuntimeProfiler::setGauge("Renderer2D.triangles",
                                       statistics.triangles);
    runtime::RuntimeProfiler::setGauge("Renderer2D.quads", statistics.quads);
    if (!rendered && error.empty())
      error = "Could not queue the 2D Game view.";
    return rendered && flushed;
  }

  runtime::render::BgfxCameraFrame3D frame;
  frame.cameraId = "embedded-game-camera";
  frame.viewportX = area.x;
  frame.viewportY = area.y;
  frame.viewportWidth = area.width;
  frame.viewportHeight = area.height;
  frame.viewId = 8;
  frame.frameBuffer = target_->frameBuffer;
  frame.camera.renderHudToTarget = true;
  const auto cameras = runtime::renderCameras3D(world);
  if (!cameras.empty()) {
    const runtime::Entity &entity = *cameras.front();
    frame.camera = *entity.component<runtime::Camera3DComponent>();
    frame.camera.renderHudToTarget = true;
    if (const auto transform =
            runtime::resolveWorldTransform3D(world, entity)) {
      frame.position = transform->position;
      frame.forward =
          runtime::transformDirection3D(*transform, frame.camera.targetOffset);
      frame.up = runtime::transformDirection3D(
          *transform, {0.0F, frame.camera.upAxis, 0.0F});
    }
  }
  const bool rendered =
      renderer3D_->renderFrame(world, frame, deltaSeconds, error);
  const auto &statistics = renderer3D_->statistics();
  runtime::RuntimeProfiler::setGauge("Renderer3D.batches", statistics.batches);
  runtime::RuntimeProfiler::setGauge("Renderer3D.triangles",
                                     statistics.triangles);
  runtime::RuntimeProfiler::setGauge("Renderer3D.visible_meshes",
                                     statistics.visibleMeshes);
  runtime::RuntimeProfiler::setGauge("Renderer3D.culled_meshes",
                                     statistics.culledMeshes);
  runtime::RuntimeProfiler::setGauge("Renderer3D.render_target_bytes",
                                     statistics.renderTargetBytes);
  return rendered;
}

std::uint16_t EditorGameRenderer::textureIndex() const {
  if (!target_->color)
    return UINT16_MAX;
  const auto *lookup =
      dynamic_cast<const runtime::render::BgfxResourceLookup *>(&resources_);
  if (lookup == nullptr)
    return UINT16_MAX;
  const bgfx::TextureHandle texture = lookup->bgfxTexture(target_->color);
  return bgfx::isValid(texture) ? texture.idx : UINT16_MAX;
}

bool EditorGameRenderer::ensureTarget(const std::uint16_t width,
                                      const std::uint16_t height,
                                      std::string &error) {
  if (target_->frameBuffer && targetWidth_ == width && targetHeight_ == height)
    return true;
  destroyTarget();
  *target_ = resources_.createRenderTarget(
      {.width = width,
       .height = height,
       .colorFormat = runtime::render::TextureFormat::RGBA8,
       .depth = true,
       .debugName = "Editor embedded Game view"},
      error);
  if (!target_->frameBuffer || !target_->color) {
    destroyTarget();
    return false;
  }
  targetWidth_ = width;
  targetHeight_ = height;
  return true;
}

void EditorGameRenderer::destroyTarget() {
  if (target_->frameBuffer)
    resources_.destroy(target_->frameBuffer);
  if (target_->depth)
    resources_.destroy(target_->depth);
  if (target_->color)
    resources_.destroy(target_->color);
  *target_ = {};
  targetWidth_ = 0;
  targetHeight_ = 0;
}

} // namespace demi::editor
